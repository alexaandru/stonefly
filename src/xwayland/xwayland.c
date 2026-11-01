#include "xwayland/xwayland.h"

#include <stdlib.h>
#include <wlr/config.h>

#if WLR_HAS_XWAYLAND

#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_xdg_decoration_v1.h>
#include <wlr/xcursor.h>
#include <wlr/xwayland.h>
#include <wlr/xwayland/server.h>
#include <wlr/xwayland/shell.h>
#include <scenefx/types/wlr_scene.h>

#include "anim/anim.h"
#include "config/config.h"
#include "decor/decor.h"
#include "log/log.h"
#include "output/output.h"
#include "seat/seat.h"
#include "server/server.h"
#include "session/session.h"
#include "ssd/ssd.h"
#include "ssd/ssd_policy.h"
#include "toplevel/ext_capture.h"
#include "toplevel/foreign.h"
#include "toplevel/toplevel.h"
#include "wm/arrange.h"
#include "wm/overview.h"
#include "wm/place_sm.h"
#include "wm/rules.h"
#include "wm/scratchpad.h"
#include "wm/wm.h"
#include "wm/workspace.h"
#include "xdg/xdg.h"

/* ---- Override-redirect (unmanaged) X11 windows: menus, tooltips, popups ---- */

struct sf_xwayland_unmanaged {
	struct sf_server *server;
	struct wlr_xwayland_surface *xsurface;
	struct wlr_scene_tree *scene_tree;
	bool surface_listeners_attached;
	struct wl_listener associate;
	struct wl_listener dissociate;
	struct wl_listener map;
	struct wl_listener unmap;
	struct wl_listener request_configure;
	struct wl_listener set_geometry;
	struct wl_listener destroy;
};

static void unmanaged_map(struct wl_listener *listener, void *data) {
	(void)data;
	struct sf_xwayland_unmanaged *u = wl_container_of(listener, u, map);
	struct sf_server *server = u->server;
	struct wlr_xwayland_surface *xs = u->xsurface;
	if (xs == NULL || xs->surface == NULL) {
		return;
	}
	if (u->scene_tree != NULL) {
		wlr_scene_node_destroy(&u->scene_tree->node);
		u->scene_tree = NULL;
	}
	u->scene_tree = wlr_scene_subsurface_tree_create(server->layers.overlay, xs->surface);
	if (u->scene_tree == NULL) {
		return;
	}
	wlr_scene_node_set_position(&u->scene_tree->node, xs->x, xs->y);
	wlr_scene_node_raise_to_top(&u->scene_tree->node);

	if (!sf_session_locked(server) && wlr_xwayland_surface_override_redirect_wants_focus(xs)) {
		wlr_xwayland_surface_activate(xs, true);
		struct wlr_keyboard *kb = wlr_seat_get_keyboard(server->seat);
		if (kb != NULL) {
			wlr_seat_keyboard_notify_enter(server->seat, xs->surface, kb->keycodes,
						       kb->num_keycodes, &kb->modifiers);
		} else {
			wlr_seat_keyboard_notify_enter(server->seat, xs->surface, NULL, 0, NULL);
		}
	}
}

static void unmanaged_unmap(struct wl_listener *listener, void *data) {
	(void)data;
	struct sf_xwayland_unmanaged *u = wl_container_of(listener, u, unmap);
	struct sf_server *server = u->server;
	struct wlr_surface *surf = u->xsurface != NULL ? u->xsurface->surface : NULL;

	if (u->scene_tree != NULL) {
		wlr_scene_node_destroy(&u->scene_tree->node);
		u->scene_tree = NULL;
	}
	if (surf != NULL && server->seat != NULL &&
	    server->seat->keyboard_state.focused_surface == surf) {
		sf_focus_top(server);
	}
}

static void unmanaged_attach_surface(struct sf_xwayland_unmanaged *u) {
	if (u->xsurface == NULL || u->xsurface->surface == NULL || u->surface_listeners_attached) {
		return;
	}
	u->map.notify = unmanaged_map;
	wl_signal_add(&u->xsurface->surface->events.map, &u->map);
	u->unmap.notify = unmanaged_unmap;
	wl_signal_add(&u->xsurface->surface->events.unmap, &u->unmap);
	u->surface_listeners_attached = true;
}

