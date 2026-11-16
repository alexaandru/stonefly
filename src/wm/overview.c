#include "wm/overview.h"

#include <linux/input-event-codes.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <scenefx/types/fx/clipped_region.h>
#include <scenefx/types/wlr_scene.h>
#include <xkbcommon/xkbcommon.h>

#include "anim/anim.h"
#include "config/config.h"
#include "log/log.h"
#include "output/output.h"
#include "seat/seat.h"
#include "server/server.h"
#include "session/session.h"
#include "util/color.h"
#include "util/math.h"
#include "util/time.h"
#include "wm/scratchpad.h"
#include "wm/wm.h"
#include "wm/workspace.h"
#include "toplevel/toplevel.h"

/* Gap between workspace previews as a fraction of scaled row height. */
static const double sf_overview_row_gap_frac = 0.1;

struct sf_overview_card {
	struct wl_list link;
	struct sf_server *server;
	struct sf_toplevel *toplevel;
	struct sf_workspace *workspace;
	struct wlr_scene_tree *tree;
	struct wlr_scene_buffer *buffer;
	struct wlr_scene_rect *border;
	struct wlr_surface *surface;
	struct wl_listener surface_commit;
	struct wl_listener surface_destroy;
	struct wlr_box world; /* layout geom when built / last laid out */
	int workspace_index;
	/* Last laid-out card box in layout coords (hit-test). */
	struct wlr_box box;
};

struct sf_overview_dim {
	struct wl_list link;
	struct wlr_scene_rect *rect;
	struct sf_output *output;
};

static void overview_teardown(struct sf_server *server);
static void overview_apply_progress(struct sf_server *server);
static void overview_layout(struct sf_server *server);
static bool overview_begin(struct sf_server *server);
static void overview_start_anim(struct sf_server *server, double target, bool closing);

bool sf_overview_active(struct sf_server *server) {
	return server != NULL && server->overview.active;
}

bool sf_overview_interactive(struct sf_server *server) {
	return server != NULL && server->overview.active && !server->overview.closing;
}

static double overview_settled_zoom(struct sf_server *server) {
	double z = (double)sf_cfg(server)->overview_zoom;
	if (z < 0.1) {
		z = 0.1;
	}
	if (z > 0.75) {
		z = 0.75;
	}
	return z;
}

static double overview_zoom_at(struct sf_server *server, double progress) {
	double settled = overview_settled_zoom(server);
	return 1.0 + (settled - 1.0) * sf_clamp01(progress);
}

static void card_sync_buffer(struct sf_overview_card *card) {
	if (card->buffer == NULL || card->surface == NULL) {
		return;
	}
	struct wlr_buffer *committed = NULL;
	if (card->surface->buffer != NULL) {
		committed = &card->surface->buffer->base;
	}
	wlr_scene_buffer_set_buffer(card->buffer, committed);
	if (committed == NULL) {
		wlr_scene_node_set_enabled(&card->buffer->node, false);
		return;
	}
	wlr_scene_node_set_enabled(&card->buffer->node, true);
	wlr_scene_buffer_set_transform(card->buffer, card->surface->current.transform);
}

static void card_surface_commit(struct wl_listener *listener, void *data) {
	(void)data;
	struct sf_overview_card *card = wl_container_of(listener, card, surface_commit);
	card_sync_buffer(card);
	if (card->server->overview.active) {
		overview_layout(card->server);
	}
}

static void card_destroy(struct sf_overview_card *card) {
	wl_list_remove(&card->surface_commit.link);
	wl_list_remove(&card->surface_destroy.link);
	if (card->tree != NULL) {
		wlr_scene_node_destroy(&card->tree->node);
		card->tree = NULL;
	}
	wl_list_remove(&card->link);
	free(card);
}

