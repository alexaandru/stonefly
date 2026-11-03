#ifndef STONEFLY_WORKSPACE_H
#define STONEFLY_WORKSPACE_H

#include <stdbool.h>
#include <wayland-server-core.h>

struct stonefly_server;
struct stonefly_output;
struct stonefly_toplevel;
struct wlr_ext_workspace_handle_v1;
struct wlr_ext_workspace_group_handle_v1;
struct wlr_scene_tree;

/* Layout inheritance: workspace override → output → global.
 * Only STACKING is implemented; COLUMNS/TILE reserved for later. */
enum stonefly_layout {
	STONEFLY_LAYOUT_UNSET = -1,
	STONEFLY_LAYOUT_STACKING = 0,
	STONEFLY_LAYOUT_COLUMNS = 1,
	STONEFLY_LAYOUT_TILE = 2,
};

struct stonefly_workspace {
	struct wl_list link; /* stonefly_output.workspaces */
	struct stonefly_output *output;
	int index;		     /* 0-based in strip */
	char name[16];		     /* "1", "2", … */
	enum stonefly_layout layout; /* UNSET = inherit */
	struct wlr_scene_tree *tree;
	struct wlr_ext_workspace_handle_v1 *ext;
	char ext_id[64];
};

void stonefly_workspace_setup(struct stonefly_server *server);
void stonefly_workspace_finish(struct stonefly_server *server);

/* Create the per-output strip (one empty workspace) + ext group. */
void stonefly_workspace_output_init(struct stonefly_output *output);
void stonefly_workspace_output_finish(struct stonefly_output *output);

enum stonefly_layout stonefly_layout_parse(const char *s);
const char *stonefly_layout_name(enum stonefly_layout layout);
enum stonefly_layout stonefly_workspace_layout(const struct stonefly_workspace *ws);
void stonefly_workspace_apply_layout(struct stonefly_workspace *ws);

struct stonefly_output *stonefly_output_at_cursor(struct stonefly_server *server);
struct stonefly_output *stonefly_output_for_toplevel(struct stonefly_toplevel *toplevel);
struct stonefly_workspace *stonefly_workspace_active(struct stonefly_output *output);

/* True if toplevel is on an active workspace (or is a scratchpad). */
bool stonefly_toplevel_workspace_visible(const struct stonefly_toplevel *toplevel);

/* Assign a normal window to a workspace (reparents scene). Scratchpads use
 * stonefly_workspace_detach_scratchpad instead. */
void stonefly_workspace_assign(struct stonefly_toplevel *toplevel, struct stonefly_workspace *ws);
void stonefly_workspace_detach_scratchpad(struct stonefly_toplevel *toplevel);

/* Compact strip after membership changes; ensure one trailing empty. */
void stonefly_workspace_compact(struct stonefly_output *output);

void stonefly_workspace_switch(struct stonefly_output *output, struct stonefly_workspace *ws);
void stonefly_workspace_switch_offset(struct stonefly_output *output, int delta);
/* 1-based name/index into the strip; no-op if out of range. */
bool stonefly_workspace_switch_index(struct stonefly_output *output, int one_based);

void stonefly_workspace_move_toplevel(struct stonefly_toplevel *toplevel,
				      struct stonefly_workspace *ws, bool follow);
void stonefly_workspace_move_offset(struct stonefly_toplevel *toplevel, int delta, bool follow);
bool stonefly_workspace_move_index(struct stonefly_toplevel *toplevel, int one_based, bool follow);

/* Focus helper: switch to the window's workspace first when needed. */
void stonefly_workspace_reveal_toplevel(struct stonefly_toplevel *toplevel);

#endif
