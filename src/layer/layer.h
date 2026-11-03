#ifndef STONEFLY_LAYER_H
#define STONEFLY_LAYER_H

#include <stdbool.h>
#include <wayland-server-core.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/util/box.h>

#include "server/server.h"

struct stonefly_output;

struct stonefly_layer_surface {
	enum stonefly_node_kind kind; /* STONEFLY_NODE_LAYER */
	struct wl_list link;
	struct stonefly_server *server;
	struct stonefly_output *output;
	struct wlr_layer_surface_v1 *layer_surface;
	struct wlr_scene_layer_surface_v1 *scene_layer;
	struct wlr_scene_tree *scene;
	struct wlr_scene_tree *popups;
	struct wl_listener map;
	struct wl_listener unmap;
	struct wl_listener commit;
	struct wl_listener new_popup;
	struct wl_listener destroy;
};

void stonefly_layer_setup(struct stonefly_server *server);
void stonefly_layer_finish(struct stonefly_server *server);

/* Scene tree for a zwlr_layer_shell_v1 layer enum value. */
struct wlr_scene_tree *stonefly_layer_tree(struct stonefly_server *server,
					   enum zwlr_layer_shell_v1_layer layer);

void stonefly_layer_arrange_output(struct stonefly_output *output);

/* Layout-coord usable box (exclusive zones applied). */
bool stonefly_output_usable_box(struct stonefly_output *output, struct wlr_box *box);
bool stonefly_layout_usable_at(struct stonefly_server *server, double lx, double ly,
			       struct wlr_box *box);

#endif