static void unmanaged_associate(struct wl_listener *listener, void *data) {
	(void)data;
	struct sf_xwayland_unmanaged *u = wl_container_of(listener, u, associate);
	unmanaged_attach_surface(u);
}

static void unmanaged_dissociate(struct wl_listener *listener, void *data) {
	(void)data;
	struct sf_xwayland_unmanaged *u = wl_container_of(listener, u, dissociate);
	if (u->surface_listeners_attached) {
		wl_list_remove(&u->map.link);
		wl_list_remove(&u->unmap.link);
		u->surface_listeners_attached = false;
	}
	if (u->scene_tree != NULL) {
		wlr_scene_node_destroy(&u->scene_tree->node);
		u->scene_tree = NULL;
	}
}

static void unmanaged_request_configure(struct wl_listener *listener, void *data) {
	struct sf_xwayland_unmanaged *u = wl_container_of(listener, u, request_configure);
	struct wlr_xwayland_surface_configure_event *ev = data;
	wlr_xwayland_surface_configure(u->xsurface, ev->x, ev->y, ev->width, ev->height);
	if (u->scene_tree != NULL) {
		wlr_scene_node_set_position(&u->scene_tree->node, ev->x, ev->y);
	}
}

static void unmanaged_set_geometry(struct wl_listener *listener, void *data) {
	(void)data;
	struct sf_xwayland_unmanaged *u = wl_container_of(listener, u, set_geometry);
	if (u->scene_tree != NULL && u->xsurface != NULL) {
		wlr_scene_node_set_position(&u->scene_tree->node, u->xsurface->x, u->xsurface->y);
	}
}

static void unmanaged_destroy(struct wl_listener *listener, void *data) {
	(void)data;
	struct sf_xwayland_unmanaged *u = wl_container_of(listener, u, destroy);
	if (u->surface_listeners_attached) {
		wl_list_remove(&u->map.link);
		wl_list_remove(&u->unmap.link);
		u->surface_listeners_attached = false;
	}
	wl_list_remove(&u->associate.link);
	wl_list_remove(&u->dissociate.link);
	wl_list_remove(&u->request_configure.link);
	wl_list_remove(&u->set_geometry.link);
	wl_list_remove(&u->destroy.link);
	if (u->xsurface != NULL) {
		u->xsurface->data = NULL;
		u->xsurface = NULL;
	}
	if (u->scene_tree != NULL) {
		wlr_scene_node_destroy(&u->scene_tree->node);
		u->scene_tree = NULL;
	}
	free(u);
}

static void create_unmanaged(struct sf_server *server, struct wlr_xwayland_surface *xsurface) {
	struct sf_xwayland_unmanaged *u = calloc(1, sizeof(*u));
	if (u == NULL) {
		return;
	}
	u->server = server;
	u->xsurface = xsurface;
	xsurface->data = u;

	u->associate.notify = unmanaged_associate;
	wl_signal_add(&xsurface->events.associate, &u->associate);
	u->dissociate.notify = unmanaged_dissociate;
	wl_signal_add(&xsurface->events.dissociate, &u->dissociate);
	u->request_configure.notify = unmanaged_request_configure;
	wl_signal_add(&xsurface->events.request_configure, &u->request_configure);
	u->set_geometry.notify = unmanaged_set_geometry;
	wl_signal_add(&xsurface->events.set_geometry, &u->set_geometry);
	u->destroy.notify = unmanaged_destroy;
	wl_signal_add(&xsurface->events.destroy, &u->destroy);

	if (xsurface->surface != NULL) {
		unmanaged_attach_surface(u);
	}
}

/* ---- Managed X11 windows (unified struct sf_toplevel) ---- */

