#define _POSIX_C_SOURCE 200809L

#include <stdlib.h>
#include <unistd.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/box.h>

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

static bool stonefly_toplevel_output_box(struct stonefly_toplevel *toplevel,
		struct wlr_box *box) {
	struct stonefly_server *server = toplevel->server;
	struct wlr_box geom;
	stonefly_toplevel_layout_geom(toplevel, &geom);
	double cx = geom.x + geom.width / 2.0;
	double cy = geom.y + geom.height / 2.0;
	struct wlr_output *output =
		wlr_output_layout_output_at(server->output_layout, cx, cy);
	if (output == NULL) {
		output = wlr_output_layout_output_at(server->output_layout,
			server->cursor->x, server->cursor->y);
	}
	if (output == NULL) {
		return false;
	}
	wlr_output_layout_get_box(server->output_layout, output, box);
	return box->width > 0 && box->height > 0;
}

static void stonefly_cover_fractional_gap(struct wlr_box *box) {
	/* Same class of gap as peeble at non-integer scale: grow 1 logical px. */
	float scale = STONEFLY_OUTPUT_SCALE;
	if (scale != (float)(int)scale) {
		box->width += 1;
		box->height += 1;
	}
}

static void stonefly_toplevel_apply_box(struct stonefly_toplevel *toplevel,
		const struct wlr_box *box) {
	wlr_scene_node_set_position(&toplevel->scene_tree->node, box->x, box->y);
	wlr_xdg_toplevel_set_size(toplevel->xdg_toplevel, box->width, box->height);
}

void stonefly_toplevel_restore_saved(struct stonefly_toplevel *toplevel) {
	if (!toplevel->has_saved_geom) {
		return;
	}
	wlr_xdg_toplevel_set_maximized(toplevel->xdg_toplevel, false);
	stonefly_toplevel_apply_box(toplevel, &toplevel->saved_geom);
	toplevel->has_saved_geom = false;
	toplevel->maximized_edges = false;
	toplevel->snapped = STONEFLY_SNAP_NONE;
}

void stonefly_toplevel_set_max_edges(struct stonefly_toplevel *toplevel, bool want) {
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
			wlr_xdg_surface_schedule_configure(toplevel->xdg_toplevel->base);
		}
		return;
	}
	struct wlr_box out;
	if (!stonefly_toplevel_output_box(toplevel, &out)) {
		wlr_xdg_surface_schedule_configure(toplevel->xdg_toplevel->base);
		return;
	}
	stonefly_toplevel_save_geom(toplevel);
	toplevel->snapped = STONEFLY_SNAP_NONE;
	stonefly_cover_fractional_gap(&out);
	wlr_xdg_toplevel_set_maximized(toplevel->xdg_toplevel, true);
	stonefly_toplevel_apply_box(toplevel, &out);
	toplevel->maximized_edges = true;
}

void stonefly_toplevel_toggle_max_edges(struct stonefly_toplevel *toplevel) {
	stonefly_toplevel_set_max_edges(toplevel, !toplevel->maximized_edges);
}

void stonefly_toplevel_snap(struct stonefly_toplevel *toplevel,
		enum stonefly_snap edge) {
	struct wlr_box out;
	if (!stonefly_toplevel_output_box(toplevel, &out)) {
		return;
	}
	stonefly_toplevel_save_geom(toplevel);
	if (toplevel->maximized_edges) {
		wlr_xdg_toplevel_set_maximized(toplevel->xdg_toplevel, false);
		toplevel->maximized_edges = false;
	}
	struct wlr_box box = out;
	switch (edge) {
	case STONEFLY_SNAP_LEFT:
		box.width = out.width / 2;
		break;
	case STONEFLY_SNAP_RIGHT:
		box.width = out.width / 2;
		box.x = out.x + out.width - box.width;
		break;
	case STONEFLY_SNAP_TOP:
		box.height = out.height / 2;
		break;
	case STONEFLY_SNAP_BOTTOM:
		box.height = out.height / 2;
		box.y = out.y + out.height - box.height;
		break;
	case STONEFLY_SNAP_NONE:
		return;
	}
	stonefly_toplevel_apply_box(toplevel, &box);
	toplevel->snapped = edge;
}

