#include "wm/action_run.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <wayland-server-core.h>

#include "config/action.h"
#include "log/log.h"
#include "seat/seat.h"
#include "wm/arrange.h"
#include "wm/internal.h"
#include "wm/layout_sm.h"
#include "wm/scratchpad.h"
#include "wm/snap.h"
#include "wm/wm.h"
#include "wm/workspace.h"

struct sf_action_ctx {
	struct sf_server *server;
	struct sf_toplevel *win;
	const char *cmd;
	enum sf_action action;
};

typedef void (*sf_action_fn)(struct sf_action_ctx *ctx);

struct sf_action_slot {
	unsigned flags;
	sf_action_fn fn;
};

static void action_quit(struct sf_action_ctx *ctx) {
	wl_display_terminate(ctx->server->wl_display);
}

static void action_close(struct sf_action_ctx *ctx) {
	sf_close_toplevel(ctx->win);
}

static void action_focus_next(struct sf_action_ctx *ctx) {
	/* Tiling: walk arrange_tiles (layout order). Stacking: MRU list. */
	if (!sf_arrange_focus_cycle(ctx->server, false)) {
		sf_focus_cycle(ctx->server, false, false);
	}
}

static void action_focus_prev(struct sf_action_ctx *ctx) {
	if (!sf_arrange_focus_cycle(ctx->server, true)) {
		sf_focus_cycle(ctx->server, true, false);
	}
}

static void action_focus_next_all(struct sf_action_ctx *ctx) {
	sf_focus_cycle(ctx->server, false, true);
}

static void action_focus_prev_all(struct sf_action_ctx *ctx) {
	sf_focus_cycle(ctx->server, true, true);
}

static void action_maximize(struct sf_action_ctx *ctx) {
	sf_toplevel_set_max_gaps(ctx->win, true);
}

static void action_maximize_to_edges(struct sf_action_ctx *ctx) {
	sf_toplevel_set_max_edges(ctx->win, true);
}

static void action_unmaximize(struct sf_action_ctx *ctx) {
	if (sf_layout_sm_is_max_edges(&ctx->win->layout)) {
		sf_toplevel_set_max_edges(ctx->win, false);
	} else if (sf_layout_sm_is_max_gaps(&ctx->win->layout)) {
		sf_toplevel_set_max_gaps(ctx->win, false);
	}
}

static void action_toggle_maximize_to_edges(struct sf_action_ctx *ctx) {
	sf_toplevel_toggle_max_edges(ctx->win);
}

static void action_toggle_maximize(struct sf_action_ctx *ctx) {
	sf_toplevel_toggle_max_gaps(ctx->win);
}

static void action_minimize(struct sf_action_ctx *ctx) {
	sf_toplevel_minimize(ctx->win);
}

static void action_unminimize(struct sf_action_ctx *ctx) {
	sf_unminimize_most_recent(ctx->server);
}

static void action_snap_left(struct sf_action_ctx *ctx) {
	sf_toplevel_snap(ctx->win, SF_SNAP_LEFT);
}

static void action_snap_right(struct sf_action_ctx *ctx) {
	sf_toplevel_snap(ctx->win, SF_SNAP_RIGHT);
}

static void action_snap_top(struct sf_action_ctx *ctx) {
	sf_toplevel_snap(ctx->win, SF_SNAP_TOP);
}

static void action_snap_bottom(struct sf_action_ctx *ctx) {
	sf_toplevel_snap(ctx->win, SF_SNAP_BOTTOM);
}

static void action_unsnap(struct sf_action_ctx *ctx) {
	sf_toplevel_unsnap(ctx->win);
}

static void action_exec(struct sf_action_ctx *ctx) {
	SF_LOGD("exec", sf_str("cmd", ctx->cmd));
	sf_spawn_cmd(ctx->cmd);
}

static void action_scratch_toggle(struct sf_action_ctx *ctx) {
	const char *name = (ctx->cmd != NULL && ctx->cmd[0] != '\0') ? ctx->cmd : "term";
	sf_scratchpad_toggle(ctx->server, name);
}

static void action_scratch_claim(struct sf_action_ctx *ctx) {
	const char *name = (ctx->cmd != NULL && ctx->cmd[0] != '\0') ? ctx->cmd : "term";
	sf_scratchpad_claim_focused(ctx->server, name);
}

