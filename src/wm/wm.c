#define _POSIX_C_SOURCE 200809L

#include <stdlib.h>
#include <unistd.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <scenefx/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/box.h>

#include "anim/anim.h"
#include "decor/decor.h"
#include "layer/layer.h"
#include "log/log.h"
#include "output/output.h"
#include "session/session.h"
#include "toplevel/foreign.h"
#include "wm/scratchpad.h"
#include "wm/wm.h"

void stonefly_toplevel_layout_geom(struct stonefly_toplevel *toplevel,
		struct wlr_box *box) {
	struct wlr_box *geo = &toplevel->xdg_toplevel->base->geometry;
	box->x = toplevel->scene_tree->node.x;
	box->y = toplevel->scene_tree->node.y;
	box->width = geo->width;
	box->height = geo->height;
	if (box->width <= 0 || box->height <= 0) {
		box->width = toplevel->xdg_toplevel->base->surface->current.width;
		box->height = toplevel->xdg_toplevel->base->surface->current.height;
	}
}

static void stonefly_toplevel_save_geom(struct stonefly_toplevel *toplevel) {
	if (toplevel->has_saved_geom || toplevel->maximized_edges ||
			toplevel->maximized_gaps || toplevel->fullscreen ||
			toplevel->snapped != STONEFLY_SNAP_NONE) {
		return;
	}
	struct wlr_box geom;
	stonefly_toplevel_layout_geom(toplevel, &geom);
	/* Never remember a zero-sized box — restore would make the window vanish. */
	if (geom.width <= 0 || geom.height <= 0) {
		return;
	}
	toplevel->saved_geom = geom;
	toplevel->has_saved_geom = true;
}

bool stonefly_toplevel_output_box(struct stonefly_toplevel *toplevel,
		struct wlr_box *box) {
	struct stonefly_server *server = toplevel->server;
	struct wlr_box geom;
	stonefly_toplevel_layout_geom(toplevel, &geom);
	double cx = geom.x + geom.width / 2.0;
	double cy = geom.y + geom.height / 2.0;
	if (stonefly_layout_usable_at(server, cx, cy, box)) {
		return true;
	}
	return stonefly_layout_usable_at(server,
		server->cursor->x, server->cursor->y, box);
}

void stonefly_box_inset_gap(struct wlr_box *box) {
	const int g = STONEFLY_GAP_SIZE;
	if (box->width > 2 * g) {
		box->x += g;
		box->width -= 2 * g;
	}
	if (box->height > 2 * g) {
		box->y += g;
		box->height -= 2 * g;
	}
}

static void stonefly_cover_fractional_gap(struct stonefly_server *server,
		struct wlr_box *box) {
	/* Same class of gap as peeble at non-integer scale: grow 1 logical px. */
	float scale = stonefly_output_scale_factor(server);
	if (scale != (float)(int)scale) {
		box->width += 1;
		box->height += 1;
	}
}

/* wlroots 0.20 asserts schedule_configure only after the first commit. */
static bool xdg_surface_ready(struct stonefly_toplevel *toplevel) {
	return toplevel->xdg_toplevel->base->initialized;
}

static void stonefly_toplevel_apply_box(struct stonefly_toplevel *toplevel,
		const struct wlr_box *box) {
	stonefly_anim_cancel(toplevel, false);
	wlr_scene_node_set_position(&toplevel->scene_tree->node, box->x, box->y);
	toplevel->resting_x = box->x;
	toplevel->resting_y = box->y;
	if (!xdg_surface_ready(toplevel)) {
		return;
	}
	wlr_xdg_toplevel_set_size(toplevel->xdg_toplevel, box->width, box->height);
}

void stonefly_toplevel_restore_saved(struct stonefly_toplevel *toplevel) {
	if (!toplevel->has_saved_geom) {
		return;
	}
	if (xdg_surface_ready(toplevel)) {
		wlr_xdg_toplevel_set_maximized(toplevel->xdg_toplevel, false);
		wlr_xdg_toplevel_set_fullscreen(toplevel->xdg_toplevel, false);
	}
	struct wlr_box from;
	stonefly_toplevel_layout_geom(toplevel, &from);
	struct wlr_box to = toplevel->saved_geom;
	toplevel->has_saved_geom = false;
	toplevel->maximized_edges = false;
	toplevel->maximized_gaps = false;
	toplevel->fullscreen = false;
	bool was_snapped = toplevel->snapped != STONEFLY_SNAP_NONE;
	toplevel->snapped = STONEFLY_SNAP_NONE;
	/* Live morph only for snap/unsnap restores; max/fullscreen stay instant. */
	if (was_snapped && from.width > 0 && from.height > 0 &&
			stonefly_anim_start_morph(toplevel, &from, &to)) {
		return;
	}
	stonefly_toplevel_apply_box(toplevel, &to);
	stonefly_decor_update_geom(toplevel);
}

