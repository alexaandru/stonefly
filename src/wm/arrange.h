/* Workspace arrange engines: stacking / columns / master / dwindle / monocle.
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

/* Cycle active workspace arrange. delta +1 = next, -1 = prev.
 * Order: stacking → columns → master → dwindle → monocle → … */
void sf_arrange_cycle(struct sf_server *server, int delta);

/* Set active workspace arrange by name (stacking/columns/master|tile/…). */
void sf_arrange_set(struct sf_server *server, const char *name);

/* Join eligible windows into the tile set when entering a tiling arrange. */
void sf_arrange_join_workspace(struct sf_workspace *ws);

/* Leave the tile set (drag break / float rule). Arms geom-regain hold. */
void sf_arrange_break_out(struct sf_toplevel *toplevel);

/* Tile-set membership predicate. */
bool sf_arrange_toplevel_managed(const struct sf_toplevel *toplevel);

#endif