static void action_workspace_next(struct sf_action_ctx *ctx) {
	struct sf_output *output = sf_output_at_cursor(ctx->server);
	if (output != NULL) {
		sf_workspace_switch_offset(output, 1);
	}
}

static void action_workspace_prev(struct sf_action_ctx *ctx) {
	struct sf_output *output = sf_output_at_cursor(ctx->server);
	if (output != NULL) {
		sf_workspace_switch_offset(output, -1);
	}
}

static void action_workspace_switch(struct sf_action_ctx *ctx) {
	struct sf_output *output = sf_output_at_cursor(ctx->server);
	long v = (ctx->cmd != NULL) ? strtol(ctx->cmd, NULL, 10) : 0;
	int idx = (v > 0 && v <= (long)INT_MAX) ? (int)v : 0;
	if (output != NULL && idx > 0) {
		sf_workspace_switch_index(output, idx);
	}
}

static void action_move_to_workspace_next(struct sf_action_ctx *ctx) {
	sf_workspace_move_offset(ctx->win, 1, true);
}

static void action_move_to_workspace_prev(struct sf_action_ctx *ctx) {
	sf_workspace_move_offset(ctx->win, -1, true);
}

static void action_move_to_workspace(struct sf_action_ctx *ctx) {
	long v = (ctx->cmd != NULL) ? strtol(ctx->cmd, NULL, 10) : 0;
	int idx = (v > 0 && v <= (long)INT_MAX) ? (int)v : 0;
	if (idx > 0) {
		sf_workspace_move_index(ctx->win, idx, true);
	}
}

static void action_pointer_center(struct sf_action_ctx *ctx) {
	sf_pointer_center(ctx->server, ctx->win);
}

static void action_focus_monitor(struct sf_action_ctx *ctx) {
	sf_focus_monitor(ctx->server, ctx->cmd);
}

static void action_send_to_monitor(struct sf_action_ctx *ctx) {
	sf_send_to_monitor(ctx->server, ctx->win, ctx->cmd);
}

static void action_nudge(struct sf_action_ctx *ctx) {
	sf_toplevel_nudge(ctx->win, ctx->cmd);
}

static void action_resize_edge(struct sf_action_ctx *ctx) {
	sf_toplevel_resize_edge(ctx->win, ctx->cmd);
}

static void action_toggle_always_on_top(struct sf_action_ctx *ctx) {
	sf_toplevel_toggle_always_on_top(ctx->win);
}

static void action_always_on_top(struct sf_action_ctx *ctx) {
	bool on = false;
	if (strcmp(ctx->cmd, "1") == 0 || strcasecmp(ctx->cmd, "true") == 0 ||
	    strcasecmp(ctx->cmd, "on") == 0 || strcasecmp(ctx->cmd, "yes") == 0) {
		on = true;
	} else if (strcmp(ctx->cmd, "0") == 0 || strcasecmp(ctx->cmd, "false") == 0 ||
		   strcasecmp(ctx->cmd, "off") == 0 || strcasecmp(ctx->cmd, "no") == 0) {
		on = false;
	} else {
		SF_LOGW("always_on_top bad value", sf_str("cmd", ctx->cmd));
		return;
	}
	sf_toplevel_set_always_on_top(ctx->win, on);
}

static void action_set_fullscreen(struct sf_action_ctx *ctx) {
	sf_toplevel_set_fullscreen(ctx->win, true, NULL);
}

static void action_unset_fullscreen(struct sf_action_ctx *ctx) {
	sf_toplevel_set_fullscreen(ctx->win, false, NULL);
}

static void action_toggle_fullscreen(struct sf_action_ctx *ctx) {
	sf_toplevel_toggle_fullscreen(ctx->win);
}

static void action_arrange_cycle(struct sf_action_ctx *ctx) {
	int delta = 1;
	if (ctx->cmd != NULL && (strcmp(ctx->cmd, "prev") == 0 || strcmp(ctx->cmd, "-1") == 0)) {
		delta = -1;
	}
	sf_arrange_cycle(ctx->server, delta);
}

static void action_arrange_set(struct sf_action_ctx *ctx) {
	sf_arrange_set(ctx->server, ctx->cmd);
}

static void action_focus_dir(struct sf_action_ctx *ctx) {
	sf_arrange_focus_dir(ctx->server, ctx->cmd);
}