void stonefly_toplevel_capture_geom(struct stonefly_toplevel *toplevel,
		const struct wlr_box *box) {
	if (toplevel->has_saved_geom || box == NULL ||
			box->width <= 0 || box->height <= 0) {
		return;
	}
	toplevel->saved_geom = *box;
	toplevel->has_saved_geom = true;
}

void stonefly_toplevel_apply_saved_at(struct stonefly_toplevel *toplevel,
		int x, int y) {
	if (!toplevel->has_saved_geom) {
		return;
	}
	struct wlr_box box = toplevel->saved_geom;
	box.x = x;
	box.y = y;
	if (xdg_surface_ready(toplevel)) {
		wlr_xdg_toplevel_set_maximized(toplevel->xdg_toplevel, false);
	}
	stonefly_toplevel_apply_box(toplevel, &box);
	toplevel->maximized_edges = false;
	toplevel->maximized_gaps = false;
	toplevel->snapped = STONEFLY_SNAP_NONE;
	stonefly_decor_update_geom(toplevel);
}

void stonefly_toplevel_set_max_edges(struct stonefly_toplevel *toplevel, bool want) {
	if (!xdg_surface_ready(toplevel)) {
		return;
	}
	if (want == toplevel->maximized_edges) {
		if (want) {
			/* Still send a configure so the client is not left hanging. */
			wlr_xdg_surface_schedule_configure(toplevel->xdg_toplevel->base);
		}
		return;
	}
	if (!want) {
		if (toplevel->has_saved_geom) {
			stonefly_toplevel_restore_saved(toplevel);
		} else {
			wlr_xdg_toplevel_set_maximized(toplevel->xdg_toplevel, false);
			toplevel->maximized_edges = false;
			toplevel->maximized_gaps = false;
			wlr_xdg_surface_schedule_configure(toplevel->xdg_toplevel->base);
			stonefly_decor_update_geom(toplevel);
		}
		stonefly_foreign_toplevel_on_state_change(toplevel);
		return;
	}
	if (toplevel->fullscreen) {
		wlr_xdg_toplevel_set_fullscreen(toplevel->xdg_toplevel, false);
		toplevel->fullscreen = false;
	}
	struct wlr_box out;
	if (!stonefly_toplevel_output_box(toplevel, &out)) {
		wlr_xdg_surface_schedule_configure(toplevel->xdg_toplevel->base);
		return;
	}
	stonefly_toplevel_save_geom(toplevel);
	toplevel->snapped = STONEFLY_SNAP_NONE;
	toplevel->maximized_gaps = false;
	stonefly_cover_fractional_gap(toplevel->server, &out);
	wlr_xdg_toplevel_set_maximized(toplevel->xdg_toplevel, true);
	stonefly_toplevel_apply_box(toplevel, &out);
	toplevel->maximized_edges = true;
	/* Drop blur/border/shadow/corners immediately (commit may lag). */
	stonefly_decor_update_geom(toplevel);
	stonefly_foreign_toplevel_on_state_change(toplevel);
	stonefly_foreign_toplevel_on_outputs_change(toplevel);
}

void stonefly_toplevel_toggle_max_edges(struct stonefly_toplevel *toplevel) {
	stonefly_toplevel_set_max_edges(toplevel, !toplevel->maximized_edges);
}

static bool toplevel_full_output_box(struct stonefly_toplevel *toplevel,
		struct wlr_output *hint, struct wlr_box *box) {
	struct stonefly_server *server = toplevel->server;
	struct wlr_output *wlr_output = hint;
	if (wlr_output == NULL) {
		struct wlr_box geom;
		stonefly_toplevel_layout_geom(toplevel, &geom);
		double cx = geom.x + geom.width / 2.0;
		double cy = geom.y + geom.height / 2.0;
		wlr_output = wlr_output_layout_output_at(server->output_layout, cx, cy);
		if (wlr_output == NULL) {
			wlr_output = wlr_output_layout_output_at(server->output_layout,
				server->cursor->x, server->cursor->y);
		}
	}
	if (wlr_output == NULL) {
		return false;
	}
	wlr_output_layout_get_box(server->output_layout, wlr_output, box);
	return box->width > 0 && box->height > 0;
}

