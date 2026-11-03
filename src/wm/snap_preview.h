#ifndef STONEFLY_SNAP_PREVIEW_H
#define STONEFLY_SNAP_PREVIEW_H

#include <stdbool.h>

struct stonefly_server;
struct wlr_box;
struct wlr_scene_tree;
struct wlr_scene_rect;

struct stonefly_snap_preview {
	struct stonefly_server *server;
	struct wlr_scene_tree *tree;
	struct wlr_scene_rect *fill;
	struct wlr_scene_rect *rim;
	bool visible;
	int edge; /* enum stonefly_snap */
};

void stonefly_snap_preview_init(struct stonefly_server *server);
void stonefly_snap_preview_finish(struct stonefly_server *server);

void stonefly_snap_preview_show(struct stonefly_server *server, int edge,
				const struct wlr_box *usable);
void stonefly_snap_preview_hide(struct stonefly_server *server);

#endif