static void xw_toplevel_map(struct wl_listener *listener, void *data) {
	(void)data;
	struct sf_toplevel *toplevel = wl_container_of(listener, toplevel, map);
	struct wlr_xwayland_surface *xs = toplevel->xwayland_surface;

	if (toplevel->scene_tree != NULL) {
		wlr_scene_node_set_enabled(&toplevel->scene_tree->node, false);
	}

	/* Motif hints: show SSD when config allows and client didn't opt out. */
	if (xs != NULL && !toplevel->rule_ssd_set) {
		enum sf_ssd_mode mode = sf_cfg(toplevel->server)->ssd_mode;
		bool no_title = ((uint32_t)xs->decorations &
				 (uint32_t)WLR_XWAYLAND_SURFACE_DECORATIONS_NO_TITLE) != 0;
		int req = no_title ? WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE
				   : WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE;
		toplevel->want_ssd = sf_ssd_want(mode, req);
	}

	wl_list_insert(&toplevel->server->toplevels, &toplevel->link);
	sf_scratchpad_try_claim(toplevel);
	if (sf_toplevel_is_scratchpad(toplevel)) {
		sf_workspace_detach_scratchpad(toplevel);
		if (!sf_layout_sm_is_max_edges(&toplevel->layout)) {
			sf_toplevel_set_max_edges(toplevel, true);
		}
		sf_focus_toplevel(toplevel);
	} else {
		struct sf_output *output = sf_output_at_cursor(toplevel->server);
		if (output != NULL && output->active != NULL) {
			sf_workspace_assign(toplevel, output->active);
		}
		sf_toplevel_place_new(toplevel);
		sf_toplevel_ensure_on_screen(toplevel);
		sf_focus_toplevel(toplevel);
	}
	sf_toplevel_set_pos(toplevel, toplevel->resting_x, toplevel->resting_y);
	sf_decor_apply_buffers(toplevel);
	sf_decor_refresh_close_snap(toplevel);
	sf_toplevel_arm_appear(toplevel);
	sf_foreign_toplevel_on_map(toplevel);
	sf_ext_capture_on_map(toplevel);
	sf_overview_on_toplevel_mapped(toplevel);
}

static void xw_toplevel_unmap(struct wl_listener *listener, void *data) {
	(void)data;
	struct sf_toplevel *toplevel = wl_container_of(listener, toplevel, unmap);
	struct sf_server *server = toplevel->server;

	sf_overview_on_toplevel_unmapped(toplevel);

	if (toplevel == server->grabbed_toplevel) {
		sf_reset_cursor_mode(server);
	}

	bool was_focused = sf_focused_toplevel(server) == toplevel;

	sf_ghost_try_spawn(toplevel);
	sf_anim_cancel(toplevel, false);
	{
		struct sf_place_ev ev = {.kind = SF_PLACE_EV_RESET};
		struct sf_place_result out;
		sf_place_sm_step(&toplevel->place, &ev, NULL, &out);
		toplevel->place = out.state;
	}
	sf_scratchpad_on_destroy(toplevel);

	if (toplevel->scene_tree != NULL) {
		wlr_scene_node_set_enabled(&toplevel->scene_tree->node, false);
	}

	struct sf_workspace *old_ws = toplevel->workspace;
	struct sf_output *ws_output = old_ws != NULL ? old_ws->output : NULL;
	bool was_tiled = toplevel->arrange_managed || toplevel->column != NULL;
	sf_arrange_unlink(toplevel);
	sf_arrange_slot_clear(toplevel);
	toplevel->arrange_managed = false;
	toplevel->workspace = NULL;

	if (toplevel->minimized) {
		wl_list_remove(&toplevel->minimize_link);
		wl_list_init(&toplevel->minimize_link);
		toplevel->minimized = false;
	}

	if (!wl_list_empty(&toplevel->link)) {
		wl_list_remove(&toplevel->link);
		wl_list_init(&toplevel->link);
	}

	if (old_ws != NULL) {
		if (was_tiled) {
			sf_arrange_flush(old_ws);
		} else {
			sf_arrange_schedule(old_ws);
		}
	}
	if (ws_output != NULL) {
		sf_workspace_compact(ws_output);
	}

	if (was_focused || sf_focused_toplevel(server) == NULL) {
		sf_focus_top(server);
	}
	sf_foreign_toplevel_on_unmap(toplevel);
	sf_ext_capture_on_unmap(toplevel);
}

static void xw_toplevel_commit(struct wl_listener *listener, void *data) {
	(void)data;
	struct sf_toplevel *toplevel = wl_container_of(listener, toplevel, commit);
	if (!sf_toplevel_is_mapped(toplevel)) {
		return;
	}
	if (toplevel->anim.active && toplevel->anim.pop) {
		sf_decor_pop_schedule_reapply(toplevel);
	}
	sf_toplevel_try_appear(toplevel);
	sf_decor_update_geom(toplevel);
	sf_decor_refresh_close_snap(toplevel);
	sf_toplevel_layout_observe(toplevel);
	sf_seat_resize_center_sync(toplevel->server);
	if (sf_place_sm_is_managed(&toplevel->place)) {
		sf_toplevel_place_transient(toplevel);
	}
}

