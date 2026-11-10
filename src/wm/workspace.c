#include "wm/workspace.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_ext_workspace_v1.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <scenefx/types/wlr_scene.h>
#include <wayland-protocols/ext-workspace-v1-enum.h>

#include "anim/anim.h"
#include "config/config.h"
#include "decor/decor.h"
#include "log/log.h"
#include "output/output.h"
#include "seat/seat.h"
#include "server/server.h"
#include "ssd/ssd.h"
#include "util/math.h"
#include "util/time.h"
#include "wm/scratchpad.h"
#include "wm/wm.h"
#include "toplevel/toplevel.h"

static uint32_t workspace_caps(void) {
	return EXT_WORKSPACE_HANDLE_V1_WORKSPACE_CAPABILITIES_ACTIVATE;
}

static uint32_t group_caps(void) {
	return EXT_WORKSPACE_GROUP_HANDLE_V1_GROUP_CAPABILITIES_CREATE_WORKSPACE;
}

static bool workspace_has_members(struct sf_workspace *ws) {
	struct sf_toplevel *t;
	wl_list_for_each(t, &ws->output->server->toplevels, link) {
		if (t->workspace == ws && !sf_toplevel_is_scratchpad(t)) {
			return true;
		}
	}
	/* Minimized windows stay on the strip and keep the workspace alive. */
	wl_list_for_each(t, &ws->output->server->minimized, minimize_link) {
		if (t->workspace == ws && !sf_toplevel_is_scratchpad(t)) {
			return true;
		}
	}
	return false;
}

static void workspace_set_ext_coords(struct sf_workspace *ws) {
	if (ws->ext == NULL) {
		return;
	}
	uint32_t coords[2] = {0, (uint32_t)ws->index};
	wlr_ext_workspace_handle_v1_set_coordinates(ws->ext, coords, 2);
}

static void workspace_sync_ext(struct sf_workspace *ws) {
	if (ws->ext == NULL) {
		return;
	}
	wlr_ext_workspace_handle_v1_set_name(ws->ext, ws->name);
	workspace_set_ext_coords(ws);
	wlr_ext_workspace_handle_v1_set_active(ws->ext, ws->output->active == ws);
}

static void workspace_renumber(struct sf_output *output) {
	int i = 0;
	struct sf_workspace *ws;
	wl_list_for_each(ws, &output->workspaces, link) {
		/* NOLINTNEXTLINE(clang-analyzer-unix.Malloc) -- destroy unlinks before free */
		ws->index = i++;
		snprintf(ws->name, sizeof(ws->name), "%d", ws->index + 1);
		workspace_sync_ext(ws);
	}
}

static void workspace_tree_reset_pos(struct sf_workspace *ws) {
	if (ws != NULL && ws->tree != NULL) {
		wlr_scene_node_set_position(&ws->tree->node, 0, 0);
	}
}

static void workspace_update_visibility(struct sf_output *output) {
	struct sf_workspace *ws;
	wl_list_for_each(ws, &output->workspaces, link) {
		bool on = (ws == output->active);
		if (output->ws_anim.active &&
		    (ws == output->ws_anim.from || ws == output->ws_anim.to)) {
			on = true;
		}
		if (ws->tree != NULL) {
			wlr_scene_node_set_enabled(&ws->tree->node, on);
			if (!on) {
				wlr_scene_node_set_position(&ws->tree->node, 0, 0);
			}
		}
		if (ws->ext != NULL) {
			wlr_ext_workspace_handle_v1_set_active(ws->ext, ws == output->active);
		}
	}
}

static void workspace_anim_finish(struct sf_output *output) {
	if (output == NULL || !output->ws_anim.active) {
		return;
	}
	struct sf_workspace *from = output->ws_anim.from;
	struct sf_workspace *to = output->ws_anim.to;
	output->ws_anim.active = false;
	output->ws_anim.from = NULL;
	output->ws_anim.to = NULL;
	workspace_tree_reset_pos(from);
	workspace_tree_reset_pos(to);
	workspace_update_visibility(output);
}