static void action_toggle_float(struct sf_action_ctx *ctx) {
	if (ctx->win == NULL) {
		return;
	}
	if (ctx->win->arrange_managed || ctx->win->arrange_floating ||
	    (ctx->win->workspace != NULL &&
	     sf_arrange_is_tiling(sf_workspace_arrange(ctx->win->workspace)))) {
		sf_arrange_toggle_float(ctx->win);
		return;
	}
	/* Stacking: drop snap/max to a free float. */
	if (sf_layout_sm_is_edged(&ctx->win->layout)) {
		sf_toplevel_layout_float(ctx->win, false);
	}
}

static void action_float(struct sf_action_ctx *ctx) {
	if (ctx->win == NULL) {
		return;
	}
	sf_arrange_float(ctx->win);
}

static void action_unfloat(struct sf_action_ctx *ctx) {
	if (ctx->win == NULL) {
		return;
	}
	sf_arrange_unfloat(ctx->win);
}

static void action_consume(struct sf_action_ctx *ctx) {
	sf_arrange_consume(ctx->server, ctx->cmd);
}

static void action_arrange_rotate(struct sf_action_ctx *ctx) {
	int delta = 1;
	if (ctx->cmd != NULL && (strcmp(ctx->cmd, "prev") == 0 || strcmp(ctx->cmd, "ccw") == 0 ||
				 strcmp(ctx->cmd, "-1") == 0)) {
		delta = -1;
	}
	sf_arrange_rotate(ctx->server, delta);
}

/*
 * Verb catalog: one slot per enum value. Flags encode the shared protocol
 * (need window / need cmd) so arms stay free of duplicated guards.
 */
static const struct sf_action_slot sf_action_slots[SF_ACTION_COUNT] = {
	[SF_ACTION_NONE] = {0, NULL},
	[SF_ACTION_QUIT] = {0, action_quit},
	[SF_ACTION_CLOSE] = {0, action_close},
	[SF_ACTION_FOCUS_NEXT] = {0, action_focus_next},
	[SF_ACTION_FOCUS_PREV] = {0, action_focus_prev},
	[SF_ACTION_FOCUS_NEXT_ALL] = {0, action_focus_next_all},
	[SF_ACTION_FOCUS_PREV_ALL] = {0, action_focus_prev_all},
	[SF_ACTION_MAXIMIZE] = {SF_ACTION_F_WIN, action_maximize},
	[SF_ACTION_MAXIMIZE_TO_EDGES] = {SF_ACTION_F_WIN, action_maximize_to_edges},
	[SF_ACTION_UNMAXIMIZE] = {SF_ACTION_F_WIN, action_unmaximize},
	[SF_ACTION_TOGGLE_MAXIMIZE] = {SF_ACTION_F_WIN, action_toggle_maximize},
	[SF_ACTION_TOGGLE_MAXIMIZE_TO_EDGES] = {SF_ACTION_F_WIN, action_toggle_maximize_to_edges},
	[SF_ACTION_MINIMIZE] = {SF_ACTION_F_WIN, action_minimize},
	[SF_ACTION_UNMINIMIZE] = {0, action_unminimize},
	[SF_ACTION_SNAP_LEFT] = {SF_ACTION_F_WIN, action_snap_left},
	[SF_ACTION_SNAP_RIGHT] = {SF_ACTION_F_WIN, action_snap_right},
	[SF_ACTION_SNAP_TOP] = {SF_ACTION_F_WIN, action_snap_top},
	[SF_ACTION_SNAP_BOTTOM] = {SF_ACTION_F_WIN, action_snap_bottom},
	[SF_ACTION_UNSNAP] = {SF_ACTION_F_WIN, action_unsnap},
	[SF_ACTION_EXEC] = {SF_ACTION_F_CMD, action_exec},
	[SF_ACTION_SCRATCH_TOGGLE] = {0, action_scratch_toggle},
	[SF_ACTION_SCRATCH_CLAIM] = {0, action_scratch_claim},
	[SF_ACTION_WORKSPACE_NEXT] = {0, action_workspace_next},
	[SF_ACTION_WORKSPACE_PREV] = {0, action_workspace_prev},
	[SF_ACTION_WORKSPACE_SWITCH] = {0, action_workspace_switch},
	[SF_ACTION_MOVE_TO_WORKSPACE_NEXT] = {SF_ACTION_F_WIN, action_move_to_workspace_next},
	[SF_ACTION_MOVE_TO_WORKSPACE_PREV] = {SF_ACTION_F_WIN, action_move_to_workspace_prev},
	[SF_ACTION_MOVE_TO_WORKSPACE] = {SF_ACTION_F_WIN, action_move_to_workspace},
	[SF_ACTION_POINTER_CENTER] = {0, action_pointer_center},
	[SF_ACTION_FOCUS_MONITOR] = {SF_ACTION_F_CMD, action_focus_monitor},
	[SF_ACTION_SEND_TO_MONITOR] = {SF_ACTION_F_WIN | SF_ACTION_F_CMD, action_send_to_monitor},
	[SF_ACTION_NUDGE] = {SF_ACTION_F_WIN | SF_ACTION_F_CMD, action_nudge},
	[SF_ACTION_RESIZE_EDGE] = {SF_ACTION_F_WIN | SF_ACTION_F_CMD, action_resize_edge},
	[SF_ACTION_TOGGLE_ALWAYS_ON_TOP] = {SF_ACTION_F_WIN, action_toggle_always_on_top},
	[SF_ACTION_ALWAYS_ON_TOP] = {SF_ACTION_F_WIN | SF_ACTION_F_CMD, action_always_on_top},
	[SF_ACTION_SET_FULLSCREEN] = {SF_ACTION_F_WIN, action_set_fullscreen},
	[SF_ACTION_UNSET_FULLSCREEN] = {SF_ACTION_F_WIN, action_unset_fullscreen},
	[SF_ACTION_TOGGLE_FULLSCREEN] = {SF_ACTION_F_WIN, action_toggle_fullscreen},
	[SF_ACTION_ARRANGE_CYCLE] = {0, action_arrange_cycle},
	[SF_ACTION_ARRANGE_SET] = {SF_ACTION_F_CMD, action_arrange_set},
	[SF_ACTION_FOCUS_DIR] = {SF_ACTION_F_CMD, action_focus_dir},
	[SF_ACTION_TOGGLE_FLOAT] = {SF_ACTION_F_WIN, action_toggle_float},
	[SF_ACTION_FLOAT] = {SF_ACTION_F_WIN, action_float},
	[SF_ACTION_UNFLOAT] = {SF_ACTION_F_WIN, action_unfloat},
	[SF_ACTION_CONSUME] = {0, action_consume},
	[SF_ACTION_ARRANGE_ROTATE] = {0, action_arrange_rotate},
};