void stonefly_toplevel_set_fullscreen(struct stonefly_toplevel *toplevel,
		bool want, struct wlr_output *hint) {
	if (!xdg_surface_ready(toplevel)) {
		return;
	}
	if (want == toplevel->fullscreen) {
		if (want) {
			wlr_xdg_surface_schedule_configure(toplevel->xdg_toplevel->base);
		}
		return;
	}
	if (!want) {
		wlr_xdg_toplevel_set_fullscreen(toplevel->xdg_toplevel, false);
		toplevel->fullscreen = false;
		if (toplevel->has_saved_geom) {
			stonefly_toplevel_restore_saved(toplevel);
		} else {
			wlr_xdg_surface_schedule_configure(toplevel->xdg_toplevel->base);
			stonefly_decor_update_geom(toplevel);
		}
		stonefly_foreign_toplevel_on_state_change(toplevel);
		stonefly_foreign_toplevel_on_outputs_change(toplevel);
		return;
	}

	if (toplevel->maximized_edges) {
		wlr_xdg_toplevel_set_maximized(toplevel->xdg_toplevel, false);
		toplevel->maximized_edges = false;
	}
	toplevel->maximized_gaps = false;
	toplevel->snapped = STONEFLY_SNAP_NONE;

	struct wlr_box out;
	if (!toplevel_full_output_box(toplevel, hint, &out)) {
		wlr_xdg_surface_schedule_configure(toplevel->xdg_toplevel->base);
		return;
	}
	stonefly_toplevel_save_geom(toplevel);
	wlr_xdg_toplevel_set_fullscreen(toplevel->xdg_toplevel, true);
	stonefly_toplevel_apply_box(toplevel, &out);
	toplevel->fullscreen = true;
	stonefly_decor_update_geom(toplevel);
	stonefly_foreign_toplevel_on_state_change(toplevel);
	stonefly_foreign_toplevel_on_outputs_change(toplevel);
}

void stonefly_toplevel_set_max_gaps(struct stonefly_toplevel *toplevel, bool want) {
	if (!xdg_surface_ready(toplevel)) {
		return;
	}
	if (want == toplevel->maximized_gaps) {
		if (want) {
			wlr_xdg_surface_schedule_configure(toplevel->xdg_toplevel->base);
		}
		return;
	}
	if (!want) {
		if (toplevel->has_saved_geom) {
			stonefly_toplevel_restore_saved(toplevel);
		} else {
			toplevel->maximized_gaps = false;
			wlr_xdg_surface_schedule_configure(toplevel->xdg_toplevel->base);
		}
		stonefly_foreign_toplevel_on_state_change(toplevel);
		return;
	}
	struct wlr_box out;
	if (!stonefly_toplevel_output_box(toplevel, &out)) {
		wlr_xdg_surface_schedule_configure(toplevel->xdg_toplevel->base);
		return;
	}
	stonefly_toplevel_save_geom(toplevel);
	toplevel->snapped = STONEFLY_SNAP_NONE;
	if (toplevel->maximized_edges) {
		wlr_xdg_toplevel_set_maximized(toplevel->xdg_toplevel, false);
		toplevel->maximized_edges = false;
	}
	/* Gap-max: WM-only — do not inform the client it is maximized (peeble). */
	stonefly_box_inset_gap(&out);
	stonefly_toplevel_apply_box(toplevel, &out);
	toplevel->maximized_gaps = true;
	stonefly_foreign_toplevel_on_state_change(toplevel);
	stonefly_foreign_toplevel_on_outputs_change(toplevel);
}

void stonefly_toplevel_toggle_max_gaps(struct stonefly_toplevel *toplevel) {
	stonefly_toplevel_set_max_gaps(toplevel, !toplevel->maximized_gaps);
}