static void workspace_anim_apply(struct sf_output *output, double p) {
	int from_y = sf_lerp_i(output->ws_anim.from_y0,
			       output->ws_anim.from_y0 + output->ws_anim.travel, p);
	int to_y =
		sf_lerp_i(output->ws_anim.to_y0, output->ws_anim.to_y0 + output->ws_anim.travel, p);
	if (output->ws_anim.from != NULL && output->ws_anim.from->tree != NULL) {
		wlr_scene_node_set_position(&output->ws_anim.from->tree->node, 0, from_y);
	}
	if (output->ws_anim.to != NULL && output->ws_anim.to->tree != NULL) {
		wlr_scene_node_set_position(&output->ws_anim.to->tree->node, 0, to_y);
	}
}

static void workspace_switch_instant(struct sf_output *output, struct sf_workspace *ws) {
	workspace_anim_finish(output);
	output->active = ws;
	workspace_update_visibility(output);
	sf_workspace_apply_layout(ws);
	sf_focus_top(output->server);
	sf_pointer_follow_focus(output->server);
}

static void workspace_anim_start(struct sf_output *output, struct sf_workspace *to) {
	if (to == NULL) {
		return;
	}
	struct sf_workspace *from = output->active;
	if (from == NULL || from == to) {
		workspace_switch_instant(output, to);
		return;
	}

	/* Interrupt in-flight slide: land on the current target, then leave it. */
	if (output->ws_anim.active) {
		workspace_anim_finish(output);
		from = output->active;
		if (from == to) {
			return;
		}
	}

	int H = output->full.height;
	if (H <= 0 && output->wlr_output != NULL) {
		H = output->wlr_output->height;
	}
	if (H <= 0) {
		workspace_switch_instant(output, to);
		return;
	}

	/* Next (higher index): content slides up — old exits top, new enters from bottom.
	 * Prev: opposite. Matches natural 3-finger swipe (finger up → next). */
	int travel = (to->index >= from->index) ? -H : H;

	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	int64_t dur_ns = (int64_t)sf_cfg(output->server)->slide_duration_ms * 1000000LL;

	output->ws_anim.active = true;
	output->ws_anim.from = from;
	output->ws_anim.to = to;
	output->ws_anim.from_y0 = 0;
	output->ws_anim.to_y0 = -travel; /* enter from the opposite edge */
	output->ws_anim.travel = travel;
	output->ws_anim.start = now;
	output->ws_anim.dur_ns = dur_ns;

	output->active = to;
	workspace_update_visibility(output);
	workspace_anim_apply(output, 0.0);
	sf_workspace_apply_layout(to);
	sf_focus_top(output->server);
	sf_pointer_follow_focus(output->server);
	sf_anim_schedule(output->server);
}

static struct sf_workspace *workspace_create(struct sf_output *output) {
	struct sf_server *server = output->server;
	struct sf_workspace *ws = calloc(1, sizeof(*ws));
	if (ws == NULL) {
		return NULL;
	}
	ws->output = output;
	ws->layout = SF_LAYOUT_UNSET;
	ws->tree = wlr_scene_tree_create(server->layers.toplevel);
	if (ws->tree == NULL) {
		free(ws);
		return NULL;
	}
	wlr_scene_node_set_enabled(&ws->tree->node, false);

	if (server->ext_workspace_mgr != NULL && output->ext_group != NULL) {
		snprintf(ws->ext_id, sizeof(ws->ext_id), "%s:%u",
			 output->wlr_output->name != NULL ? output->wlr_output->name : "out",
			 ++server->next_workspace_ext_id);
		ws->ext = wlr_ext_workspace_handle_v1_create(server->ext_workspace_mgr, ws->ext_id,
							     workspace_caps());
		if (ws->ext != NULL) {
			ws->ext->data = ws;
			wlr_ext_workspace_handle_v1_set_group(ws->ext, output->ext_group);
		}
	}

	wl_list_insert(output->workspaces.prev, &ws->link);
	workspace_renumber(output);
	return ws;
}

/* Drop scene chrome before the workspace tree is destroyed. Nested-session
 * output teardown can free ws->tree (and its children) while sf_toplevel
 * still lives; border hit-tests then UAF on scene_tree. */
static void workspace_detach_toplevel_scene(struct sf_toplevel *toplevel) {
	if (toplevel == NULL) {
		return;
	}
	if (toplevel->server != NULL && toplevel->server->grabbed_toplevel == toplevel) {
		sf_reset_cursor_mode(toplevel->server);
	}
	sf_anim_cancel(toplevel, false);
	sf_ssd_destroy(toplevel);
	sf_decor_finish_toplevel(toplevel);
}