static void xw_toplevel_attach_surface(struct sf_toplevel *toplevel) {
	struct wlr_xwayland_surface *xs = toplevel->xwayland_surface;
	if (xs == NULL || xs->surface == NULL) {
		return;
	}
	if (toplevel->xdg_scene_tree != NULL) {
		wlr_scene_node_destroy(&toplevel->xdg_scene_tree->node);
		toplevel->xdg_scene_tree = NULL;
	}
	if (toplevel->scene_tree != NULL) {
		toplevel->xdg_scene_tree =
			wlr_scene_subsurface_tree_create(toplevel->scene_tree, xs->surface);
		sf_decor_restack_chrome(toplevel);
	}
	if (!toplevel->xwayland_surface_listeners_attached) {
		toplevel->map.notify = xw_toplevel_map;
		wl_signal_add(&xs->surface->events.map, &toplevel->map);
		toplevel->unmap.notify = xw_toplevel_unmap;
		wl_signal_add(&xs->surface->events.unmap, &toplevel->unmap);
		toplevel->commit.notify = xw_toplevel_commit;
		wl_signal_add(&xs->surface->events.commit, &toplevel->commit);
		toplevel->xwayland_surface_listeners_attached = true;
	}
}

static void xw_toplevel_associate(struct wl_listener *listener, void *data) {
	(void)data;
	struct sf_toplevel *toplevel = wl_container_of(listener, toplevel, xwayland_associate);
	xw_toplevel_attach_surface(toplevel);
}

static void xw_toplevel_dissociate(struct wl_listener *listener, void *data) {
	(void)data;
	struct sf_toplevel *toplevel = wl_container_of(listener, toplevel, xwayland_dissociate);
	if (toplevel->xwayland_surface_listeners_attached) {
		wl_list_remove(&toplevel->map.link);
		wl_list_remove(&toplevel->unmap.link);
		wl_list_remove(&toplevel->commit.link);
		toplevel->xwayland_surface_listeners_attached = false;
	}
	if (toplevel->xdg_scene_tree != NULL) {
		wlr_scene_node_destroy(&toplevel->xdg_scene_tree->node);
		toplevel->xdg_scene_tree = NULL;
	}
}

static void xw_toplevel_request_configure(struct wl_listener *listener, void *data) {
	struct sf_toplevel *toplevel =
		wl_container_of(listener, toplevel, xwayland_request_configure);
	struct wlr_xwayland_surface_configure_event *ev = data;
	struct wlr_xwayland_surface *xs = toplevel->xwayland_surface;
	if (xs == NULL) {
		return;
	}
	if (toplevel->arrange_managed || sf_layout_sm_is_fixed(&toplevel->layout)) {
		struct wlr_box geom;
		sf_toplevel_layout_geom(toplevel, &geom);
		uint16_t w = geom.width > 0 ? (uint16_t)geom.width : ev->width;
		uint16_t h = geom.height > 0 ? (uint16_t)geom.height : ev->height;
		wlr_xwayland_surface_configure(xs, (int16_t)toplevel->resting_x,
					       (int16_t)toplevel->resting_y, w, h);
		return;
	}
	wlr_xwayland_surface_configure(xs, ev->x, ev->y, ev->width, ev->height);
}

static void xw_toplevel_request_move(struct wl_listener *listener, void *data) {
	(void)data;
	struct sf_toplevel *toplevel = wl_container_of(listener, toplevel, request_move);
	sf_begin_interactive(toplevel, SF_CURSOR_MOVE, 0);
}

static void xw_toplevel_request_resize(struct wl_listener *listener, void *data) {
	struct sf_toplevel *toplevel = wl_container_of(listener, toplevel, request_resize);
	struct wlr_xwayland_resize_event *ev = data;
	sf_begin_interactive(toplevel, SF_CURSOR_RESIZE, ev->edges);
}

static void xw_toplevel_request_maximize(struct wl_listener *listener, void *data) {
	(void)data;
	struct sf_toplevel *toplevel = wl_container_of(listener, toplevel, request_maximize);
	struct wlr_xwayland_surface *xs = toplevel->xwayland_surface;
	if (xs == NULL) {
		return;
	}
	bool want = xs->maximized_horz || xs->maximized_vert;
	sf_toplevel_set_client_maximized(toplevel, want);
}