/* Peeble snapBorderBox: gap between two half-snaps is exactly GapSize. */
struct wlr_box stonefly_wm_snap_box(enum stonefly_snap edge,
		const struct wlr_box *usable) {
	const int g = STONEFLY_GAP_SIZE;
	struct wlr_box u = *usable;
	int half_w = (u.width - 3 * g) / 2;
	int half_h = (u.height - 3 * g) / 2;
	int full_w = u.width - 2 * g;
	int full_h = u.height - 2 * g;
	struct wlr_box box = {0};

	switch (edge) {
	case STONEFLY_SNAP_LEFT:
		box.x = u.x + g;
		box.y = u.y + g;
		box.width = half_w;
		box.height = full_h;
		break;
	case STONEFLY_SNAP_RIGHT:
		box.x = u.x + 2 * g + half_w;
		box.y = u.y + g;
		box.width = half_w;
		box.height = full_h;
		break;
	case STONEFLY_SNAP_TOP:
		box.x = u.x + g;
		box.y = u.y + g;
		box.width = full_w;
		box.height = half_h;
		break;
	case STONEFLY_SNAP_BOTTOM:
		box.x = u.x + g;
		box.y = u.y + 2 * g + half_h;
		box.width = full_w;
		box.height = half_h;
		break;
	case STONEFLY_SNAP_NONE:
		break;
	}
	return box;
}

void stonefly_toplevel_snap_in(struct stonefly_toplevel *toplevel,
		enum stonefly_snap edge, const struct wlr_box *usable) {
	if (edge == STONEFLY_SNAP_NONE || usable == NULL ||
			!xdg_surface_ready(toplevel)) {
		return;
	}
	/* save_geom no-ops when has_saved_geom (drag origin / PreSnap kept). */
	stonefly_toplevel_save_geom(toplevel);
	if (toplevel->maximized_edges) {
		wlr_xdg_toplevel_set_maximized(toplevel->xdg_toplevel, false);
		toplevel->maximized_edges = false;
	}
	toplevel->maximized_gaps = false;
	struct wlr_box box = stonefly_wm_snap_box(edge, usable);
	if (box.width <= 0 || box.height <= 0) {
		return;
	}
	struct wlr_box from;
	stonefly_toplevel_layout_geom(toplevel, &from);
	toplevel->snapped = edge;
	/* start_morph retargets from the current live rect when already morphing. */
	if (from.width > 0 && from.height > 0 &&
			stonefly_anim_start_morph(toplevel, &from, &box)) {
		return;
	}
	stonefly_toplevel_apply_box(toplevel, &box);
}

void stonefly_toplevel_snap(struct stonefly_toplevel *toplevel,
		enum stonefly_snap edge) {
	struct wlr_box out;
	if (!stonefly_toplevel_output_box(toplevel, &out)) {
		return;
	}
	stonefly_toplevel_snap_in(toplevel, edge, &out);
}

static bool toplevel_on_output(struct stonefly_toplevel *toplevel,
		struct stonefly_output *output) {
	struct wlr_box geom;
	stonefly_toplevel_layout_geom(toplevel, &geom);
	double cx = geom.x + geom.width / 2.0;
	double cy = geom.y + geom.height / 2.0;
	struct wlr_output *at = wlr_output_layout_output_at(
		toplevel->server->output_layout, cx, cy);
	return at == output->wlr_output;
}

static void toplevel_set_resting(struct stonefly_toplevel *toplevel,
		int x, int y) {
	toplevel->resting_x = x;
	toplevel->resting_y = y;
	if (toplevel->anim.active && !toplevel->anim_hide) {
		/* Retarget an in-flight appear slide (peeble RetargetShowResting). */
		toplevel->anim.to_x = x;
		toplevel->anim.to_y = y;
		return;
	}
	if (!toplevel->anim.active) {
		wlr_scene_node_set_position(&toplevel->scene_tree->node, x, y);
	}
}

void stonefly_toplevel_ensure_on_screen(struct stonefly_toplevel *toplevel) {
	if (toplevel == NULL || toplevel->minimized || toplevel->anim_hide) {
		return;
	}
	if (toplevel->maximized_edges || toplevel->maximized_gaps ||
			toplevel->snapped != STONEFLY_SNAP_NONE) {
		return;
	}
	if (toplevel->server->grabbed_toplevel == toplevel) {
		return;
	}

	struct wlr_box usable;
	if (!stonefly_toplevel_output_box(toplevel, &usable)) {
		return;
	}
	struct wlr_box area = usable;
	stonefly_box_inset_gap(&area);
	if (area.width <= 0 || area.height <= 0) {
		return;
	}

	struct wlr_box geom;
	stonefly_toplevel_layout_geom(toplevel, &geom);
	if (geom.width <= 0 || geom.height <= 0) {
		/* Size unknown: at least park the cascade origin inside usable. */
		int x = toplevel->resting_x;
		int y = toplevel->resting_y;
		if (x < area.x) {
			x = area.x;
		}
		if (y < area.y) {
			y = area.y;
		}
		if (x != toplevel->resting_x || y != toplevel->resting_y) {
			toplevel_set_resting(toplevel, x, y);
		}
		return;
	}

	if (geom.width > area.width || geom.height > area.height) {
		stonefly_toplevel_set_max_gaps(toplevel, true);
		return;
	}

	int x = geom.x;
	int y = geom.y;
	if (x + geom.width > area.x + area.width) {
		x = area.x + area.width - geom.width;
	}
	if (y + geom.height > area.y + area.height) {
		y = area.y + area.height - geom.height;
	}
	if (x < area.x) {
		x = area.x;
	}
	if (y < area.y) {
		y = area.y;
	}
	if (x == geom.x && y == geom.y) {
		return;
	}
	toplevel_set_resting(toplevel, x, y);
}