static void card_surface_destroy(struct wl_listener *listener, void *data) {
	(void)data;
	struct sf_overview_card *card = wl_container_of(listener, card, surface_destroy);
	struct sf_server *server = card->server;
	card_destroy(card);
	if (server->overview.active) {
		overview_layout(server);
	}
}

static struct wlr_surface *toplevel_root_surface(struct sf_toplevel *toplevel) {
	if (toplevel == NULL || toplevel->xdg_toplevel == NULL ||
	    toplevel->xdg_toplevel->base == NULL) {
		return NULL;
	}
	return toplevel->xdg_toplevel->base->surface;
}

static struct sf_overview_card *card_create(struct sf_server *server,
					    struct sf_toplevel *toplevel) {
	struct wlr_surface *surface = toplevel_root_surface(toplevel);
	if (surface == NULL || toplevel->workspace == NULL || toplevel->scene_tree == NULL) {
		return NULL;
	}

	struct sf_overview_card *card = calloc(1, sizeof(*card));
	if (card == NULL) {
		return NULL;
	}
	card->server = server;
	card->toplevel = toplevel;
	card->workspace = toplevel->workspace;
	card->workspace_index = toplevel->workspace->index;
	card->surface = surface;
	sf_toplevel_layout_geom(toplevel, &card->world);

	card->tree = wlr_scene_tree_create(server->overview.tree);
	if (card->tree == NULL) {
		free(card);
		return NULL;
	}
	card->buffer = wlr_scene_buffer_create(card->tree, NULL);
	if (card->buffer == NULL) {
		wlr_scene_node_destroy(&card->tree->node);
		free(card);
		return NULL;
	}
	wlr_scene_buffer_set_filter_mode(card->buffer, WLR_SCALE_FILTER_BILINEAR);
	wlr_scene_buffer_set_corner_radii(card->buffer,
					  corner_radii_all(sf_cfg(server)->corner_radius));
	wlr_scene_buffer_set_opacity(card->buffer, sf_cfg(server)->client_opacity);

	float border_col[4] = {0.10f, 0.75f, 1.00f, 0.0f};
	card->border = wlr_scene_rect_create(card->tree, 1, 1, border_col);
	if (card->border != NULL) {
		wlr_scene_node_set_position(&card->border->node, -2, -2);
		wlr_scene_node_set_enabled(&card->border->node, false);
		wlr_scene_node_lower_to_bottom(&card->border->node);
	}

	card->surface_commit.notify = card_surface_commit;
	wl_signal_add(&surface->events.commit, &card->surface_commit);
	card->surface_destroy.notify = card_surface_destroy;
	wl_signal_add(&surface->events.destroy, &card->surface_destroy);

	card_sync_buffer(card);
	wl_list_insert(server->overview.cards.prev, &card->link);
	return card;
}

static void overview_clear_cards(struct sf_server *server) {
	struct sf_overview_card *card, *tmp;
	wl_list_for_each_safe(card, tmp, &server->overview.cards, link) {
		card_destroy(card);
	}
}

static void overview_clear_dims(struct sf_server *server) {
	struct sf_overview_dim *dim, *tmp;
	wl_list_for_each_safe(dim, tmp, &server->overview.dims, link) {
		if (dim->rect != NULL) {
			wlr_scene_node_destroy(&dim->rect->node);
		}
		wl_list_remove(&dim->link);
		free(dim);
	}
}

static void overview_build_dims(struct sf_server *server) {
	overview_clear_dims(server);
	struct sf_rgba tint;
	sf_rgba_set(&tint, 0.0f, 0.0f, 0.0f, 0.45f);
	struct sf_output *output;
	wl_list_for_each(output, &server->outputs, link) {
		struct wlr_box box;
		wlr_output_layout_get_box(server->output_layout, output->wlr_output, &box);
		if (box.width <= 0 || box.height <= 0) {
			continue;
		}
		struct sf_overview_dim *dim = calloc(1, sizeof(*dim));
		if (dim == NULL) {
			continue;
		}
		dim->output = output;
		dim->rect = wlr_scene_rect_create(server->overview.tree, box.width, box.height,
						  sf_rgba_vec(&tint));
		if (dim->rect == NULL) {
			free(dim);
			continue;
		}
		wlr_scene_node_set_position(&dim->rect->node, box.x, box.y);
		wlr_scene_node_lower_to_bottom(&dim->rect->node);
		wl_list_insert(server->overview.dims.prev, &dim->link);
	}
}

