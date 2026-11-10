#ifndef SF_WORKSPACE_H
#define SF_WORKSPACE_H

#include <stdbool.h>
#include <time.h>
#include <wayland-server-core.h>

struct sf_server;
struct sf_output;
struct sf_toplevel;
struct wlr_ext_workspace_handle_v1;
struct wlr_ext_workspace_group_handle_v1;
struct wlr_scene_tree;

/* Layout inheritance: workspace override → output → global.
 * Only STACKING is implemented; COLUMNS/TILE reserved for later. */
enum sf_layout {
	SF_LAYOUT_UNSET = -1,
	SF_LAYOUT_STACKING = 0,
	SF_LAYOUT_COLUMNS = 1,
	SF_LAYOUT_TILE = 2,
};

struct sf_workspace {
	struct wl_list link; /* sf_output.workspaces */
	struct sf_output *output;
	int index;	       /* 0-based in strip */
	char name[16];	       /* "1", "2", … */
	enum sf_layout layout; /* UNSET = inherit */
	struct wlr_scene_tree *tree;
	struct wlr_ext_workspace_handle_v1 *ext;
	char ext_id[64];
};

void sf_workspace_setup(struct sf_server *server);
void sf_workspace_finish(struct sf_server *server);

/* Create the per-output strip (one empty workspace) + ext group. */
void sf_workspace_output_init(struct sf_output *output);
void sf_workspace_output_finish(struct sf_output *output);

enum sf_layout sf_layout_parse(const char *s);
const char *sf_layout_name(enum sf_layout layout);
enum sf_layout sf_workspace_layout(const struct sf_workspace *ws);
void sf_workspace_apply_layout(struct sf_workspace *ws);

struct sf_output *sf_output_at_cursor(struct sf_server *server);
struct sf_output *sf_output_for_toplevel(struct sf_toplevel *toplevel);
struct sf_workspace *sf_workspace_active(struct sf_output *output);

/* True if toplevel is on an active workspace (or is a scratchpad). */
bool sf_toplevel_workspace_visible(const struct sf_toplevel *toplevel);

/* Assign a normal window to a workspace (reparents scene). Scratchpads use
 * sf_workspace_detach_scratchpad instead. */
void sf_workspace_assign(struct sf_toplevel *toplevel, struct sf_workspace *ws);
void sf_workspace_detach_scratchpad(struct sf_toplevel *toplevel);

/* Compact strip after membership changes; ensure one trailing empty. */
void sf_workspace_compact(struct sf_output *output);

void sf_workspace_switch(struct sf_output *output, struct sf_workspace *ws);
void sf_workspace_switch_offset(struct sf_output *output, int delta);
/* 1-based name/index into the strip; no-op if out of range. */
bool sf_workspace_switch_index(struct sf_output *output, int one_based);

void sf_workspace_move_toplevel(struct sf_toplevel *toplevel, struct sf_workspace *ws, bool follow);
void sf_workspace_move_offset(struct sf_toplevel *toplevel, int delta, bool follow);
bool sf_workspace_move_index(struct sf_toplevel *toplevel, int one_based, bool follow);

/* Focus helper: switch to the window's workspace first when needed. */
void sf_workspace_reveal_toplevel(struct sf_toplevel *toplevel);

/* Advance in-flight workspace strip slides (from sf_anim_tick). */
void sf_workspace_anim_tick(struct sf_server *server, const struct timespec *now);
bool sf_workspace_anim_busy(struct sf_server *server);

#endif