void stonefly_wm_relayout_output(struct stonefly_output *output) {
	if (output == NULL) {
		return;
	}
	struct stonefly_server *server = output->server;
	struct stonefly_toplevel *toplevel;
	wl_list_for_each(toplevel, &server->toplevels, link) {
		if (toplevel->minimized || !toplevel_on_output(toplevel, output)) {
			continue;
		}
		struct wlr_box out;
		if (!stonefly_output_usable_box(output, &out)) {
			continue;
		}
		if (toplevel->maximized_edges) {
			stonefly_cover_fractional_gap(toplevel->server, &out);
			stonefly_toplevel_apply_box(toplevel, &out);
		} else if (toplevel->maximized_gaps) {
			stonefly_box_inset_gap(&out);
			stonefly_toplevel_apply_box(toplevel, &out);
		} else if (toplevel->snapped != STONEFLY_SNAP_NONE) {
			struct wlr_box box = stonefly_wm_snap_box(toplevel->snapped, &out);
			if (box.width > 0 && box.height > 0) {
				stonefly_toplevel_apply_box(toplevel, &box);
			}
		} else {
			/* Floaters: exclusive zone often arrives after PlaceNew. */
			stonefly_toplevel_ensure_on_screen(toplevel);
		}
	}
}

void stonefly_toplevel_unsnap(struct stonefly_toplevel *toplevel) {
	if (toplevel->snapped == STONEFLY_SNAP_NONE &&
			!toplevel->maximized_edges && !toplevel->maximized_gaps) {
		return;
	}
	stonefly_toplevel_restore_saved(toplevel);
}

void stonefly_toplevel_place_cascade(struct stonefly_toplevel *toplevel) {
	struct stonefly_server *server = toplevel->server;
	struct wlr_box out = {0};
	/* Prefer the usable area under the cursor for brand-new maps. */
	if (!stonefly_layout_usable_at(server,
			server->cursor->x, server->cursor->y, &out)) {
		if (!stonefly_toplevel_output_box(toplevel, &out)) {
			return;
		}
	}
	stonefly_box_inset_gap(&out);

	/* Count already-mapped siblings (caller inserts after place). */
	size_t n = 0;
	struct stonefly_toplevel *t;
	wl_list_for_each(t, &toplevel->server->toplevels, link) {
		if (t != toplevel && !t->minimized) {
			n++;
		}
	}
	int offset = (int)(n % STONEFLY_CASCADE_WRAP) * STONEFLY_CASCADE_STEP;
	int x = out.x + offset;
	int y = out.y + offset;
	wlr_scene_node_set_position(&toplevel->scene_tree->node, x, y);
	toplevel->resting_x = x;
	toplevel->resting_y = y;
}

/* minimize_link is either self-linked (off stack) or in server->minimized. */
static void minimize_link_detach(struct stonefly_toplevel *toplevel) {
	if (toplevel->minimize_link.next != &toplevel->minimize_link) {
		wl_list_remove(&toplevel->minimize_link);
	}
	wl_list_init(&toplevel->minimize_link);
}

static bool toplevel_focusable(const struct stonefly_toplevel *toplevel) {
	/* anim_hide: sliding out but minimized not set yet — do not re-pick. */
	return !toplevel->minimized && !toplevel->anim_hide;
}

