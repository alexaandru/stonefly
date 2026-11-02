#define _POSIX_C_SOURCE 200809L

#include <assert.h>
#include <stdlib.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_cursor.h>
#include <scenefx/types/wlr_scene.h>
#include <wlr/types/wlr_xdg_activation_v1.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/types/wlr_xdg_system_bell_v1.h>
#include <wlr/util/box.h>
#include <wlr/util/edges.h>

#include "anim/anim.h"
#include "decor/decor.h"
#include "log/log.h"
#include "seat/seat.h"
#include "toplevel/foreign.h"
#include "wm/drag_snap.h"
#include "wm/scratchpad.h"
#include "wm/wm.h"
#include "xdg/xdg.h"

static struct stonefly_toplevel *toplevel_from_surface(
		struct stonefly_server *server, struct wlr_surface *surface) {
	if (surface == NULL) {
		return NULL;
	}
	struct wlr_surface *root = wlr_surface_get_root_surface(surface);
	struct stonefly_toplevel *t;
	wl_list_for_each(t, &server->toplevels, link) {
		if (t->xdg_toplevel->base->surface == root) {
			return t;
		}
	}
	return NULL;
}

static void request_activate(struct wl_listener *listener, void *data) {
	struct stonefly_server *server =
		wl_container_of(listener, server, request_activate);
	const struct wlr_xdg_activation_v1_request_activate_event *event = data;
	struct stonefly_toplevel *toplevel =
		toplevel_from_surface(server, event->surface);
	if (toplevel == NULL) {
		return;
	}
	if (stonefly_focused_toplevel(server) == toplevel) {
		return;
	}
	stonefly_decor_set_urgent(toplevel, true);
	SF_LOGI("urgency", sf_i64("id", (int64_t)toplevel->id));
}

static void system_bell_ring(struct wl_listener *listener, void *data) {
	struct stonefly_server *server =
		wl_container_of(listener, server, system_bell_ring);
	const struct wlr_xdg_system_bell_v1_ring_event *event = data;
	struct stonefly_toplevel *toplevel =
		toplevel_from_surface(server, event->surface);
	if (toplevel == NULL) {
		toplevel = stonefly_focused_toplevel(server);
	}
	stonefly_decor_ring_bell(toplevel);
	SF_LOGI("system bell",
		sf_i64("id", toplevel != NULL ? (int64_t)toplevel->id : 0));
}

static void xdg_toplevel_map(struct wl_listener *listener, void *data) {
	(void)data;
	/* Called when the surface is mapped, or ready to display on-screen. */
	struct stonefly_toplevel *toplevel = wl_container_of(listener, toplevel, map);

	if (toplevel->scene_tree != NULL) {
		wlr_scene_node_set_enabled(&toplevel->scene_tree->node, true);
	}

	wl_list_insert(&toplevel->server->toplevels, &toplevel->link);
	stonefly_scratchpad_try_claim(toplevel);
	if (stonefly_toplevel_is_scratchpad(toplevel)) {
		/* Claim may have run early on set_app_id; finish first-spawn max. */
		if (!toplevel->maximized_edges) {
			stonefly_toplevel_set_max_edges(toplevel, true);
		}
		stonefly_focus_toplevel(toplevel);
	} else {
		stonefly_toplevel_place_cascade(toplevel);
		/* Bar exclusive may already be applied; keep cascade origin in usable. */
		stonefly_toplevel_ensure_on_screen(toplevel);
		stonefly_focus_toplevel(toplevel);
	}
	stonefly_decor_apply_buffers(toplevel);
	/* Park off-screen then slide in (peeble open/appear). */
	stonefly_anim_start_show(toplevel);
	stonefly_foreign_toplevel_on_map(toplevel);
}

static void xdg_toplevel_unmap(struct wl_listener *listener, void *data) {
	(void)data;
	/* Called when the surface is unmapped, and should no longer be shown. */
	struct stonefly_toplevel *toplevel = wl_container_of(listener, toplevel, unmap);
	struct stonefly_server *server = toplevel->server;

	/* Reset the cursor mode if the grabbed toplevel was unmapped. */
	if (toplevel == server->grabbed_toplevel) {
		stonefly_reset_cursor_mode(server);
	}

	bool was_focused = stonefly_focused_toplevel(server) == toplevel;

	/* Client Ctrl+W / CSD close: snapshot + slide (FORK.md close-ghost). */
	stonefly_ghost_try_spawn(toplevel);
	stonefly_anim_cancel(toplevel, false);
	stonefly_scratchpad_on_destroy(toplevel);

	/* Hide wrapper immediately so blur/border/shadow cannot outlive the
	 * client buffer. Ghost (if any) is a separate tree under layers.toplevel. */
	if (toplevel->scene_tree != NULL) {
		wlr_scene_node_set_enabled(&toplevel->scene_tree->node, false);
	}

	if (toplevel->minimized) {
		wl_list_remove(&toplevel->minimize_link);
		wl_list_init(&toplevel->minimize_link);
		toplevel->minimized = false;
	}

	wl_list_remove(&toplevel->link);

	/* Peeble Focus(nil): fall back to the previous top of the stack.
	 * Also recover when the seat already dropped focus before unmap. */
	if (was_focused || stonefly_focused_toplevel(server) == NULL) {
		stonefly_focus_top(server);
	}
	stonefly_foreign_toplevel_on_unmap(toplevel);
}