static void overview_build_cards(struct sf_server *server) {
	overview_clear_cards(server);
	struct sf_toplevel *toplevel;
	wl_list_for_each(toplevel, &server->toplevels, link) {
		if (toplevel->minimized || sf_toplevel_is_scratchpad(toplevel)) {
			continue;
		}
		if (toplevel->xdg_toplevel == NULL ||
		    !toplevel->xdg_toplevel->base->surface->mapped) {
			continue;
		}
		if (toplevel->workspace == NULL) {
			continue;
		}
		(void)card_create(server, toplevel);
	}
}

static void overview_layout(struct sf_server *server) {
	if (!server->overview.active || server->overview.tree == NULL) {
		return;
	}

	double progress = server->overview.progress;
	double zoom = overview_zoom_at(server, progress);
	struct sf_toplevel *live = sf_focused_toplevel(server);

	/* Dim alpha follows open progress. */
	struct sf_overview_dim *dim;
	wl_list_for_each(dim, &server->overview.dims, link) {
		if (dim->rect == NULL) {
			continue;
		}
		struct sf_rgba tint;
		sf_rgba_set(&tint, 0.0f, 0.0f, 0.0f, (float)(0.45 * sf_clamp01(progress)));
		wlr_scene_rect_set_color(dim->rect, sf_rgba_vec(&tint));
		struct wlr_box box;
		wlr_output_layout_get_box(server->output_layout, dim->output->wlr_output, &box);
		wlr_scene_rect_set_size(dim->rect, box.width, box.height);
		wlr_scene_node_set_position(&dim->rect->node, box.x, box.y);
	}

	struct sf_overview_card *card;
	wl_list_for_each(card, &server->overview.cards, link) {
		struct sf_workspace *ws = card->workspace;
		struct sf_output *output = ws != NULL ? ws->output : NULL;
		if (output == NULL || card->toplevel == NULL) {
			wlr_scene_node_set_enabled(&card->tree->node, false);
			continue;
		}

		/* Refresh world geom from the live window when its tree is still up. */
		if (card->toplevel->scene_tree != NULL) {
			sf_toplevel_layout_geom(card->toplevel, &card->world);
		}
		card->workspace_index = ws->index;

		struct wlr_box out_box;
		wlr_output_layout_get_box(server->output_layout, output->wlr_output, &out_box);
		if (out_box.width <= 0 || out_box.height <= 0) {
			wlr_scene_node_set_enabled(&card->tree->node, false);
			continue;
		}

		int preview_w = (int)lround((double)out_box.width * zoom);
		int preview_h = (int)lround((double)out_box.height * zoom);
		if (preview_w < 1) {
			preview_w = 1;
		}
		if (preview_h < 1) {
			preview_h = 1;
		}
		int gap = (int)lround(sf_overview_row_gap_frac * (double)out_box.height * zoom);
		int base_x = out_box.x + (out_box.width - preview_w) / 2;
		int base_y = out_box.y + (out_box.height - preview_h) / 2;
		double row_scroll = output->active != NULL ? (double)output->active->index : 0.0;
		int preview_y = base_y + (int)lround(((double)card->workspace_index - row_scroll) *
						     (double)(preview_h + gap));
		int preview_x = base_x;

		double rel_x = (double)(card->world.x - out_box.x);
		double rel_y = (double)(card->world.y - out_box.y);
		int card_w = (int)lround((double)card->world.width * zoom);
		int card_h = (int)lround((double)card->world.height * zoom);
		if (card_w < 1) {
			card_w = 1;
		}
		if (card_h < 1) {
			card_h = 1;
		}
		int card_x = preview_x + (int)lround(rel_x * zoom);
		int card_y = preview_y + (int)lround(rel_y * zoom);

		card->box.x = card_x;
		card->box.y = card_y;
		card->box.width = card_w;
		card->box.height = card_h;

		wlr_scene_node_set_enabled(&card->tree->node, true);
		wlr_scene_node_set_position(&card->tree->node, card_x, card_y);
		wlr_scene_buffer_set_dest_size(card->buffer, card_w, card_h);
		wlr_scene_buffer_set_corner_radii(
			card->buffer, corner_radii_all((int)lround(
					      (double)sf_cfg(server)->corner_radius * zoom)));

		bool focused = card->toplevel == live;
		if (card->border != NULL) {
			int bw = 2;
			wlr_scene_rect_set_size(card->border, card_w + 2 * bw, card_h + 2 * bw);
			wlr_scene_node_set_position(&card->border->node, -bw, -bw);
			struct sf_rgba col;
			if (focused) {
				sf_rgba_copy(&col, &sf_cfg(server)->border_active);
				col.a = (float)sf_clamp01(progress);
			} else {
				sf_rgba_copy(&col, &sf_cfg(server)->border_inactive);
				col.a = (float)(0.35 * sf_clamp01(progress));
			}
			wlr_scene_rect_set_color(card->border, sf_rgba_vec(&col));
			wlr_scene_node_set_enabled(&card->border->node, progress > 0.05);
		}
	}
}