void stonefly_toplevel_apply_hidden(struct stonefly_toplevel *toplevel,
		enum stonefly_hide_kind kind) {
	struct stonefly_server *server = toplevel->server;
	const bool was_hidden = toplevel->minimized;
	const bool was_focused = stonefly_focused_toplevel(server) == toplevel;

	minimize_link_detach(toplevel);
	if (kind == STONEFLY_HIDE_LIFO) {
		wl_list_insert(&server->minimized, &toplevel->minimize_link);
	}

	if (!was_hidden) {
		toplevel->minimized = true;
		wlr_scene_node_set_enabled(&toplevel->scene_tree->node, false);
	}

	if (was_focused) {
		if (xdg_surface_ready(toplevel)) {
			wlr_xdg_toplevel_set_activated(toplevel->xdg_toplevel, false);
		}
		stonefly_decor_set_focused(toplevel, false);
		stonefly_focus_top(server);
	}
	stonefly_foreign_toplevel_on_state_change(toplevel);
}

void stonefly_toplevel_apply_shown(struct stonefly_toplevel *toplevel,
		bool activate) {
	if (toplevel->minimized) {
		toplevel->minimized = false;
		minimize_link_detach(toplevel);
		/* Enable at resting, then slide in from off-screen. */
		wlr_scene_node_set_position(&toplevel->scene_tree->node,
			toplevel->resting_x, toplevel->resting_y);
		wlr_scene_node_set_enabled(&toplevel->scene_tree->node, true);
		stonefly_anim_start_show(toplevel);
		stonefly_foreign_toplevel_on_state_change(toplevel);
	}
	if (activate) {
		stonefly_focus_toplevel(toplevel);
	}
}

static void minimize_now(struct stonefly_toplevel *toplevel) {
	stonefly_toplevel_apply_hidden(toplevel, STONEFLY_HIDE_LIFO);
}

void stonefly_toplevel_unminimize(struct stonefly_toplevel *toplevel) {
	stonefly_toplevel_apply_shown(toplevel, false);
}

void stonefly_focus_toplevel(struct stonefly_toplevel *toplevel) {
	/* Note: this function only deals with keyboard focus. */
	if (toplevel == NULL) {
		return;
	}
	struct stonefly_server *server = toplevel->server;
	if (stonefly_session_locked(server)) {
		return;
	}
	if (toplevel->minimized) {
		stonefly_toplevel_apply_shown(toplevel, false);
	}
	server->exclusive_layer = NULL;
	struct wlr_seat *seat = server->seat;
	struct wlr_surface *prev_surface = seat->keyboard_state.focused_surface;
	struct wlr_surface *surface = toplevel->xdg_toplevel->base->surface;
	if (prev_surface == surface) {
		/* Already focused (e.g. taskbar click on the active window before
		 * minimize). Still raise so the request is visible. */
		wlr_scene_node_raise_to_top(&toplevel->scene_tree->node);
		wl_list_remove(&toplevel->link);
		wl_list_insert(&server->toplevels, &toplevel->link);
		return;
	}
	struct stonefly_toplevel *prev_toplevel = NULL;
	if (prev_surface) {
		/*
		 * Deactivate the previously focused surface. This lets the client know
		 * it no longer has focus and the client will repaint accordingly, e.g.
		 * stop displaying a caret.
		 */
		struct wlr_xdg_toplevel *prev_xdg =
			wlr_xdg_toplevel_try_from_wlr_surface(prev_surface);
		if (prev_xdg != NULL) {
			wlr_xdg_toplevel_set_activated(prev_xdg, false);
			struct wlr_surface *root = wlr_surface_get_root_surface(prev_surface);
			struct stonefly_toplevel *t;
			wl_list_for_each(t, &server->toplevels, link) {
				if (t->xdg_toplevel->base->surface == root) {
					stonefly_decor_set_focused(t, false);
					prev_toplevel = t;
					break;
				}
			}
		}
	}
	struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(seat);
	/* Move the toplevel to the front */
	wlr_scene_node_raise_to_top(&toplevel->scene_tree->node);
	wl_list_remove(&toplevel->link);
	wl_list_insert(&server->toplevels, &toplevel->link);
	/* Activate the new surface */
	wlr_xdg_toplevel_set_activated(toplevel->xdg_toplevel, true);
	stonefly_decor_set_focused(toplevel, true);
	/*
	 * Tell the seat to have the keyboard enter this surface. wlroots will keep
	 * track of this and automatically send key events to the appropriate
	 * clients without additional work on your part.
	 */
	if (keyboard != NULL) {
		wlr_seat_keyboard_notify_enter(seat, surface,
			keyboard->keycodes, keyboard->num_keycodes, &keyboard->modifiers);
	}
	if (prev_toplevel != NULL) {
		stonefly_foreign_toplevel_on_state_change(prev_toplevel);
	}
	stonefly_foreign_toplevel_on_state_change(toplevel);
}

