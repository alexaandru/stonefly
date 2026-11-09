#ifndef SF_WM_H
#define SF_WM_H

#include <stdbool.h>

#include "config/config.h"
#include "toplevel/toplevel.h"

struct sf_output;
struct wlr_output;

void sf_focus_toplevel(struct sf_toplevel *toplevel);
struct sf_toplevel *sf_focused_toplevel(struct sf_server *server);

/* Focus the top of the stacking list (most recently focused visible),
 * or clear keyboard focus when none remain. Peeble Focus(nil) equivalent. */
void sf_focus_top(struct sf_server *server);

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

/* Inset box by gap on every side (clamped). */
void sf_box_inset_gap(struct wlr_box *box, int gap);

void sf_toplevel_set_max_edges(struct sf_toplevel *toplevel, bool want);
void sf_toplevel_toggle_max_edges(struct sf_toplevel *toplevel);
void sf_toplevel_set_max_gaps(struct sf_toplevel *toplevel, bool want);
void sf_toplevel_toggle_max_gaps(struct sf_toplevel *toplevel);
void sf_toplevel_snap(struct sf_toplevel *toplevel, enum sf_snap edge);
/* Half-snap into an explicit usable box (drag release uses pointer head). */
void sf_toplevel_snap_in(struct sf_toplevel *toplevel, enum sf_snap edge,
			 const struct wlr_box *usable);
/* Peeble snapBorderBox geometry (layout coords). */
struct wlr_box sf_wm_snap_box(enum sf_snap edge, const struct wlr_box *usable, int gap);
/* Remember floating geom as PreSnap if none is stored yet. */
void sf_toplevel_capture_geom(struct sf_toplevel *toplevel, const struct wlr_box *box);
/* Apply saved size at (x,y) without clearing has_saved_geom (PrepareDrag). */
void sf_toplevel_apply_saved_at(struct sf_toplevel *toplevel, int x, int y);
void sf_toplevel_unsnap(struct sf_toplevel *toplevel);

/* Drop gap-max / edge-max / snap without restoring PreMax (window_rule float).
 * Interactive resize does not call this — undragged edges stay put. */
void sf_toplevel_float_for_resize(struct sf_toplevel *toplevel);

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
 * No size-based "dialog" guessing. Keeps auto_place for late parent/size. */
void sf_toplevel_place_new(struct sf_toplevel *toplevel);

/* Re-run parent centering while auto_place (late set_parent / size commit). */
void sf_toplevel_place_transient(struct sf_toplevel *toplevel);

/* Stop WM auto-placement (user move/resize/max/snap owns geometry). */
void sf_toplevel_clear_auto_place(struct sf_toplevel *toplevel);

/* cmd: EXEC string, or scratchpad name for SCRATCH_TOGGLE/CLAIM; else NULL.
 * target NULL → focused window (binds/taps); non-NULL → that window (rules). */
void sf_run_action(struct sf_server *server, struct sf_toplevel *target, enum sf_action action,
		   const char *cmd);
void sf_run_actions(struct sf_server *server, struct sf_toplevel *target,
		    const struct sf_action_step *steps, size_t n);

#endif
