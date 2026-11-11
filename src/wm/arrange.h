/* Workspace arrange engines: stacking / columns.
 * Distinct from per-window layout_sm (float/max/snap/fullscreen). */
#ifndef SF_WM_ARRANGE_H
#define SF_WM_ARRANGE_H

#include <stdbool.h>

#include "wm/arrange_math.h"

struct sf_server;
struct sf_toplevel;
struct sf_workspace;
struct sf_output;

/* Inheritance: workspace override → output → global. */
enum sf_arrange sf_workspace_arrange(const struct sf_workspace *ws);
void sf_workspace_apply_arrange(struct sf_workspace *ws);

/* Coalesce apply onto the display idle handler (map / exclusive-zone storms). */
void sf_arrange_schedule(struct sf_workspace *ws);

/* Cancel a pending idle and apply now (pack before tiled appear slide). */
void sf_arrange_flush(struct sf_workspace *ws);

/* Cycle active workspace arrange. delta +1 = next, -1 = prev.
 * Order: stacking ↔ columns. */
void sf_arrange_cycle(struct sf_server *server, int delta);

/* Rotate tile-set order on the active workspace. delta +1 = clockwise
 * (last→first); -1 = counter-clockwise (first→last). Focus stays put. */
void sf_arrange_rotate(struct sf_server *server, int delta);

/* Set active workspace arrange by name (stacking|columns; old master/dwindle/
 * monocle names alias to columns). */
void sf_arrange_set(struct sf_server *server, const char *name);

/* Join eligible windows into the tile set when entering a tiling arrange. */
void sf_arrange_join_workspace(struct sf_workspace *ws);

/* Enroll one window into its workspace tile set (map / send / unfloat).
 * Skips arrange_floating windows. */
void sf_arrange_join_toplevel(struct sf_toplevel *toplevel);

/* Leave the tile set as an explicit float. Arms geom-regain hold.
 * keep_current_size: true = user float (keep slot geom); false = prefer
 * arrange_float_box saved at join/place (rule float after lone-column pack). */
void sf_arrange_break_out(struct sf_toplevel *toplevel, bool keep_current_size);

void sf_arrange_float(struct sf_toplevel *toplevel);
/* Rule :float — restore pre-tile size when leaving the strip. */
void sf_arrange_float_from_rule(struct sf_toplevel *toplevel);
void sf_arrange_unfloat(struct sf_toplevel *toplevel);
/* Snapshot current geom as the float restore box (after SoftPlace/cascade). */
void sf_arrange_remember_float_box(struct sf_toplevel *toplevel);
/* Toggle tile membership: tiled→float, or float→rejoin on a tiling workspace. */
void sf_arrange_toggle_float(struct sf_toplevel *toplevel);

/* Columns: stack focused into neighbor column, or expel from a multi-window
 * column when no neighbor exists (umbriel consume-or-expel). dir: left|right. */
void sf_arrange_consume(struct sf_server *server, const char *dir);

/* Drop from the workspace tile-order list (unmap / destroy / leave). */
void sf_arrange_unlink(struct sf_toplevel *toplevel);

/* Tile-set membership predicate. */
bool sf_arrange_toplevel_managed(const struct sf_toplevel *toplevel);

/* Keybind filter: UNSET=any, TILING=any tiling, else exact name match. */
bool sf_arrange_bind_matches(enum sf_arrange filter, enum sf_arrange current);

/* Focus nearest managed peer in direction. cmd=left|right|up|down. */
void sf_arrange_focus_dir(struct sf_server *server, const char *dir);

/* Tab cycle along columns→rows (cw = next; same ring as arrange_rotate on columns).
 * Returns false when not tiling / fewer than 2 tiles — caller falls back to MRU. */
bool sf_arrange_focus_cycle(struct sf_server *server, bool backward);

/* Super+LMB: drag follows pointer; cell mid swaps, seams/gaps insert with live
 * make-room reflow, strip gap = new column. */
void sf_arrange_swap_begin(struct sf_server *server, struct sf_toplevel *toplevel);
void sf_arrange_swap_motion(struct sf_server *server);
void sf_arrange_swap_finish(struct sf_server *server);
/* Cancel without commit: restore lifted window to its home cell. */
void sf_arrange_swap_abort(struct sf_server *server);

/* Super+RMB tile resize: adjust that column's width_frac while columns. */
void sf_arrange_resize_begin(struct sf_server *server, struct sf_toplevel *toplevel);
void sf_arrange_resize_motion(struct sf_server *server);
void sf_arrange_resize_finish(struct sf_server *server);

#endif