static void xdg_toplevel_commit(struct wl_listener *listener, void *data) {
	(void)data;
	/* Called when a new surface state is committed. */
	struct stonefly_toplevel *toplevel = wl_container_of(listener, toplevel, commit);

	if (toplevel->xdg_toplevel->base->initial_commit) {
		/* When an xdg_surface performs an initial commit, the compositor must
		 * reply with a configure so the client can map the surface. stonefly
		 * configures the xdg_toplevel with 0,0 size to let the client pick the
		 * dimensions itself. */
		wlr_xdg_toplevel_set_size(toplevel->xdg_toplevel, 0, 0);
		return;
	}
	stonefly_decor_update_geom(toplevel);
	stonefly_foreign_toplevel_on_outputs_change(toplevel);
}

static void xdg_toplevel_set_title(struct wl_listener *listener, void *data) {
	(void)data;
	struct stonefly_toplevel *toplevel =
		wl_container_of(listener, toplevel, set_title);
	stonefly_foreign_toplevel_on_title_change(toplevel);
}

static void xdg_toplevel_set_app_id(struct wl_listener *listener, void *data) {
	(void)data;
	struct stonefly_toplevel *toplevel =
		wl_container_of(listener, toplevel, set_app_id);
	stonefly_scratchpad_try_claim(toplevel);
	stonefly_foreign_toplevel_on_app_id_change(toplevel);
}

static void xdg_toplevel_set_parent(struct wl_listener *listener, void *data) {
	(void)data;
	struct stonefly_toplevel *toplevel =
		wl_container_of(listener, toplevel, set_parent);
	stonefly_foreign_toplevel_on_parent_change(toplevel);
}

static void xdg_toplevel_destroy(struct wl_listener *listener, void *data) {
	(void)data;
	/* Called when the xdg_toplevel is destroyed. */
	struct stonefly_toplevel *toplevel = wl_container_of(listener, toplevel, destroy);

	stonefly_foreign_toplevel_on_close(toplevel);
	stonefly_anim_cancel(toplevel, false);
	stonefly_scratchpad_on_destroy(toplevel);
	stonefly_decor_finish_toplevel(toplevel);

	if (toplevel->minimized) {
		wl_list_remove(&toplevel->minimize_link);
	}

	wl_list_remove(&toplevel->map.link);
	wl_list_remove(&toplevel->unmap.link);
	wl_list_remove(&toplevel->commit.link);
	wl_list_remove(&toplevel->destroy.link);
	wl_list_remove(&toplevel->request_move.link);
	wl_list_remove(&toplevel->request_resize.link);
	wl_list_remove(&toplevel->request_maximize.link);
	wl_list_remove(&toplevel->request_fullscreen.link);
	wl_list_remove(&toplevel->set_title.link);
	wl_list_remove(&toplevel->set_app_id.link);
	wl_list_remove(&toplevel->set_parent.link);

	free(toplevel);
}

void stonefly_begin_interactive(struct stonefly_toplevel *toplevel,
		enum stonefly_cursor_mode mode, uint32_t edges) {
	/* This function sets up an interactive move or resize operation, where the
	 * compositor stops propagating pointer events to clients and instead
	 * consumes them itself, to move or resize windows. */
	struct stonefly_server *server = toplevel->server;

	stonefly_anim_cancel(toplevel, true);

	if (mode == STONEFLY_CURSOR_MOVE) {
		/* Peeble PrepareDrag: float snap/max, keep PreSnap, record origin. */
		stonefly_drag_snap_prepare(toplevel);
	}

	server->grabbed_toplevel = toplevel;
	server->cursor_mode = mode;

	if (mode == STONEFLY_CURSOR_MOVE) {
		server->grab_x = server->cursor->x - toplevel->scene_tree->node.x;
		server->grab_y = server->cursor->y - toplevel->scene_tree->node.y;
		stonefly_drag_snap_begin(server, toplevel);
	} else {
		struct wlr_box *geo_box = &toplevel->xdg_toplevel->base->geometry;

		double border_x = (toplevel->scene_tree->node.x + geo_box->x) +
			((edges & WLR_EDGE_RIGHT) ? geo_box->width : 0);
		double border_y = (toplevel->scene_tree->node.y + geo_box->y) +
			((edges & WLR_EDGE_BOTTOM) ? geo_box->height : 0);
		server->grab_x = server->cursor->x - border_x;
		server->grab_y = server->cursor->y - border_y;

		server->grab_geobox = *geo_box;
		server->grab_geobox.x += toplevel->scene_tree->node.x;
		server->grab_geobox.y += toplevel->scene_tree->node.y;

		server->resize_edges = edges;
	}
}