static void workspace_clear_members(struct sf_workspace *ws) {
	if (ws == NULL || ws->output == NULL || ws->output->server == NULL) {
		return;
	}
	struct sf_server *server = ws->output->server;
	struct sf_toplevel *t;
	wl_list_for_each(t, &server->toplevels, link) {
		if (t->workspace == ws) {
			workspace_detach_toplevel_scene(t);
			t->workspace = NULL;
		}
	}
	wl_list_for_each(t, &server->minimized, minimize_link) {
		if (t->workspace == ws) {
			workspace_detach_toplevel_scene(t);
			t->workspace = NULL;
		}
	}
}

static void workspace_destroy(struct sf_workspace *ws) {
	if (ws == NULL) {
		return;
	}
	workspace_clear_members(ws);
	if (ws->ext != NULL) {
		wlr_ext_workspace_handle_v1_destroy(ws->ext);
		ws->ext = NULL;
	}
	if (ws->tree != NULL) {
		wlr_scene_node_destroy(&ws->tree->node);
		ws->tree = NULL;
	}
	wl_list_remove(&ws->link);
	free(ws);
}

enum sf_layout sf_workspace_layout(const struct sf_workspace *ws) {
	if (ws == NULL) {
		return SF_LAYOUT_STACKING;
	}
	if (ws->layout != SF_LAYOUT_UNSET) {
		return ws->layout;
	}
	if (ws->output != NULL && ws->output->layout != SF_LAYOUT_UNSET) {
		return ws->output->layout;
	}
	if (ws->output == NULL) {
		return SF_LAYOUT_STACKING;
	}
	return ws->output->server->config.layout;
}

void sf_workspace_apply_layout(struct sf_workspace *ws) {
	if (ws == NULL) {
		return;
	}
	enum sf_layout layout = sf_workspace_layout(ws);
	/* STACKING: no rearrange. COLUMNS/TILE land here later. */
	(void)layout;
}

struct sf_output *sf_output_at_cursor(struct sf_server *server) {
	struct wlr_output *wlr_output = wlr_output_layout_output_at(
		server->output_layout, server->cursor->x, server->cursor->y);
	if (wlr_output == NULL || wlr_output->data == NULL) {
		if (wl_list_empty(&server->outputs)) {
			return NULL;
		}
		struct sf_output *first = wl_container_of(server->outputs.next, first, link);
		return first;
	}
	return wlr_output->data;
}

struct sf_output *sf_output_for_toplevel(struct sf_toplevel *toplevel) {
	if (toplevel == NULL) {
		return NULL;
	}
	if (toplevel->workspace != NULL) {
		return toplevel->workspace->output;
	}
	struct wlr_box geom;
	sf_toplevel_layout_geom(toplevel, &geom);
	struct wlr_output *wlr_output =
		wlr_output_layout_output_at(toplevel->server->output_layout,
					    geom.x + geom.width / 2.0, geom.y + geom.height / 2.0);
	if (wlr_output != NULL && wlr_output->data != NULL) {
		return wlr_output->data;
	}
	return sf_output_at_cursor(toplevel->server);
}

struct sf_workspace *sf_workspace_active(struct sf_output *output) {
	return output != NULL ? output->active : NULL;
}

bool sf_toplevel_workspace_visible(const struct sf_toplevel *toplevel) {
	if (toplevel == NULL) {
		return false;
	}
	if (sf_toplevel_is_scratchpad(toplevel)) {
		return true;
	}
	if (toplevel->workspace == NULL) {
		return true;
	}
	return toplevel->workspace->output->active == toplevel->workspace;
}

void sf_workspace_assign(struct sf_toplevel *toplevel, struct sf_workspace *ws) {
	if (toplevel == NULL || ws == NULL) {
		return;
	}
	if (sf_toplevel_is_scratchpad(toplevel)) {
		sf_workspace_detach_scratchpad(toplevel);
		return;
	}
	struct sf_output *old_output =
		toplevel->workspace != NULL ? toplevel->workspace->output : NULL;
	toplevel->workspace = ws;
	/* Fullscreen stays on layers.fullscreen above the bar; sync picks
	 * ws->tree only when not fullscreen. */
	sf_toplevel_sync_scene_layer(toplevel);
	if (old_output != NULL && old_output != ws->output) {
		sf_workspace_compact(old_output);
	}
	sf_workspace_compact(ws->output);
}