static void overview_apply_progress(struct sf_server *server) {
	overview_layout(server);
	sf_anim_schedule(server);
}

static void overview_start_anim(struct sf_server *server, double target, bool closing) {
	server->overview.closing = closing;
	server->overview.anim_from = server->overview.progress;
	server->overview.anim_to = sf_clamp01(target);
	clock_gettime(CLOCK_MONOTONIC, &server->overview.anim_start);
	int ms = sf_cfg(server)->slide_duration_ms;
	if (ms < 0) {
		ms = 0;
	}
	server->overview.anim_dur_ns = (int64_t)ms * 1000000LL;
	server->overview.anim_active = true;
	if (ms == 0) {
		server->overview.progress = server->overview.anim_to;
		server->overview.anim_active = false;
		overview_apply_progress(server);
		if (closing || server->overview.progress <= 0.0) {
			overview_teardown(server);
		}
		return;
	}
	sf_anim_schedule(server);
}

static void overview_restore_desktop(struct sf_server *server) {
	if (server->layers.toplevel != NULL) {
		wlr_scene_node_set_enabled(&server->layers.toplevel->node, true);
	}
	if (server->layers.fullscreen != NULL) {
		wlr_scene_node_set_enabled(&server->layers.fullscreen->node, true);
	}
}

static void overview_teardown(struct sf_server *server) {
	struct sf_toplevel *focus = server->overview.pending_focus;
	server->overview.pending_focus = NULL;
	server->overview.anim_active = false;
	server->overview.closing = false;
	server->overview.progress = 0.0;
	server->overview.gesture_active = false;

	overview_clear_cards(server);
	overview_clear_dims(server);
	if (server->overview.tree != NULL) {
		wlr_scene_node_set_enabled(&server->overview.tree->node, false);
	}
	overview_restore_desktop(server);
	server->overview.active = false;

	if (focus != NULL && focus->xdg_toplevel != NULL &&
	    focus->xdg_toplevel->base->surface->mapped && !focus->minimized) {
		sf_workspace_reveal_toplevel(focus);
		sf_focus_toplevel(focus);
	} else {
		sf_focus_top(server);
	}
	SF_LOGI("overview", sf_str("event", "closed"));
}