static void xw_toplevel_request_fullscreen(struct wl_listener *listener, void *data) {
	(void)data;
	struct sf_toplevel *toplevel = wl_container_of(listener, toplevel, request_fullscreen);
	struct wlr_xwayland_surface *xs = toplevel->xwayland_surface;
	if (xs == NULL) {
		return;
	}
	sf_toplevel_set_fullscreen(toplevel, xs->fullscreen, NULL);
}

static void xw_toplevel_request_minimize(struct wl_listener *listener, void *data) {
	struct sf_toplevel *toplevel = wl_container_of(listener, toplevel, request_minimize);
	struct wlr_xwayland_minimize_event *ev = data;
	if (ev->minimize) {
		sf_toplevel_minimize(toplevel);
	} else {
		sf_toplevel_apply_shown(toplevel, true);
	}
}

static void xw_toplevel_request_activate(struct wl_listener *listener, void *data) {
	(void)data;
	struct sf_toplevel *toplevel =
		wl_container_of(listener, toplevel, xwayland_request_activate);
	if (sf_focused_toplevel(toplevel->server) == toplevel) {
		return;
	}
	sf_decor_set_urgent(toplevel, true);
}

static void xw_toplevel_set_title(struct wl_listener *listener, void *data) {
	(void)data;
	struct sf_toplevel *toplevel = wl_container_of(listener, toplevel, set_title);
	sf_ssd_update(toplevel);
	sf_foreign_toplevel_on_title_change(toplevel);
	sf_ext_capture_on_title_change(toplevel);
	sf_rules_consider_place(toplevel);
}

static void xw_toplevel_set_class(struct wl_listener *listener, void *data) {
	(void)data;
	struct sf_toplevel *toplevel = wl_container_of(listener, toplevel, set_app_id);
	sf_scratchpad_try_claim(toplevel);
	sf_foreign_toplevel_on_app_id_change(toplevel);
	sf_ext_capture_on_app_id_change(toplevel);
	sf_rules_consider_place(toplevel);
}

static void xw_toplevel_set_parent(struct wl_listener *listener, void *data) {
	(void)data;
	struct sf_toplevel *toplevel = wl_container_of(listener, toplevel, set_parent);
	sf_foreign_toplevel_on_parent_change(toplevel);
	sf_rules_consider_place(toplevel);
	if (sf_place_sm_is_managed(&toplevel->place)) {
		sf_toplevel_place_transient(toplevel);
	}
}

static void xw_toplevel_set_window_type(struct wl_listener *listener, void *data) {
	(void)data;
	struct sf_toplevel *toplevel =
		wl_container_of(listener, toplevel, xwayland_set_window_type);
	sf_rules_consider_place(toplevel);
	if (sf_place_sm_is_managed(&toplevel->place)) {
		sf_toplevel_place_transient(toplevel);
	}
}

static void xw_toplevel_set_size_hints(struct wl_listener *listener, void *data) {
	(void)data;
	struct sf_toplevel *toplevel = wl_container_of(listener, toplevel, xwayland_set_size_hints);
	sf_rules_consider_place(toplevel);
	if (sf_place_sm_is_managed(&toplevel->place)) {
		sf_toplevel_place_transient(toplevel);
	}
}

