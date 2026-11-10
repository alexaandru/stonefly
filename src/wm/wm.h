#ifndef SF_WM_H
#define SF_WM_H

#include <stdbool.h>

#include "config/config.h"
#include "toplevel/toplevel.h"
#include "wm/geom.h"

struct sf_output;
struct wlr_output;

void sf_focus_toplevel(struct sf_toplevel *toplevel);
struct sf_toplevel *sf_focused_toplevel(struct sf_server *server);

/* Raise toplevel, then re-stack always-on-top (+ hide slides above pins). */
void sf_toplevel_raise(struct sf_toplevel *toplevel);
void sf_pinned_restack(struct sf_server *server);
void sf_toplevel_set_always_on_top(struct sf_toplevel *toplevel, bool on);
void sf_toplevel_toggle_always_on_top(struct sf_toplevel *toplevel);

/* Focus the top of the stacking list (most recently focused visible),
 * or clear keyboard focus when none remain. Peeble Focus(nil) equivalent. */
void sf_focus_top(struct sf_server *server);

/* Resolve a head: connector name, left|right|up|down, next|prev|current.
 * from may be NULL (falls back to cursor / first live). */
struct sf_output *sf_output_resolve(struct sf_server *server, struct sf_output *from,
				    const char *target);
/* Warp to target head + focus topmost window there (cursor_follows_focus). */
void sf_focus_monitor(struct sf_server *server, const char *target);
/* Move toplevel onto target head (active workspace); focus + pointer center. */
void sf_send_to_monitor(struct sf_server *server, struct sf_toplevel *toplevel, const char *target);
void sf_toplevel_place_on_output(struct sf_toplevel *toplevel, struct sf_output *output);

/* Pixel-step move / resize (dir: left|right|up|down). No-op when max/snap/fs. */
void sf_toplevel_nudge(struct sf_toplevel *toplevel, const char *dir);
void sf_toplevel_resize_edge(struct sf_toplevel *toplevel, const char *dir);

/* Layout-box in output-layout coordinates (scene position + surface size). */
void sf_toplevel_layout_geom(struct sf_toplevel *toplevel, struct wlr_box *box);

/* Restore floating geometry saved before maximize/snap. */
void sf_toplevel_restore_saved(struct sf_toplevel *toplevel);

/* Usable output box for a toplevel (layout coords, exclusive zones applied). */
bool sf_toplevel_output_box(struct sf_toplevel *toplevel, struct wlr_box *box);

/* Re-apply max/snap and EnsureOnScreen floaters after exclusive-zone change. */
void sf_wm_relayout_output(struct sf_output *output);

/* Pull a floating toplevel into the gap-inset usable area (gap-max if oversized). */
void sf_toplevel_ensure_on_screen(struct sf_toplevel *toplevel);

void sf_toplevel_set_max_edges(struct sf_toplevel *toplevel, bool want);
void sf_toplevel_toggle_max_edges(struct sf_toplevel *toplevel);
void sf_toplevel_set_max_gaps(struct sf_toplevel *toplevel, bool want);
void sf_toplevel_toggle_max_gaps(struct sf_toplevel *toplevel);
void sf_toplevel_snap(struct sf_toplevel *toplevel, enum sf_snap edge);
/* Half-snap into an explicit usable box (drag release uses pointer head). */
void sf_toplevel_snap_in(struct sf_toplevel *toplevel, enum sf_snap edge,
			 const struct wlr_box *usable);
/* Remember floating geom as PreSnap if none is stored yet. */
void sf_toplevel_capture_geom(struct sf_toplevel *toplevel, const struct wlr_box *box);
/* Apply saved size at (x,y) without clearing PreMax (PrepareDrag). */
void sf_toplevel_apply_saved_at(struct sf_toplevel *toplevel, int x, int y);
void sf_toplevel_unsnap(struct sf_toplevel *toplevel);

/* Leave max/snap/fs without restoring. clear_saved drops PreMax (window_rule). */
void sf_toplevel_layout_float(struct sf_toplevel *toplevel, bool clear_saved);
/* window_rule float: layout_float(true). */
void sf_toplevel_float_for_resize(struct sf_toplevel *toplevel);

/* Reconcile layout mode with current geometry (resize / client configure). */
void sf_toplevel_layout_observe(struct sf_toplevel *toplevel);
/* Same, with an explicit box (interactive resize proposes size before commit). */
void sf_toplevel_layout_observe_box(struct sf_toplevel *toplevel, const struct wlr_box *current);

/* How a window is hidden: LIFO unnamed stack (Ctrl+Esc / CSD / foreign) vs
 * named scratchpad (off that stack). Both set minimized + foreign state. */
enum sf_hide_kind {
	SF_HIDE_LIFO = 0,
	SF_HIDE_SCRATCH,
};

/* Sole place for hide side effects: flag, list membership, scene, foreign,
 * defocus + focus_top when the hidden window had focus. */
void sf_toplevel_apply_hidden(struct sf_toplevel *toplevel, enum sf_hide_kind kind);
/* Clear hidden state (scene + foreign). If activate, also focus_toplevel. */
void sf_toplevel_apply_shown(struct sf_toplevel *toplevel, bool activate);

void sf_toplevel_minimize(struct sf_toplevel *toplevel);
void sf_toplevel_unminimize(struct sf_toplevel *toplevel);

/* Fullscreen on hint output, or the output under the window when hint is NULL. */
void sf_toplevel_set_fullscreen(struct sf_toplevel *toplevel, bool want, struct wlr_output *hint);

/* Peeble-style cascade origin for a newly mapped floating toplevel. */
void sf_toplevel_place_cascade(struct sf_toplevel *toplevel);

/* Map placement: center on visible parent when set_parent, else cascade.
 * No size-based "dialog" guessing. Keeps place managed for late parent/size. */
void sf_toplevel_place_new(struct sf_toplevel *toplevel);

/* Re-run parent centering while place is managed (late set_parent / size). */
void sf_toplevel_place_transient(struct sf_toplevel *toplevel);

/* Stop WM auto-placement (user move/resize/max/snap owns geometry). */
void sf_toplevel_clear_auto_place(struct sf_toplevel *toplevel);

/* SoftPlace: center no longer possible — drop soft; release hold if needed. */
void sf_toplevel_place_drop_soft(struct sf_toplevel *toplevel);
/* SoftPlace: center rule matched — reassert managed + soft. */
void sf_toplevel_place_soft_center(struct sf_toplevel *toplevel);

/* SoftPlace appear-hold via place_sm, or slide/show immediately. */
void sf_toplevel_arm_appear(struct sf_toplevel *toplevel);

/* Release appear-hold once SoftPlace settled; no-op when not holding. */
void sf_toplevel_try_appear(struct sf_toplevel *toplevel);

/* cmd: EXEC string, or scratchpad name for SCRATCH_TOGGLE/CLAIM; else NULL.
 * target NULL → focused window (binds/taps); non-NULL → that window (rules). */
void sf_run_action(struct sf_server *server, struct sf_toplevel *target, enum sf_action action,
		   const char *cmd);
void sf_run_actions(struct sf_server *server, struct sf_toplevel *target,
		    const struct sf_action_step *steps, size_t n);

#endif