static bool overview_begin(struct sf_server *server) {
	if (server->overview.active) {
		return true;
	}
	if (sf_session_locked(server)) {
		return false;
	}
	if (server->overview.tree == NULL) {
		return false;
	}
	if (server->seat != NULL && server->seat->drag != NULL) {
		SF_LOGD("overview open ignored during client drag");
		return false;
	}

	sf_reset_cursor_mode(server);
	sf_scratchpad_hide_all(server);

	/* Finish in-flight workspace slides so cards start from a settled strip. */
	struct sf_output *output;
	wl_list_for_each(output, &server->outputs, link) {
		if (output->ws_anim.active && output->ws_anim.to != NULL) {
			sf_workspace_switch(output, output->ws_anim.to);
		}
	}

	overview_build_dims(server);
	overview_build_cards(server);
	if (wl_list_empty(&server->overview.dims)) {
		overview_clear_cards(server);
		return false;
	}

	server->overview.active = true;
	server->overview.closing = false;
	server->overview.progress = 0.0;
	server->overview.pending_focus = NULL;
	server->overview.anim_active = false;

	if (server->layers.toplevel != NULL) {
		wlr_scene_node_set_enabled(&server->layers.toplevel->node, false);
	}
	if (server->layers.fullscreen != NULL) {
		wlr_scene_node_set_enabled(&server->layers.fullscreen->node, false);
	}
	wlr_scene_node_set_enabled(&server->overview.tree->node, true);

	wlr_seat_keyboard_notify_clear_focus(server->seat);
	wlr_seat_pointer_clear_focus(server->seat);

	overview_apply_progress(server);
	SF_LOGI("overview", sf_str("event", "opened"));
	return true;
}

void sf_overview_open(struct sf_server *server) {
	if (server == NULL) {
		return;
	}
	if (server->overview.active) {
		server->overview.closing = false;
		server->overview.pending_focus = NULL;
		if (server->overview.progress < 1.0) {
			overview_start_anim(server, 1.0, false);
		}
		return;
	}
	if (!overview_begin(server)) {
		return;
	}
	overview_start_anim(server, 1.0, false);
}

void sf_overview_close(struct sf_server *server) {
	if (server == NULL || !server->overview.active) {
		return;
	}
	server->overview.pending_focus = NULL;
	overview_start_anim(server, 0.0, true);
}

void sf_overview_toggle(struct sf_server *server) {
	if (server == NULL) {
		return;
	}
	if (sf_overview_interactive(server)) {
		sf_overview_close(server);
		return;
	}
	if (server->overview.active && server->overview.closing) {
		/* Re-open while closing. */
		server->overview.closing = false;
		server->overview.pending_focus = NULL;
		overview_start_anim(server, 1.0, false);
		return;
	}
	sf_overview_open(server);
}

void sf_overview_force_close(struct sf_server *server) {
	if (server == NULL || !server->overview.active) {
		return;
	}
	server->overview.pending_focus = NULL;
	overview_teardown(server);
}

void sf_overview_gesture_update(struct sf_server *server, double progress) {
	if (server == NULL) {
		return;
	}
	if (!server->overview.active) {
		if (progress <= 0.0) {
			return;
		}
		if (!overview_begin(server)) {
			return;
		}
	}
	server->overview.anim_active = false;
	server->overview.closing = false;
	server->overview.progress = progress;
	overview_apply_progress(server);
}

void sf_overview_gesture_end(struct sf_server *server, bool commit_open) {
	if (server == NULL || !server->overview.active) {
		return;
	}
	if (commit_open) {
		overview_start_anim(server, 1.0, false);
		return;
	}
	overview_start_anim(server, 0.0, true);
}