void sf_workspace_detach_scratchpad(struct sf_toplevel *toplevel) {
	if (toplevel == NULL) {
		return;
	}
	struct sf_output *output = toplevel->workspace != NULL ? toplevel->workspace->output : NULL;
	toplevel->workspace = NULL;
	sf_toplevel_sync_scene_layer(toplevel);
	if (output != NULL) {
		sf_workspace_compact(output);
	}
}

void sf_workspace_compact(struct sf_output *output) {
	if (output == NULL) {
		return;
	}

	struct sf_workspace *ws, *tmp;

	/* Keep occupied workspaces, the current active (may be empty), and the
	 * outgoing slide partner until the strip animation finishes. */
	wl_list_for_each_safe(ws, tmp, &output->workspaces, link) {
		if (workspace_has_members(ws) || ws == output->active) {
			continue;
		}
		if (output->ws_anim.active && ws == output->ws_anim.from) {
			continue;
		}
		workspace_destroy(ws);
	}

	if (wl_list_empty(&output->workspaces)) {
		output->active = workspace_create(output);
	}

	workspace_renumber(output);

	int last_occ = -1;
	wl_list_for_each(ws, &output->workspaces, link) {
		/* NOLINTNEXTLINE(clang-analyzer-unix.Malloc) -- destroy unlinks before free */
		if (workspace_has_members(ws)) {
			last_occ = ws->index;
		}
	}
	int want_trailing = last_occ + 1;
	if (want_trailing < 0) {
		want_trailing = 0;
	}

	/* If active empty sits past the natural trailing slot, pull it back. */
	if (output->active != NULL && !workspace_has_members(output->active) &&
	    output->active->index > want_trailing) {
		struct sf_workspace *target = NULL;
		wl_list_for_each(ws, &output->workspaces, link) {
			/* NOLINTNEXTLINE(clang-analyzer-unix.Malloc) -- destroy unlinks before free */
			if (ws->index == want_trailing) {
				target = ws;
				break;
			}
		}
		if (target != NULL) {
			output->active = target;
		}
		want_trailing = output->active != NULL ? output->active->index : want_trailing;
	}

	/* Grow until the trailing empty index exists. */
	int count = 0;
	wl_list_for_each(ws, &output->workspaces, link) {
		count++;
	}
	while (count <= want_trailing) {
		if (workspace_create(output) == NULL) {
			break;
		}
		count++;
	}

	/* Drop empties after the single trailing slot. */
	wl_list_for_each_safe(ws, tmp, &output->workspaces, link) {
		if (ws->index > want_trailing && !workspace_has_members(ws) &&
		    ws != output->active &&
		    !(output->ws_anim.active && ws == output->ws_anim.from)) {
			workspace_destroy(ws);
		}
	}

	if (output->active == NULL) {
		struct sf_workspace *last = NULL;
		wl_list_for_each(ws, &output->workspaces, link) {
			last = ws;
			/* NOLINTNEXTLINE(clang-analyzer-unix.Malloc) -- destroy unlinks before free */
			if (workspace_has_members(ws)) {
				output->active = ws;
			}
		}
		if (output->active == NULL) {
			output->active = last;
		}
	}

	workspace_renumber(output);
	workspace_update_visibility(output);
	if (output->active != NULL) {
		sf_workspace_apply_layout(output->active);
	}
}

void sf_workspace_switch(struct sf_output *output, struct sf_workspace *ws) {
	if (output == NULL || ws == NULL || ws->output != output) {
		return;
	}
	if (output->active == ws) {
		return;
	}
	if (sf_cfg(output->server)->slide_duration_ms <= 0) {
		workspace_switch_instant(output, ws);
		return;
	}
	workspace_anim_start(output, ws);
}

void sf_workspace_switch_offset(struct sf_output *output, int delta) {
	if (output == NULL || output->active == NULL || delta == 0) {
		return;
	}
	int count = 0;
	struct sf_workspace *ws;
	wl_list_for_each(ws, &output->workspaces, link) {
		count++;
	}
	if (count <= 0) {
		return;
	}
	int idx = output->active->index + delta;
	if (idx < 0) {
		idx = 0;
	}
	if (idx >= count) {
		/* Moving past the end: ensure trailing empty exists, land there. */
		sf_workspace_compact(output);
		idx = 0;
		wl_list_for_each(ws, &output->workspaces, link) {
			idx = ws->index;
		}
	}
	struct sf_workspace *target = NULL;
	wl_list_for_each(ws, &output->workspaces, link) {
		if (ws->index == idx) {
			target = ws;
			break;
		}
	}
	if (target != NULL) {
		sf_workspace_switch(output, target);
	}
}

