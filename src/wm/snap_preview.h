#ifndef SF_SNAP_PREVIEW_H
#define SF_SNAP_PREVIEW_H

#include <stdbool.h>

#include "wm/snap.h"

struct sf_server;
struct wlr_box;
struct wlr_scene_tree;
struct wlr_scene_rect;

struct sf_snap_preview {
	struct sf_server *server;
	struct wlr_scene_tree *tree;
	struct wlr_scene_rect *fill;
	struct wlr_scene_rect *rim;
	bool visible;
	enum sf_snap edge;
};

void sf_snap_preview_init(struct sf_server *server);
void sf_snap_preview_finish(struct sf_server *server);

void sf_snap_preview_show(struct sf_server *server, enum sf_snap edge,
			  const struct wlr_box *usable);
/* Highlight an arbitrary layout box (tile-swap drop target). */
void sf_snap_preview_show_box(struct sf_server *server, const struct wlr_box *box);
void sf_snap_preview_hide(struct sf_server *server);

#endif