static void xw_toplevel_destroy(struct wl_listener *listener, void *data) {
	(void)data;
	struct sf_toplevel *toplevel = wl_container_of(listener, toplevel, destroy);
	struct sf_server *server = toplevel->server;

	sf_overview_on_toplevel_unmapped(toplevel);
	if (toplevel == server->grabbed_toplevel) {
		sf_reset_cursor_mode(server);
	}
	sf_anim_cancel(toplevel, false);
	sf_scratchpad_on_destroy(toplevel);
	sf_arrange_unlink(toplevel);
	sf_arrange_slot_clear(toplevel);
	if (toplevel->minimized) {
		wl_list_remove(&toplevel->minimize_link);
		wl_list_init(&toplevel->minimize_link);
		toplevel->minimized = false;
	}
	if (!wl_list_empty(&toplevel->link)) {
		wl_list_remove(&toplevel->link);
		wl_list_init(&toplevel->link);
	}

	if (toplevel->xwayland_surface_listeners_attached) {
		wl_list_remove(&toplevel->map.link);
		wl_list_remove(&toplevel->unmap.link);
		wl_list_remove(&toplevel->commit.link);
		toplevel->xwayland_surface_listeners_attached = false;
	}
	wl_list_remove(&toplevel->xwayland_associate.link);
	wl_list_remove(&toplevel->xwayland_dissociate.link);
	wl_list_remove(&toplevel->xwayland_request_configure.link);
	wl_list_remove(&toplevel->xwayland_request_activate.link);
	wl_list_remove(&toplevel->request_move.link);
	wl_list_remove(&toplevel->request_resize.link);
	wl_list_remove(&toplevel->request_maximize.link);
	wl_list_remove(&toplevel->request_fullscreen.link);
	wl_list_remove(&toplevel->request_minimize.link);
	wl_list_remove(&toplevel->set_title.link);
	wl_list_remove(&toplevel->set_app_id.link);
	wl_list_remove(&toplevel->set_parent.link);
	wl_list_remove(&toplevel->xwayland_set_window_type.link);
	wl_list_remove(&toplevel->xwayland_set_size_hints.link);
	wl_list_remove(&toplevel->destroy.link);

	if (toplevel->xwayland_surface != NULL) {
		toplevel->xwayland_surface->data = NULL;
		toplevel->xwayland_surface = NULL;
	}
	sf_foreign_toplevel_on_unmap(toplevel);
	sf_ext_capture_on_unmap(toplevel);
	sf_ssd_destroy(toplevel);
	sf_decor_finish_toplevel(toplevel);
	free(toplevel->tag);
	free(toplevel->description);
	free(toplevel->icon_name);
	free(toplevel);
}

static void create_managed(struct sf_server *server, struct wlr_xwayland_surface *xsurface) {
	struct sf_toplevel *toplevel = calloc(1, sizeof(*toplevel));
	if (toplevel == NULL) {
		return;
	}
	toplevel->kind = SF_NODE_TOPLEVEL;
	toplevel->server = server;
	toplevel->xwayland_surface = xsurface;
	toplevel->id = ++server->next_toplevel_id;
	wl_list_init(&toplevel->link);
	wl_list_init(&toplevel->minimize_link);
	wl_list_init(&toplevel->column_link);
	toplevel->column_row_weight = 1.0f;

	toplevel->scene_tree = wlr_scene_tree_create(server->layers.toplevel);
	if (toplevel->scene_tree == NULL) {
		free(toplevel);
		return;
	}
	toplevel->scene_tree->node.data = toplevel;
	wlr_scene_node_set_enabled(&toplevel->scene_tree->node, false);
	xsurface->data = toplevel;

	sf_decor_init_toplevel(toplevel);

	toplevel->xwayland_associate.notify = xw_toplevel_associate;
	wl_signal_add(&xsurface->events.associate, &toplevel->xwayland_associate);
	toplevel->xwayland_dissociate.notify = xw_toplevel_dissociate;
	wl_signal_add(&xsurface->events.dissociate, &toplevel->xwayland_dissociate);
	toplevel->xwayland_request_configure.notify = xw_toplevel_request_configure;
	wl_signal_add(&xsurface->events.request_configure, &toplevel->xwayland_request_configure);
	toplevel->xwayland_request_activate.notify = xw_toplevel_request_activate;
	wl_signal_add(&xsurface->events.request_activate, &toplevel->xwayland_request_activate);
	toplevel->request_move.notify = xw_toplevel_request_move;
	wl_signal_add(&xsurface->events.request_move, &toplevel->request_move);
	toplevel->request_resize.notify = xw_toplevel_request_resize;
	wl_signal_add(&xsurface->events.request_resize, &toplevel->request_resize);
	toplevel->request_maximize.notify = xw_toplevel_request_maximize;
	wl_signal_add(&xsurface->events.request_maximize, &toplevel->request_maximize);
	toplevel->request_fullscreen.notify = xw_toplevel_request_fullscreen;
	wl_signal_add(&xsurface->events.request_fullscreen, &toplevel->request_fullscreen);
	toplevel->request_minimize.notify = xw_toplevel_request_minimize;
	wl_signal_add(&xsurface->events.request_minimize, &toplevel->request_minimize);
	toplevel->set_title.notify = xw_toplevel_set_title;
	wl_signal_add(&xsurface->events.set_title, &toplevel->set_title);
	toplevel->set_app_id.notify = xw_toplevel_set_class;
	wl_signal_add(&xsurface->events.set_class, &toplevel->set_app_id);
	toplevel->set_parent.notify = xw_toplevel_set_parent;
	wl_signal_add(&xsurface->events.set_parent, &toplevel->set_parent);
	toplevel->xwayland_set_window_type.notify = xw_toplevel_set_window_type;
	wl_signal_add(&xsurface->events.set_window_type, &toplevel->xwayland_set_window_type);
	toplevel->xwayland_set_size_hints.notify = xw_toplevel_set_size_hints;
	wl_signal_add(&xsurface->events.set_size_hints, &toplevel->xwayland_set_size_hints);
	toplevel->destroy.notify = xw_toplevel_destroy;
	wl_signal_add(&xsurface->events.destroy, &toplevel->destroy);

	if (xsurface->surface != NULL) {
		xw_toplevel_attach_surface(toplevel);
	}
}