static struct sf_overview_card *card_at(struct sf_server *server, double lx, double ly) {
	struct sf_overview_card *card;
	wl_list_for_each_reverse(card, &server->overview.cards, link) {
		if (card->tree == NULL || !card->tree->node.enabled) {
			continue;
		}
		if (lx >= (double)card->box.x && ly >= (double)card->box.y &&
		    lx < (double)(card->box.x + card->box.width) &&
		    ly < (double)(card->box.y + card->box.height)) {
			return card;
		}
	}
	return NULL;
}

bool sf_overview_handle_button(struct sf_server *server, uint32_t button, bool pressed, double lx,
			       double ly) {
	if (!sf_overview_interactive(server)) {
		return false;
	}
	if (!pressed) {
		return true; /* swallow releases while overview owns the pointer */
	}
	if (button != BTN_LEFT) {
		return true;
	}

	struct sf_overview_card *card = card_at(server, lx, ly);
	if (card != NULL && card->toplevel != NULL) {
		server->overview.pending_focus = card->toplevel;
		if (card->workspace != NULL && card->workspace->output != NULL &&
		    card->workspace->output->active != card->workspace) {
			sf_workspace_switch(card->workspace->output, card->workspace);
		}
		overview_start_anim(server, 0.0, true);
		return true;
	}
	/* Empty click closes. */
	sf_overview_close(server);
	return true;
}

bool sf_overview_handle_key(struct sf_server *server, uint32_t keysym) {
	if (!sf_overview_interactive(server)) {
		return false;
	}
	if (keysym == XKB_KEY_Escape) {
		sf_overview_close(server);
		return true;
	}
	return false;
}

void sf_overview_relayout(struct sf_server *server) {
	if (server == NULL || !server->overview.active) {
		return;
	}
	overview_layout(server);
	sf_anim_schedule(server);
}

void sf_overview_on_toplevel_mapped(struct sf_toplevel *toplevel) {
	if (toplevel == NULL || toplevel->server == NULL) {
		return;
	}
	struct sf_server *server = toplevel->server;
	if (!server->overview.active) {
		return;
	}
	if (toplevel->minimized || sf_toplevel_is_scratchpad(toplevel)) {
		return;
	}
	struct sf_overview_card *existing;
	wl_list_for_each(existing, &server->overview.cards, link) {
		if (existing->toplevel == toplevel) {
			return;
		}
	}
	(void)card_create(server, toplevel);
	overview_layout(server);
}

void sf_overview_on_toplevel_unmapped(struct sf_toplevel *toplevel) {
	if (toplevel == NULL || toplevel->server == NULL) {
		return;
	}
	struct sf_server *server = toplevel->server;
	if (!server->overview.active) {
		return;
	}
	struct sf_overview_card *card, *tmp;
	wl_list_for_each_safe(card, tmp, &server->overview.cards, link) {
		if (card->toplevel == toplevel) {
			if (server->overview.pending_focus == toplevel) {
				server->overview.pending_focus = NULL;
			}
			card_destroy(card);
			overview_layout(server);
			return;
		}
	}
}

void sf_overview_tick(struct sf_server *server, const struct timespec *now) {
	if (server == NULL || !server->overview.active || !server->overview.anim_active) {
		return;
	}
	int64_t elapsed = sf_timespec_ns(now) - sf_timespec_ns(&server->overview.anim_start);
	double t = 1.0;
	if (server->overview.anim_dur_ns > 0) {
		t = (double)elapsed / (double)server->overview.anim_dur_ns;
	}
	double p = sf_ease_out_cubic(t);
	server->overview.progress = server->overview.anim_from +
				    (server->overview.anim_to - server->overview.anim_from) * p;
	overview_apply_progress(server);
	if (t >= 1.0) {
		server->overview.anim_active = false;
		server->overview.progress = server->overview.anim_to;
		if (server->overview.closing || server->overview.progress <= 0.0) {
			overview_teardown(server);
		}
	}
}

bool sf_overview_anim_busy(struct sf_server *server) {
	return server != NULL && server->overview.active && server->overview.anim_active;
}
