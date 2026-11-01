#ifndef STONEFLY_WM_H
#define STONEFLY_WM_H

#include <stdbool.h>

#include "config/hardcoded.h"
#include "xdg/xdg.h"

void stonefly_focus_toplevel(struct stonefly_toplevel *toplevel);
struct stonefly_toplevel *stonefly_focused_toplevel(struct stonefly_server *server);

/* Layout-box in output-layout coordinates (scene position + surface size). */
void stonefly_toplevel_layout_geom(struct stonefly_toplevel *toplevel,
		struct wlr_box *box);

/* Restore floating geometry saved before maximize/snap. */
void stonefly_toplevel_restore_saved(struct stonefly_toplevel *toplevel);

void stonefly_toplevel_set_max_edges(struct stonefly_toplevel *toplevel, bool want);
void stonefly_toplevel_toggle_max_edges(struct stonefly_toplevel *toplevel);
void stonefly_toplevel_snap(struct stonefly_toplevel *toplevel, enum stonefly_snap edge);
void stonefly_toplevel_unsnap(struct stonefly_toplevel *toplevel);
void stonefly_toplevel_minimize(struct stonefly_toplevel *toplevel);
void stonefly_toplevel_unminimize(struct stonefly_toplevel *toplevel);

void stonefly_run_action(struct stonefly_server *server, enum stonefly_action action);

#endif
