#ifndef SF_LAYER_H
#define SF_LAYER_H

#include <stdbool.h>
#include <wayland-server-core.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/util/box.h>

#include "server/server.h"

struct sf_output;

struct sf_layer_surface {
	enum sf_node_kind kind; /* SF_NODE_LAYER */
	struct wl_list link;
	struct sf_server *server;
	struct sf_output *output;
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

void sf_layer_setup(struct sf_server *server);
void sf_layer_finish(struct sf_server *server);

/* Scene tree for a zwlr_layer_shell_v1 layer enum value. */
struct wlr_scene_tree *sf_layer_tree(struct sf_server *server,
				     enum zwlr_layer_shell_v1_layer layer);

void sf_layer_arrange_output(struct sf_output *output);

/* Layout-coord usable box (exclusive zones applied). */
bool sf_output_usable_box(struct sf_output *output, struct wlr_box *box);
bool sf_layout_usable_at(struct sf_server *server, double lx, double ly, struct wlr_box *box);

#endif