bool sf_workspace_switch_index(struct sf_output *output, int one_based) {
	if (output == NULL || one_based < 1) {
		return false;
	}
	struct sf_workspace *ws;
	wl_list_for_each(ws, &output->workspaces, link) {
		if (ws->index + 1 == one_based) {
			sf_workspace_switch(output, ws);
			return true;
		}
	}
	return false;
}

void sf_workspace_move_toplevel(struct sf_toplevel *toplevel, struct sf_workspace *ws,
				bool follow) {
	if (toplevel == NULL || ws == NULL || sf_toplevel_is_scratchpad(toplevel)) {
		return;
	}
	struct sf_output *output = ws->output;
	sf_workspace_assign(toplevel, ws);
	if (follow) {
		sf_workspace_switch(output, ws);
		sf_focus_toplevel(toplevel);
		/* Switch already followed focus_top; re-center on the moved window. */
		sf_pointer_follow_focus(output->server);
	}
}

void sf_workspace_move_offset(struct sf_toplevel *toplevel, int delta, bool follow) {
	if (toplevel == NULL || delta == 0) {
		return;
	}
	struct sf_output *output = sf_output_for_toplevel(toplevel);
	if (output == NULL || output->active == NULL) {
		return;
	}
	if (toplevel->workspace == NULL) {
		sf_workspace_assign(toplevel, output->active);
	}
	if (toplevel->workspace == NULL) {
		return;
	}
	int idx = toplevel->workspace->index + delta;
	if (idx < 0) {
		return;
	}
	/* Ensure destination exists (append empties). */
	int count = 0;
	struct sf_workspace *ws;
	wl_list_for_each(ws, &output->workspaces, link) {
		count++;
	}
	while (idx >= count) {
		if (workspace_create(output) == NULL) {
			return;
		}
		count++;
	}
	struct sf_workspace *target = NULL;
	wl_list_for_each(ws, &output->workspaces, link) {
		if (ws->index == idx) {
			target = ws;
			break;
		}
	}
	if (target == NULL) {
		return;
	}
	sf_workspace_move_toplevel(toplevel, target, follow);
	sf_workspace_compact(output);
}

bool sf_workspace_move_index(struct sf_toplevel *toplevel, int one_based, bool follow) {
	if (toplevel == NULL || one_based < 1) {
		return false;
	}
	struct sf_output *output = sf_output_for_toplevel(toplevel);
	if (output == NULL) {
		return false;
	}
	struct sf_workspace *ws;
	wl_list_for_each(ws, &output->workspaces, link) {
		if (ws->index + 1 == one_based) {
			sf_workspace_move_toplevel(toplevel, ws, follow);
			return true;
		}
	}
	return false;
}

void sf_workspace_reveal_toplevel(struct sf_toplevel *toplevel) {
	if (toplevel == NULL) {
		return;
	}
	if (toplevel->workspace != NULL &&
	    toplevel->workspace->output->active != toplevel->workspace) {
		sf_workspace_switch(toplevel->workspace->output, toplevel->workspace);
	}
}

void sf_workspace_output_init(struct sf_output *output) {
	wl_list_init(&output->workspaces);
	output->active = NULL;
	output->layout = SF_LAYOUT_UNSET;
	output->ext_group = NULL;

	struct sf_server *server = output->server;
	if (server->ext_workspace_mgr != NULL) {
		output->ext_group = wlr_ext_workspace_group_handle_v1_create(
			server->ext_workspace_mgr, group_caps());
		if (output->ext_group != NULL) {
			output->ext_group->data = output;
			wlr_ext_workspace_group_handle_v1_output_enter(output->ext_group,
								       output->wlr_output);
		}
	}

	output->active = workspace_create(output);
	if (output->active != NULL) {
		wlr_scene_node_set_enabled(&output->active->tree->node, true);
		workspace_sync_ext(output->active);
	}
}