struct stonefly_toplevel *stonefly_focused_toplevel(struct stonefly_server *server) {
	struct wlr_surface *focused = server->seat->keyboard_state.focused_surface;
	if (focused == NULL) {
		return NULL;
	}
	struct wlr_surface *root = wlr_surface_get_root_surface(focused);
	struct stonefly_toplevel *toplevel;
	wl_list_for_each(toplevel, &server->toplevels, link) {
		if (toplevel->xdg_toplevel->base->surface == root) {
			return toplevel;
		}
	}
	return NULL;
}

void stonefly_focus_top(struct stonefly_server *server) {
	if (stonefly_session_locked(server)) {
		return;
	}
	struct stonefly_toplevel *toplevel;
	wl_list_for_each(toplevel, &server->toplevels, link) {
		if (toplevel_focusable(toplevel)
				&& !stonefly_toplevel_is_scratchpad(toplevel)) {
			stonefly_focus_toplevel(toplevel);
			return;
		}
	}
	/* Fall back to a visible scratchpad if nothing else remains. */
	wl_list_for_each(toplevel, &server->toplevels, link) {
		if (toplevel_focusable(toplevel)) {
			stonefly_focus_toplevel(toplevel);
			return;
		}
	}
	wlr_seat_keyboard_notify_clear_focus(server->seat);
}

void stonefly_toplevel_minimize(struct stonefly_toplevel *toplevel) {
	if (toplevel->minimized) {
		return;
	}
	/* Mid-show reverses into a hide (peeble beginSlide). */
	if (stonefly_anim_start_hide(toplevel, minimize_now)) {
		return;
	}
	minimize_now(toplevel);
}

static void stonefly_minimize_focused(struct stonefly_server *server) {
	struct stonefly_toplevel *toplevel = stonefly_focused_toplevel(server);
	if (toplevel != NULL) {
		stonefly_toplevel_minimize(toplevel);
	}
}

static void stonefly_unminimize_most_recent(struct stonefly_server *server) {
	if (wl_list_empty(&server->minimized)) {
		return;
	}
	struct stonefly_toplevel *toplevel =
		wl_container_of(server->minimized.next, toplevel, minimize_link);
	stonefly_focus_toplevel(toplevel);
}

static void close_after_hide(struct stonefly_toplevel *toplevel) {
	wlr_xdg_toplevel_send_close(toplevel->xdg_toplevel);
}

static void stonefly_close_focused(struct stonefly_server *server) {
	struct stonefly_toplevel *toplevel = stonefly_focused_toplevel(server);
	if (toplevel == NULL) {
		return;
	}
	if (stonefly_anim_start_hide(toplevel, close_after_hide)) {
		return;
	}
	wlr_xdg_toplevel_send_close(toplevel->xdg_toplevel);
}

static void stonefly_spawn_cmd(const char *cmd) {
	if (fork() == 0) {
		execl("/bin/sh", "/bin/sh", "-c", cmd, (void *)NULL);
		_exit(1);
	}
}

static bool focus_cycle_eligible(struct stonefly_toplevel *t,
		bool include_minimized) {
	if (stonefly_toplevel_is_scratchpad(t) || t->anim_hide) {
		return false;
	}
	return include_minimized || !t->minimized;
}

static void stonefly_focus_cycle(struct stonefly_server *server, bool backward,
		bool include_minimized) {
	if (wl_list_empty(&server->toplevels)) {
		return;
	}
	struct stonefly_toplevel *current = stonefly_focused_toplevel(server);
	struct stonefly_toplevel *candidate = NULL;

	if (current == NULL) {
		wl_list_for_each(candidate, &server->toplevels, link) {
			if (focus_cycle_eligible(candidate, include_minimized)) {
				stonefly_focus_toplevel(candidate);
				return;
			}
		}
		return;
	}

	struct stonefly_toplevel *t = current;
	do {
		if (backward) {
			t = wl_container_of(t->link.next, t, link);
			if (&t->link == &server->toplevels) {
				t = wl_container_of(server->toplevels.next, t, link);
			}
		} else {
			/* Top of list is most recently focused; next is previous link. */
			t = wl_container_of(t->link.prev, t, link);
			if (&t->link == &server->toplevels) {
				t = wl_container_of(server->toplevels.prev, t, link);
			}
		}
		if (focus_cycle_eligible(t, include_minimized)) {
			stonefly_focus_toplevel(t);
			return;
		}
	} while (t != current);
}