_Static_assert(sizeof(sf_action_slots) / sizeof(sf_action_slots[0]) == (size_t)SF_ACTION_COUNT,
	       "action slot table must match SF_ACTION_COUNT");

unsigned sf_action_run_flags(enum sf_action action) {
	if ((unsigned)action >= (unsigned)SF_ACTION_COUNT) {
		return 0;
	}
	return sf_action_slots[action].flags;
}

bool sf_action_run_has_handler(enum sf_action action) {
	if ((unsigned)action >= (unsigned)SF_ACTION_COUNT) {
		return false;
	}
	return sf_action_slots[action].fn != NULL;
}

void sf_run_action(struct sf_server *server, struct sf_toplevel *target, enum sf_action action,
		   const char *cmd) {
	if (server == NULL) {
		return;
	}
	if ((unsigned)action >= (unsigned)SF_ACTION_COUNT) {
		SF_LOGW("unknown action", sf_i64("action", (int64_t)action));
		return;
	}
	const struct sf_action_slot *slot = &sf_action_slots[action];
	if (slot->fn == NULL) {
		return;
	}

	struct sf_toplevel *win = target != NULL ? target : sf_focused_toplevel(server);
	if ((slot->flags & SF_ACTION_F_WIN) && win == NULL) {
		return;
	}
	if ((slot->flags & SF_ACTION_F_CMD) && (cmd == NULL || cmd[0] == '\0')) {
		SF_LOGW("action missing cmd", sf_str("action", sf_action_name(action)));
		return;
	}

	struct sf_action_ctx ctx = {
		.server = server,
		.win = win,
		.cmd = cmd,
		.action = action,
	};
	slot->fn(&ctx);
}

void sf_run_actions(struct sf_server *server, struct sf_toplevel *target,
		    const struct sf_action_step *steps, size_t n) {
	if (steps == NULL) {
		return;
	}
	for (size_t i = 0; i < n; i++) {
		sf_run_action(server, target, steps[i].action, steps[i].cmd);
	}
}