void sf_workspace_output_finish(struct sf_output *output) {
	workspace_anim_finish(output);
	struct sf_workspace *ws, *tmp;
	/* Detach member scene trees before destroying each strip node — clients
	 * may still emit cursor-shape requests until their xdg objects die. */
	wl_list_for_each_safe(ws, tmp, &output->workspaces, link) {
		workspace_destroy(ws);
	}
	output->active = NULL;
	if (output->ext_group != NULL) {
		wlr_ext_workspace_group_handle_v1_destroy(output->ext_group);
		output->ext_group = NULL;
	}
}

static void ext_workspace_commit(struct wl_listener *listener, void *data) {
	struct sf_server *server = wl_container_of(listener, server, ext_workspace_commit);
	struct wlr_ext_workspace_v1_commit_event *event = data;
	struct wlr_ext_workspace_v1_request *req;
	wl_list_for_each(req, event->requests, link) {
		switch (req->type) {
		case WLR_EXT_WORKSPACE_V1_REQUEST_ACTIVATE:
			if (req->activate.workspace != NULL &&
			    req->activate.workspace->data != NULL) {
				struct sf_workspace *ws = req->activate.workspace->data;
				sf_workspace_switch(ws->output, ws);
			}
			break;
		case WLR_EXT_WORKSPACE_V1_REQUEST_CREATE_WORKSPACE:
			if (req->create_workspace.group != NULL &&
			    req->create_workspace.group->data != NULL) {
				struct sf_output *output = req->create_workspace.group->data;
				/* Dynamic strip already keeps a trailing empty — switch to it. */
				sf_workspace_compact(output);
				struct sf_workspace *last = NULL;
				struct sf_workspace *ws;
				wl_list_for_each(ws, &output->workspaces, link) {
					last = ws;
				}
				if (last != NULL) {
					sf_workspace_switch(output, last);
				}
			}
			break;
		case WLR_EXT_WORKSPACE_V1_REQUEST_DEACTIVATE:
		case WLR_EXT_WORKSPACE_V1_REQUEST_ASSIGN:
		case WLR_EXT_WORKSPACE_V1_REQUEST_REMOVE:
			/* Strip is compositor-owned; ignore. */
			break;
		}
	}
	(void)server;
}

void sf_workspace_anim_tick(struct sf_server *server, const struct timespec *now) {
	if (server == NULL || now == NULL) {
		return;
	}
	struct sf_output *output;
	wl_list_for_each(output, &server->outputs, link) {
		if (!output->ws_anim.active) {
			continue;
		}
		int64_t elapsed = sf_timespec_ns(now) - sf_timespec_ns(&output->ws_anim.start);
		double t = 1.0;
		if (output->ws_anim.dur_ns > 0) {
			t = (double)elapsed / (double)output->ws_anim.dur_ns;
		}
		double p = sf_ease_out_cubic(t);
		workspace_anim_apply(output, p);
		if (t >= 1.0) {
			workspace_anim_finish(output);
			/* Drop an emptied outgoing workspace now that it is off-strip. */
			sf_workspace_compact(output);
		}
	}
}

bool sf_workspace_anim_busy(struct sf_server *server) {
	if (server == NULL) {
		return false;
	}
	struct sf_output *output;
	wl_list_for_each(output, &server->outputs, link) {
		if (output->ws_anim.active) {
			return true;
		}
	}
	return false;
}

void sf_workspace_setup(struct sf_server *server) {
	server->next_workspace_ext_id = 0;
	server->ext_workspace_mgr = wlr_ext_workspace_manager_v1_create(server->wl_display, 1);
	if (server->ext_workspace_mgr == NULL) {
		SF_LOGW("ext-workspace-v1 unavailable");
		return;
	}
	server->ext_workspace_commit.notify = ext_workspace_commit;
	wl_signal_add(&server->ext_workspace_mgr->events.commit, &server->ext_workspace_commit);
}

void sf_workspace_finish(struct sf_server *server) {
	/* Destroy per-output workspace scene trees while the root scene is still
	 * alive. Otherwise server_finish's wlr_scene_node_destroy(root) frees them
	 * first, then backend output_destroy → workspace_output_finish double-frees
	 * (ASan SEGV in wl_list_remove on nested ipc exit). */
	struct sf_output *output;
	wl_list_for_each(output, &server->outputs, link) {
		sf_workspace_output_finish(output);
	}
	if (server->ext_workspace_mgr != NULL) {
		wl_list_remove(&server->ext_workspace_commit.link);
		/* Manager is destroyed with the display. */
		server->ext_workspace_mgr = NULL;
	}
}