/* ---- Xwayland server lifecycle ---- */

static void xwayland_on_ready(struct wl_listener *listener, void *data) {
	(void)data;
	struct sf_server *server = wl_container_of(listener, server, xwayland.on_ready);
	struct wlr_xwayland *xw = server->xwayland.wlr;
	if (xw == NULL) {
		return;
	}
	wlr_xwayland_set_seat(xw, server->seat);
	if (server->cursor_mgr != NULL) {
		struct wlr_xcursor *xc =
			wlr_xcursor_manager_get_xcursor(server->cursor_mgr, "default", 1);
		if (xc != NULL && xc->image_count > 0 && xc->images[0] != NULL) {
			struct wlr_xcursor_image *img = xc->images[0];
			wlr_xwayland_set_cursor(xw, wlr_xcursor_image_get_buffer(img),
						(int32_t)img->hotspot_x, (int32_t)img->hotspot_y);
		}
	}
	SF_LOGI("xwayland ready",
		sf_str("display", xw->display_name != NULL ? xw->display_name : ""));
}

static void xwayland_on_new_surface(struct wl_listener *listener, void *data) {
	struct sf_server *server = wl_container_of(listener, server, xwayland.on_new_surface);
	struct wlr_xwayland_surface *xsurface = data;
	if (xsurface == NULL) {
		return;
	}
	if (xsurface->override_redirect) {
		create_unmanaged(server, xsurface);
	} else {
		create_managed(server, xsurface);
	}
}

void sf_xwayland_setup(struct sf_server *server) {
	if (server == NULL || server->wl_display == NULL || server->compositor == NULL) {
		return;
	}
	struct wlr_xwayland *xw = wlr_xwayland_create(server->wl_display, server->compositor, true);
	if (xw == NULL) {
		SF_LOGW("xwayland unavailable");
		return;
	}
	server->xwayland.wlr = xw;
	server->xwayland.on_ready.notify = xwayland_on_ready;
	wl_signal_add(&xw->events.ready, &server->xwayland.on_ready);
	server->xwayland.on_new_surface.notify = xwayland_on_new_surface;
	wl_signal_add(&xw->events.new_surface, &server->xwayland.on_new_surface);

	if (xw->display_name != NULL && xw->display_name[0] != '\0') {
		setenv("DISPLAY", xw->display_name, true);
		SF_LOGI("xwayland initialized (lazy)", sf_str("display", xw->display_name));
	}
}

void sf_xwayland_finish(struct sf_server *server) {
	if (server == NULL || server->xwayland.wlr == NULL) {
		return;
	}
	wl_list_remove(&server->xwayland.on_ready.link);
	wl_list_remove(&server->xwayland.on_new_surface.link);
	wlr_xwayland_destroy(server->xwayland.wlr);
	server->xwayland.wlr = NULL;
}

bool sf_xwayland_allow_global(const struct sf_server *server, const struct wl_client *client,
			      const struct wl_global *global) {
	if (server == NULL || server->xwayland.wlr == NULL ||
	    server->xwayland.wlr->shell_v1 == NULL) {
		return true;
	}
	if (global != server->xwayland.wlr->shell_v1->global) {
		return true;
	}
	return server->xwayland.wlr->server != NULL &&
	       client == server->xwayland.wlr->server->client;
}

#else

void sf_xwayland_setup(struct sf_server *server) {
	(void)server;
}

void sf_xwayland_finish(struct sf_server *server) {
	(void)server;
}

bool sf_xwayland_allow_global(const struct sf_server *server, const struct wl_client *client,
			      const struct wl_global *global) {
	(void)server;
	(void)client;
	(void)global;
	return true;
}

#endif