void stonefly_run_action(struct stonefly_server *server,
		enum stonefly_action action, const char *cmd) {
	struct stonefly_toplevel *focused = stonefly_focused_toplevel(server);
	switch (action) {
	case STONEFLY_ACTION_NONE:
		break;
	case STONEFLY_ACTION_QUIT:
		wl_display_terminate(server->wl_display);
		break;
	case STONEFLY_ACTION_CLOSE:
		stonefly_close_focused(server);
		break;
	case STONEFLY_ACTION_FOCUS_NEXT:
		stonefly_focus_cycle(server, false, false);
		break;
	case STONEFLY_ACTION_FOCUS_PREV:
		stonefly_focus_cycle(server, true, false);
		break;
	case STONEFLY_ACTION_FOCUS_NEXT_ALL:
		stonefly_focus_cycle(server, false, true);
		break;
	case STONEFLY_ACTION_FOCUS_PREV_ALL:
		stonefly_focus_cycle(server, true, true);
		break;
	case STONEFLY_ACTION_TOGGLE_MAX_EDGES:
		if (focused != NULL) {
			stonefly_toplevel_toggle_max_edges(focused);
		}
		break;
	case STONEFLY_ACTION_TOGGLE_MAX_GAPS:
		if (focused != NULL) {
			stonefly_toplevel_toggle_max_gaps(focused);
		}
		break;
	case STONEFLY_ACTION_MINIMIZE:
		stonefly_minimize_focused(server);
		break;
	case STONEFLY_ACTION_UNMINIMIZE:
		stonefly_unminimize_most_recent(server);
		break;
	case STONEFLY_ACTION_SNAP_LEFT:
		if (focused != NULL) {
			stonefly_toplevel_snap(focused, STONEFLY_SNAP_LEFT);
		}
		break;
	case STONEFLY_ACTION_SNAP_RIGHT:
		if (focused != NULL) {
			stonefly_toplevel_snap(focused, STONEFLY_SNAP_RIGHT);
		}
		break;
	case STONEFLY_ACTION_SNAP_TOP:
		if (focused != NULL) {
			stonefly_toplevel_snap(focused, STONEFLY_SNAP_TOP);
		}
		break;
	case STONEFLY_ACTION_SNAP_BOTTOM:
		if (focused != NULL) {
			stonefly_toplevel_snap(focused, STONEFLY_SNAP_BOTTOM);
		}
		break;
	case STONEFLY_ACTION_UNSNAP:
		if (focused != NULL) {
			stonefly_toplevel_unsnap(focused);
		}
		break;
	case STONEFLY_ACTION_SPAWN_FOOT:
		stonefly_spawn_cmd(STONEFLY_CMD_FOOT);
		break;
	case STONEFLY_ACTION_SPAWN_NAUTILUS:
		stonefly_spawn_cmd(STONEFLY_CMD_NAUTILUS);
		break;
	case STONEFLY_ACTION_SPAWN_GHOSTTY:
		stonefly_spawn_cmd(STONEFLY_CMD_GHOSTTY);
		break;
	case STONEFLY_ACTION_SPAWN_GHOSTTY_QT:
		stonefly_spawn_cmd(STONEFLY_CMD_GHOSTTY_QT);
		break;
	case STONEFLY_ACTION_SPAWN_EDITOR:
		stonefly_spawn_cmd(STONEFLY_CMD_EDITOR);
		break;
	case STONEFLY_ACTION_SPAWN_LAUNCHER:
		stonefly_spawn_cmd(STONEFLY_CMD_LAUNCHER);
		break;
	case STONEFLY_ACTION_EXEC:
		if (cmd != NULL && cmd[0] != '\0') {
			SF_LOGD("exec", sf_str("cmd", cmd));
			stonefly_spawn_cmd(cmd);
		} else {
			SF_LOGW("exec action with empty cmd");
		}
		break;
	case STONEFLY_ACTION_SCRATCH_TOGGLE_TERM:
		stonefly_scratchpad_toggle(server, "term");
		break;
	case STONEFLY_ACTION_SCRATCH_CLAIM_TERM:
		stonefly_scratchpad_claim_focused(server, "term");
		break;
	}
}