static void xdg_toplevel_request_move(
		struct wl_listener *listener, void *data) {
	(void)data;
	/* This event is raised when a client would like to begin an interactive
	 * move, typically because the user clicked on their client-side
	 * decorations. Note that a more sophisticated compositor should check the
	 * provided serial against a list of button press serials sent to this
	 * client, to prevent the client from requesting this whenever they want. */
	struct stonefly_toplevel *toplevel = wl_container_of(listener, toplevel, request_move);
	stonefly_begin_interactive(toplevel, STONEFLY_CURSOR_MOVE, 0);
}

static void xdg_toplevel_request_resize(
		struct wl_listener *listener, void *data) {
	/* This event is raised when a client would like to begin an interactive
	 * resize, typically because the user clicked on their client-side
	 * decorations. Note that a more sophisticated compositor should check the
	 * provided serial against a list of button press serials sent to this
	 * client, to prevent the client from requesting this whenever they want. */
	struct wlr_xdg_toplevel_resize_event *event = data;
	struct stonefly_toplevel *toplevel = wl_container_of(listener, toplevel, request_resize);
	stonefly_begin_interactive(toplevel, STONEFLY_CURSOR_RESIZE, event->edges);
}

static void xdg_toplevel_request_maximize(
		struct wl_listener *listener, void *data) {
	(void)data;
	/* Honor the client's requested maximized bit — never toggle blindly
	 * (a toggle on unset-maximize after a bad 0×0 save made windows vanish). */
	struct stonefly_toplevel *toplevel =
		wl_container_of(listener, toplevel, request_maximize);
	if (!toplevel->xdg_toplevel->base->initialized) {
		return;
	}
	stonefly_toplevel_set_max_edges(toplevel, toplevel->xdg_toplevel->requested.maximized);
}

static void xdg_toplevel_request_fullscreen(
		struct wl_listener *listener, void *data) {
	(void)data;
	struct stonefly_toplevel *toplevel =
		wl_container_of(listener, toplevel, request_fullscreen);
	if (!toplevel->xdg_toplevel->base->initialized) {
		return;
	}
	stonefly_toplevel_set_fullscreen(toplevel,
		toplevel->xdg_toplevel->requested.fullscreen, NULL);
}

static void server_new_xdg_toplevel(struct wl_listener *listener, void *data) {
	/* This event is raised when a client creates a new toplevel (application window). */
	struct stonefly_server *server = wl_container_of(listener, server, new_xdg_toplevel);
	struct wlr_xdg_toplevel *xdg_toplevel = data;

	/* Allocate a stonefly_toplevel for this surface */
	struct stonefly_toplevel *toplevel = calloc(1, sizeof(*toplevel));
	toplevel->kind = STONEFLY_NODE_TOPLEVEL;
	toplevel->server = server;
	toplevel->xdg_toplevel = xdg_toplevel;
	toplevel->id = ++server->next_toplevel_id;
	wl_list_init(&toplevel->minimize_link);
	/* Wrapper holds blur/shadow/border; xdg content is a child. */
	toplevel->scene_tree = wlr_scene_tree_create(server->layers.toplevel);
	toplevel->xdg_scene_tree = wlr_scene_xdg_surface_create(
		toplevel->scene_tree, xdg_toplevel->base);
	toplevel->scene_tree->node.data = toplevel;
	xdg_toplevel->base->data = toplevel->scene_tree;
	stonefly_decor_init_toplevel(toplevel);

	/* Listen to the various events it can emit */
	toplevel->map.notify = xdg_toplevel_map;
	wl_signal_add(&xdg_toplevel->base->surface->events.map, &toplevel->map);
	toplevel->unmap.notify = xdg_toplevel_unmap;
	wl_signal_add(&xdg_toplevel->base->surface->events.unmap, &toplevel->unmap);
	toplevel->commit.notify = xdg_toplevel_commit;
	wl_signal_add(&xdg_toplevel->base->surface->events.commit, &toplevel->commit);

	toplevel->destroy.notify = xdg_toplevel_destroy;
	wl_signal_add(&xdg_toplevel->events.destroy, &toplevel->destroy);

	toplevel->request_move.notify = xdg_toplevel_request_move;
	wl_signal_add(&xdg_toplevel->events.request_move, &toplevel->request_move);
	toplevel->request_resize.notify = xdg_toplevel_request_resize;
	wl_signal_add(&xdg_toplevel->events.request_resize, &toplevel->request_resize);
	toplevel->request_maximize.notify = xdg_toplevel_request_maximize;
	wl_signal_add(&xdg_toplevel->events.request_maximize, &toplevel->request_maximize);
	toplevel->request_fullscreen.notify = xdg_toplevel_request_fullscreen;
	wl_signal_add(&xdg_toplevel->events.request_fullscreen, &toplevel->request_fullscreen);
	toplevel->set_title.notify = xdg_toplevel_set_title;
	wl_signal_add(&xdg_toplevel->events.set_title, &toplevel->set_title);
	toplevel->set_app_id.notify = xdg_toplevel_set_app_id;
	wl_signal_add(&xdg_toplevel->events.set_app_id, &toplevel->set_app_id);
	toplevel->set_parent.notify = xdg_toplevel_set_parent;
	wl_signal_add(&xdg_toplevel->events.set_parent, &toplevel->set_parent);
}