void stonefly_toplevel_unsnap(struct stonefly_toplevel *toplevel) {
	if (toplevel->snapped == STONEFLY_SNAP_NONE && !toplevel->maximized_edges) {
		return;
	}
	stonefly_toplevel_restore_saved(toplevel);
}

void stonefly_toplevel_unminimize(struct stonefly_toplevel *toplevel) {
	if (!toplevel->minimized) {
		return;
	}
	toplevel->minimized = false;
	wl_list_remove(&toplevel->minimize_link);
	wl_list_init(&toplevel->minimize_link);
	wlr_scene_node_set_enabled(&toplevel->scene_tree->node, true);
}

void stonefly_focus_toplevel(struct stonefly_toplevel *toplevel) {
	/* Note: this function only deals with keyboard focus. */
	if (toplevel == NULL) {
		return;
	}
	if (toplevel->minimized) {
		stonefly_toplevel_unminimize(toplevel);
	}
	struct stonefly_server *server = toplevel->server;
	struct wlr_seat *seat = server->seat;
	struct wlr_surface *prev_surface = seat->keyboard_state.focused_surface;
	struct wlr_surface *surface = toplevel->xdg_toplevel->base->surface;
	if (prev_surface == surface) {
		/* Don't re-focus an already focused surface. */
		return;
	}
	if (prev_surface) {
		/*
		 * Deactivate the previously focused surface. This lets the client know
		 * it no longer has focus and the client will repaint accordingly, e.g.
		 * stop displaying a caret.
		 */
		struct wlr_xdg_toplevel *prev_toplevel =
			wlr_xdg_toplevel_try_from_wlr_surface(prev_surface);
		if (prev_toplevel != NULL) {
			wlr_xdg_toplevel_set_activated(prev_toplevel, false);
		}
	}
	struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(seat);
	/* Move the toplevel to the front */
	wlr_scene_node_raise_to_top(&toplevel->scene_tree->node);
	wl_list_remove(&toplevel->link);
	wl_list_insert(&server->toplevels, &toplevel->link);
	/* Activate the new surface */
	wlr_xdg_toplevel_set_activated(toplevel->xdg_toplevel, true);
	/*
	 * Tell the seat to have the keyboard enter this surface. wlroots will keep
	 * track of this and automatically send key events to the appropriate
	 * clients without additional work on your part.
	 */
	if (keyboard != NULL) {
		wlr_seat_keyboard_notify_enter(seat, surface,
			keyboard->keycodes, keyboard->num_keycodes, &keyboard->modifiers);
	}
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

void stonefly_toplevel_minimize(struct stonefly_toplevel *toplevel) {
	if (toplevel->minimized) {
		return;
	}
	struct stonefly_server *server = toplevel->server;
	toplevel->minimized = true;
	wlr_scene_node_set_enabled(&toplevel->scene_tree->node, false);
	wl_list_insert(&server->minimized, &toplevel->minimize_link);

	bool was_focused = stonefly_focused_toplevel(server) == toplevel;
	if (was_focused) {
		wlr_xdg_toplevel_set_activated(toplevel->xdg_toplevel, false);
		wlr_seat_keyboard_notify_clear_focus(server->seat);
		/* Focus next visible toplevel if any. */
		struct stonefly_toplevel *t;
		wl_list_for_each(t, &server->toplevels, link) {
			if (!t->minimized) {
				stonefly_focus_toplevel(t);
				return;
			}
		}
	}
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

static void stonefly_close_focused(struct stonefly_server *server) {
	struct stonefly_toplevel *toplevel = stonefly_focused_toplevel(server);
	if (toplevel != NULL) {
		wlr_xdg_toplevel_send_close(toplevel->xdg_toplevel);
	}
}

static void stonefly_spawn_cmd(const char *cmd) {
	if (fork() == 0) {
		execl("/bin/sh", "/bin/sh", "-c", cmd, (void *)NULL);
		_exit(1);
	}
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
			if (include_minimized || !candidate->minimized) {
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
		if (include_minimized || !t->minimized) {
			stonefly_focus_toplevel(t);
			return;
		}
	} while (t != current);
}

void stonefly_run_action(struct stonefly_server *server, enum stonefly_action action) {
	struct stonefly_toplevel *focused = stonefly_focused_toplevel(server);
	switch (action) {
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
	case STONEFLY_ACTION_NONE:
		break;
	}
}

