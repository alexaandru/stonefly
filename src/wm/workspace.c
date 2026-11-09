#include "wm/workspace.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_ext_workspace_v1.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <scenefx/types/wlr_scene.h>
#include <wayland-protocols/ext-workspace-v1-enum.h>

#include "config/config.h"
#include "log/log.h"
#include "output/output.h"
#include "server/server.h"
#include "wm/scratchpad.h"
#include "wm/wm.h"
#include "toplevel/toplevel.h"

static uint32_t workspace_caps(void) {
	return EXT_WORKSPACE_HANDLE_V1_WORKSPACE_CAPABILITIES_ACTIVATE;
}

static uint32_t group_caps(void) {
	return EXT_WORKSPACE_GROUP_HANDLE_V1_GROUP_CAPABILITIES_CREATE_WORKSPACE;
}

static bool workspace_has_members(struct stonefly_workspace *ws) {
	struct stonefly_toplevel *t;
	wl_list_for_each(t, &ws->output->server->toplevels, link) {
		if (t->workspace == ws && !stonefly_toplevel_is_scratchpad(t)) {
			return true;
		}
	}
	/* Minimized windows stay on the strip and keep the workspace alive. */
	wl_list_for_each(t, &ws->output->server->minimized, minimize_link) {
		if (t->workspace == ws && !stonefly_toplevel_is_scratchpad(t)) {
			return true;
		}
	}
	return false;
}

static void workspace_set_ext_coords(struct stonefly_workspace *ws) {
	if (ws->ext == NULL) {
		return;
	}
	uint32_t coords[2] = {0, (uint32_t)ws->index};
	wlr_ext_workspace_handle_v1_set_coordinates(ws->ext, coords, 2);
}

static void workspace_sync_ext(struct stonefly_workspace *ws) {
	if (ws->ext == NULL) {
		return;
	}
	wlr_ext_workspace_handle_v1_set_name(ws->ext, ws->name);
	workspace_set_ext_coords(ws);
	wlr_ext_workspace_handle_v1_set_active(ws->ext, ws->output->active == ws);
}

static void workspace_renumber(struct stonefly_output *output) {
	int i = 0;
	struct stonefly_workspace *ws;
	wl_list_for_each(ws, &output->workspaces, link) {
		/* NOLINTNEXTLINE(clang-analyzer-unix.Malloc) -- destroy unlinks before free */
		ws->index = i++;
		snprintf(ws->name, sizeof(ws->name), "%d", ws->index + 1);
		workspace_sync_ext(ws);
	}
}

static void workspace_update_visibility(struct stonefly_output *output) {
	struct stonefly_workspace *ws;
	wl_list_for_each(ws, &output->workspaces, link) {
		bool on = (ws == output->active);
		if (ws->tree != NULL) {
			wlr_scene_node_set_enabled(&ws->tree->node, on);
		}
		if (ws->ext != NULL) {
			wlr_ext_workspace_handle_v1_set_active(ws->ext, on);
		}
	}
}