static void xdg_popup_commit(struct wl_listener *listener, void *data) {
	(void)data;
	/* Called when a new surface state is committed. */
	struct stonefly_popup *popup = wl_container_of(listener, popup, commit);

	if (popup->xdg_popup->base->initial_commit) {
		/* When an xdg_surface performs an initial commit, the compositor must
		 * reply with a configure so the client can map the surface.
		 * stonefly sends an empty configure. A more sophisticated compositor
		 * might change an xdg_popup's geometry to ensure it's not positioned
		 * off-screen, for example. */
		wlr_xdg_surface_schedule_configure(popup->xdg_popup->base);
	}
}

static void xdg_popup_destroy(struct wl_listener *listener, void *data) {
	(void)data;
	/* Called when the xdg_popup is destroyed. */
	struct stonefly_popup *popup = wl_container_of(listener, popup, destroy);

	wl_list_remove(&popup->commit.link);
	wl_list_remove(&popup->destroy.link);

	free(popup);
}

static void server_new_xdg_popup(struct wl_listener *listener, void *data) {
	(void)listener;
	/* This event is raised when a client creates a new popup. */
	struct wlr_xdg_popup *xdg_popup = data;

	struct stonefly_popup *popup = calloc(1, sizeof(*popup));
	popup->xdg_popup = xdg_popup;

	/* Parent scene tree: xdg surfaces stash theirs on xdg_surface->data;
	 * layer surfaces stash a popup tree on wlr_surface->data. */
	struct wlr_scene_tree *parent_tree = NULL;
	struct wlr_xdg_surface *xdg_parent =
		wlr_xdg_surface_try_from_wlr_surface(xdg_popup->parent);
	if (xdg_parent != NULL) {
		parent_tree = xdg_parent->data;
	} else {
		parent_tree = xdg_popup->parent->data;
	}
	assert(parent_tree != NULL);
	xdg_popup->base->data = wlr_scene_xdg_surface_create(parent_tree, xdg_popup->base);

	popup->commit.notify = xdg_popup_commit;
	wl_signal_add(&xdg_popup->base->surface->events.commit, &popup->commit);

	popup->destroy.notify = xdg_popup_destroy;
	wl_signal_add(&xdg_popup->events.destroy, &popup->destroy);
}


void stonefly_xdg_setup(struct stonefly_server *server) {
	wl_list_init(&server->toplevels);
	server->xdg_shell = wlr_xdg_shell_create(server->wl_display, 3);
	server->new_xdg_toplevel.notify = server_new_xdg_toplevel;
	wl_signal_add(&server->xdg_shell->events.new_toplevel, &server->new_xdg_toplevel);
	server->new_xdg_popup.notify = server_new_xdg_popup;
	wl_signal_add(&server->xdg_shell->events.new_popup, &server->new_xdg_popup);

	/* FORK.md #1281 — demand-attention via xdg-activation. */
	server->xdg_activation = wlr_xdg_activation_v1_create(server->wl_display);
	if (server->xdg_activation != NULL) {
		server->request_activate.notify = request_activate;
		wl_signal_add(&server->xdg_activation->events.request_activate,
			&server->request_activate);
	}

	/* FORK.md #1389 — visual system bell. */
	server->system_bell = wlr_xdg_system_bell_v1_create(server->wl_display, 1);
	if (server->system_bell != NULL) {
		server->system_bell_ring.notify = system_bell_ring;
		wl_signal_add(&server->system_bell->events.ring,
			&server->system_bell_ring);
	}
}
