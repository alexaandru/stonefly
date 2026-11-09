#ifndef STONEFLY_WM_H
#define STONEFLY_WM_H

#include <stdbool.h>

#include "config/config.h"
#include "xdg/xdg.h"

struct stonefly_output;
struct wlr_output;

void stonefly_focus_toplevel(struct stonefly_toplevel *toplevel);
struct stonefly_toplevel *stonefly_focused_toplevel(struct stonefly_server *server);

/* Focus the top of the stacking list (most recently focused visible),
 * or clear keyboard focus when none remain. Peeble Focus(nil) equivalent. */
void stonefly_focus_top(struct stonefly_server *server);

/* Layout-box in output-layout coordinates (scene position + surface size). */
void stonefly_toplevel_layout_geom(struct stonefly_toplevel *toplevel, struct wlr_box *box);

/* Restore floating geometry saved before maximize/snap. */
void stonefly_toplevel_restore_saved(struct stonefly_toplevel *toplevel);

/* Usable output box for a toplevel (layout coords, exclusive zones applied). */
bool stonefly_toplevel_output_box(struct stonefly_toplevel *toplevel, struct wlr_box *box);

/* Re-apply max/snap and EnsureOnScreen floaters after exclusive-zone change. */
void stonefly_wm_relayout_output(struct stonefly_output *output);

/* Pull a floating toplevel into the gap-inset usable area (gap-max if oversized). */
void stonefly_toplevel_ensure_on_screen(struct stonefly_toplevel *toplevel);

/* Inset box by gap on every side (clamped). */
void stonefly_box_inset_gap(struct wlr_box *box, int gap);

void stonefly_toplevel_set_max_edges(struct stonefly_toplevel *toplevel, bool want);
void stonefly_toplevel_toggle_max_edges(struct stonefly_toplevel *toplevel);
void stonefly_toplevel_set_max_gaps(struct stonefly_toplevel *toplevel, bool want);
void stonefly_toplevel_toggle_max_gaps(struct stonefly_toplevel *toplevel);
void stonefly_toplevel_snap(struct stonefly_toplevel *toplevel, enum stonefly_snap edge);
/* Half-snap into an explicit usable box (drag release uses pointer head). */
void stonefly_toplevel_snap_in(struct stonefly_toplevel *toplevel, enum stonefly_snap edge,
			       const struct wlr_box *usable);
/* Peeble snapBorderBox geometry (layout coords). */
struct wlr_box stonefly_wm_snap_box(enum stonefly_snap edge, const struct wlr_box *usable, int gap);
/* Remember floating geom as PreSnap if none is stored yet. */
void stonefly_toplevel_capture_geom(struct stonefly_toplevel *toplevel, const struct wlr_box *box);
/* Apply saved size at (x,y) without clearing has_saved_geom (PrepareDrag). */
void stonefly_toplevel_apply_saved_at(struct stonefly_toplevel *toplevel, int x, int y);
void stonefly_toplevel_unsnap(struct stonefly_toplevel *toplevel);

/* Drop gap-max / edge-max / snap without restoring PreMax (window_rule float).
 * Interactive resize does not call this — undragged edges stay put. */
void stonefly_toplevel_float_for_resize(struct stonefly_toplevel *toplevel);

/* How a window is hidden: LIFO unnamed stack (Ctrl+Esc / CSD / foreign) vs
 * named scratchpad (off that stack). Both set minimized + foreign state. */
enum stonefly_hide_kind {
	STONEFLY_HIDE_LIFO = 0,
	STONEFLY_HIDE_SCRATCH,
};

/* Sole place for hide side effects: flag, list membership, scene, foreign,
 * defocus + focus_top when the hidden window had focus. */
void stonefly_toplevel_apply_hidden(struct stonefly_toplevel *toplevel,
				    enum stonefly_hide_kind kind);
/* Clear hidden state (scene + foreign). If activate, also focus_toplevel. */
void stonefly_toplevel_apply_shown(struct stonefly_toplevel *toplevel, bool activate);

void stonefly_toplevel_minimize(struct stonefly_toplevel *toplevel);
void stonefly_toplevel_unminimize(struct stonefly_toplevel *toplevel);

/* Fullscreen on hint output, or the output under the window when hint is NULL. */
void stonefly_toplevel_set_fullscreen(struct stonefly_toplevel *toplevel, bool want,
				      struct wlr_output *hint);

/* Peeble-style cascade origin for a newly mapped floating toplevel. */
void stonefly_toplevel_place_cascade(struct stonefly_toplevel *toplevel);

/* Map placement: center on visible parent when set_parent, else cascade.
 * No size-based "dialog" guessing. Keeps auto_place for late parent/size. */
void stonefly_toplevel_place_new(struct stonefly_toplevel *toplevel);

/* Re-run parent centering while auto_place (late set_parent / size commit). */
void stonefly_toplevel_place_transient(struct stonefly_toplevel *toplevel);

/* Stop WM auto-placement (user move/resize/max/snap owns geometry). */
void stonefly_toplevel_clear_auto_place(struct stonefly_toplevel *toplevel);

/* cmd: EXEC string, or scratchpad name for SCRATCH_TOGGLE/CLAIM; else NULL. */
void stonefly_run_action(struct stonefly_server *server, enum stonefly_action action,
			 const char *cmd);

#endif