static struct stonefly_workspace *workspace_create(struct stonefly_output *output) {
	struct stonefly_server *server = output->server;
	struct stonefly_workspace *ws = calloc(1, sizeof(*ws));
	if (ws == NULL) {
		return NULL;
	}
	ws->output = output;
	ws->layout = STONEFLY_LAYOUT_UNSET;
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

static void workspace_destroy(struct stonefly_workspace *ws) {
	if (ws == NULL) {
		return;
	}
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

enum stonefly_layout stonefly_layout_parse(const char *s) {
	if (s == NULL || s[0] == '\0' || strcmp(s, "stacking") == 0) {
		return STONEFLY_LAYOUT_STACKING;
	}
	if (strcmp(s, "columns") == 0) {
		return STONEFLY_LAYOUT_COLUMNS;
	}
	if (strcmp(s, "tile") == 0) {
		return STONEFLY_LAYOUT_TILE;
	}
	if (strcmp(s, "unset") == 0 || strcmp(s, "inherit") == 0) {
		return STONEFLY_LAYOUT_UNSET;
	}
	return STONEFLY_LAYOUT_STACKING;
}

const char *stonefly_layout_name(enum stonefly_layout layout) {
	switch (layout) {
	case STONEFLY_LAYOUT_UNSET:
		return "inherit";
	case STONEFLY_LAYOUT_STACKING:
		return "stacking";
	case STONEFLY_LAYOUT_COLUMNS:
		return "columns";
	case STONEFLY_LAYOUT_TILE:
		return "tile";
	}
	return "stacking";
}

enum stonefly_layout stonefly_workspace_layout(const struct stonefly_workspace *ws) {
	if (ws == NULL) {
		return STONEFLY_LAYOUT_STACKING;
	}
	if (ws->layout != STONEFLY_LAYOUT_UNSET) {
		return ws->layout;
	}
	if (ws->output != NULL && ws->output->layout != STONEFLY_LAYOUT_UNSET) {
		return ws->output->layout;
	}
	if (ws->output == NULL) {
		return STONEFLY_LAYOUT_STACKING;
	}
	return ws->output->server->config.layout;
}

void stonefly_workspace_apply_layout(struct stonefly_workspace *ws) {
	if (ws == NULL) {
		return;
	}
	enum stonefly_layout layout = stonefly_workspace_layout(ws);
	/* STACKING: no rearrange. COLUMNS/TILE land here later. */
	(void)layout;
}

struct stonefly_output *stonefly_output_at_cursor(struct stonefly_server *server) {
	struct wlr_output *wlr_output = wlr_output_layout_output_at(
		server->output_layout, server->cursor->x, server->cursor->y);
	if (wlr_output == NULL || wlr_output->data == NULL) {
		if (wl_list_empty(&server->outputs)) {
			return NULL;
		}
		struct stonefly_output *first = wl_container_of(server->outputs.next, first, link);
		return first;
	}
	return wlr_output->data;
}

struct stonefly_output *stonefly_output_for_toplevel(struct stonefly_toplevel *toplevel) {
	if (toplevel == NULL) {
		return NULL;
	}
	if (toplevel->workspace != NULL) {
		return toplevel->workspace->output;
	}
	struct wlr_box geom;
	stonefly_toplevel_layout_geom(toplevel, &geom);
	struct wlr_output *wlr_output =
		wlr_output_layout_output_at(toplevel->server->output_layout,
					    geom.x + geom.width / 2.0, geom.y + geom.height / 2.0);
	if (wlr_output != NULL && wlr_output->data != NULL) {
		return wlr_output->data;
	}
	return stonefly_output_at_cursor(toplevel->server);
}

struct stonefly_workspace *stonefly_workspace_active(struct stonefly_output *output) {
	return output != NULL ? output->active : NULL;
}

bool stonefly_toplevel_workspace_visible(const struct stonefly_toplevel *toplevel) {
	if (toplevel == NULL) {
		return false;
	}
	if (stonefly_toplevel_is_scratchpad(toplevel)) {
		return true;
	}
	if (toplevel->workspace == NULL) {
		return true;
	}
	return toplevel->workspace->output->active == toplevel->workspace;
}

void stonefly_workspace_assign(struct stonefly_toplevel *toplevel, struct stonefly_workspace *ws) {
	if (toplevel == NULL || ws == NULL) {
		return;
	}
	if (stonefly_toplevel_is_scratchpad(toplevel)) {
		stonefly_workspace_detach_scratchpad(toplevel);
		return;
	}
	struct stonefly_output *old_output =
		toplevel->workspace != NULL ? toplevel->workspace->output : NULL;
	toplevel->workspace = ws;
	if (toplevel->scene_tree != NULL && ws->tree != NULL) {
		wlr_scene_node_reparent(&toplevel->scene_tree->node, ws->tree);
	}
	if (old_output != NULL && old_output != ws->output) {
		stonefly_workspace_compact(old_output);
	}
	stonefly_workspace_compact(ws->output);
}

void stonefly_workspace_detach_scratchpad(struct stonefly_toplevel *toplevel) {
	if (toplevel == NULL) {
		return;
	}
	struct stonefly_output *output =
		toplevel->workspace != NULL ? toplevel->workspace->output : NULL;
	toplevel->workspace = NULL;
	if (toplevel->scene_tree != NULL) {
		wlr_scene_node_reparent(&toplevel->scene_tree->node,
					toplevel->server->layers.toplevel);
	}
	if (output != NULL) {
		stonefly_workspace_compact(output);
	}
}

void stonefly_workspace_compact(struct stonefly_output *output) {
	if (output == NULL) {
		return;
	}

	struct stonefly_workspace *ws, *tmp;

	/* Keep occupied workspaces and the current active (may be empty). */
	wl_list_for_each_safe(ws, tmp, &output->workspaces, link) {
		if (workspace_has_members(ws) || ws == output->active) {
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
		struct stonefly_workspace *target = NULL;
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
		    ws != output->active) {
			workspace_destroy(ws);
		}
	}

	if (output->active == NULL) {
		struct stonefly_workspace *last = NULL;
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
		stonefly_workspace_apply_layout(output->active);
	}
}

void stonefly_workspace_switch(struct stonefly_output *output, struct stonefly_workspace *ws) {
	if (output == NULL || ws == NULL || ws->output != output) {
		return;
	}
	if (output->active == ws) {
		return;
	}
	output->active = ws;
	workspace_update_visibility(output);
	stonefly_workspace_apply_layout(ws);
	stonefly_focus_top(output->server);
}

void stonefly_workspace_switch_offset(struct stonefly_output *output, int delta) {
	if (output == NULL || output->active == NULL || delta == 0) {
		return;
	}
	int count = 0;
	struct stonefly_workspace *ws;
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
		stonefly_workspace_compact(output);
		idx = 0;
		wl_list_for_each(ws, &output->workspaces, link) {
			idx = ws->index;
		}
	}
	struct stonefly_workspace *target = NULL;
	wl_list_for_each(ws, &output->workspaces, link) {
		if (ws->index == idx) {
			target = ws;
			break;
		}
	}
	if (target != NULL) {
		stonefly_workspace_switch(output, target);
	}
}

bool stonefly_workspace_switch_index(struct stonefly_output *output, int one_based) {
	if (output == NULL || one_based < 1) {
		return false;
	}
	struct stonefly_workspace *ws;
	wl_list_for_each(ws, &output->workspaces, link) {
		if (ws->index + 1 == one_based) {
			stonefly_workspace_switch(output, ws);
			return true;
		}
	}
	return false;
}

void stonefly_workspace_move_toplevel(struct stonefly_toplevel *toplevel,
				      struct stonefly_workspace *ws, bool follow) {
	if (toplevel == NULL || ws == NULL || stonefly_toplevel_is_scratchpad(toplevel)) {
		return;
	}
	struct stonefly_output *output = ws->output;
	stonefly_workspace_assign(toplevel, ws);
	if (follow) {
		stonefly_workspace_switch(output, ws);
		stonefly_focus_toplevel(toplevel);
	}
}

void stonefly_workspace_move_offset(struct stonefly_toplevel *toplevel, int delta, bool follow) {
	if (toplevel == NULL || delta == 0) {
		return;
	}
	struct stonefly_output *output = stonefly_output_for_toplevel(toplevel);
	if (output == NULL || output->active == NULL) {
		return;
	}
	if (toplevel->workspace == NULL) {
		stonefly_workspace_assign(toplevel, output->active);
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
	struct stonefly_workspace *ws;
	wl_list_for_each(ws, &output->workspaces, link) {
		count++;
	}
	while (idx >= count) {
		if (workspace_create(output) == NULL) {
			return;
		}
		count++;
	}
	struct stonefly_workspace *target = NULL;
	wl_list_for_each(ws, &output->workspaces, link) {
		if (ws->index == idx) {
			target = ws;
			break;
		}
	}
	if (target == NULL) {
		return;
	}
	stonefly_workspace_move_toplevel(toplevel, target, follow);
	stonefly_workspace_compact(output);
}

bool stonefly_workspace_move_index(struct stonefly_toplevel *toplevel, int one_based, bool follow) {
	if (toplevel == NULL || one_based < 1) {
		return false;
	}
	struct stonefly_output *output = stonefly_output_for_toplevel(toplevel);
	if (output == NULL) {
		return false;
	}
	struct stonefly_workspace *ws;
	wl_list_for_each(ws, &output->workspaces, link) {
		if (ws->index + 1 == one_based) {
			stonefly_workspace_move_toplevel(toplevel, ws, follow);
			return true;
		}
	}
	return false;
}

void stonefly_workspace_reveal_toplevel(struct stonefly_toplevel *toplevel) {
	if (toplevel == NULL) {
		return;
	}
	if (toplevel->workspace != NULL &&
	    toplevel->workspace->output->active != toplevel->workspace) {
		stonefly_workspace_switch(toplevel->workspace->output, toplevel->workspace);
	}
}

void stonefly_workspace_output_init(struct stonefly_output *output) {
	wl_list_init(&output->workspaces);
	output->active = NULL;
	output->layout = STONEFLY_LAYOUT_UNSET;
	output->ext_group = NULL;

	struct stonefly_server *server = output->server;
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

void stonefly_workspace_output_finish(struct stonefly_output *output) {
	struct stonefly_workspace *ws, *tmp;
	wl_list_for_each_safe(ws, tmp, &output->workspaces, link) {
		/* Toplevels should already be gone with the output; clear back-ptrs. */
		struct stonefly_toplevel *t;
		wl_list_for_each(t, &output->server->toplevels, link) {
			if (t->workspace == ws) {
				t->workspace = NULL;
			}
		}
		wl_list_for_each(t, &output->server->minimized, minimize_link) {
			if (t->workspace == ws) {
				t->workspace = NULL;
			}
		}
		workspace_destroy(ws);
	}
	output->active = NULL;
	if (output->ext_group != NULL) {
		wlr_ext_workspace_group_handle_v1_destroy(output->ext_group);
		output->ext_group = NULL;
	}
}

static void ext_workspace_commit(struct wl_listener *listener, void *data) {
	struct stonefly_server *server = wl_container_of(listener, server, ext_workspace_commit);
	struct wlr_ext_workspace_v1_commit_event *event = data;
	struct wlr_ext_workspace_v1_request *req;
	wl_list_for_each(req, event->requests, link) {
		switch (req->type) {
		case WLR_EXT_WORKSPACE_V1_REQUEST_ACTIVATE:
			if (req->activate.workspace != NULL &&
			    req->activate.workspace->data != NULL) {
				struct stonefly_workspace *ws = req->activate.workspace->data;
				stonefly_workspace_switch(ws->output, ws);
			}
			break;
		case WLR_EXT_WORKSPACE_V1_REQUEST_CREATE_WORKSPACE:
			if (req->create_workspace.group != NULL &&
			    req->create_workspace.group->data != NULL) {
				struct stonefly_output *output = req->create_workspace.group->data;
				/* Dynamic strip already keeps a trailing empty — switch to it. */
				stonefly_workspace_compact(output);
				struct stonefly_workspace *last = NULL;
				struct stonefly_workspace *ws;
				wl_list_for_each(ws, &output->workspaces, link) {
					last = ws;
				}
				if (last != NULL) {
					stonefly_workspace_switch(output, last);
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

void stonefly_workspace_setup(struct stonefly_server *server) {
	server->next_workspace_ext_id = 0;
	server->ext_workspace_mgr = wlr_ext_workspace_manager_v1_create(server->wl_display, 1);
	if (server->ext_workspace_mgr == NULL) {
		SF_LOGW("ext-workspace-v1 unavailable");
		return;
	}
	server->ext_workspace_commit.notify = ext_workspace_commit;
	wl_signal_add(&server->ext_workspace_mgr->events.commit, &server->ext_workspace_commit);
}

void stonefly_workspace_finish(struct stonefly_server *server) {
	/* Destroy per-output workspace scene trees while the root scene is still
	 * alive. Otherwise server_finish's wlr_scene_node_destroy(root) frees them
	 * first, then backend output_destroy → workspace_output_finish double-frees
	 * (ASan SEGV in wl_list_remove on nested ipc exit). */
	struct stonefly_output *output;
	wl_list_for_each(output, &server->outputs, link) {
		stonefly_workspace_output_finish(output);
	}
	if (server->ext_workspace_mgr != NULL) {
		wl_list_remove(&server->ext_workspace_commit.link);
		/* Manager is destroyed with the display. */
		server->ext_workspace_mgr = NULL;
	}
}
