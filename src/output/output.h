#ifndef STONEFLY_OUTPUT_H
#define STONEFLY_OUTPUT_H

#include <wayland-server-core.h>
#include <wlr/util/box.h>

#include "server/server.h"

struct wlr_output;

struct stonefly_output {
	struct wl_list link;
	struct stonefly_server *server;
	struct wlr_output *wlr_output;
	struct wl_listener frame;
	struct wl_listener request_state;
	struct wl_listener destroy;

	/* Layer-shell surfaces on this output, indexed by zwlr layer enum. */
	struct wl_list layers[4];
	struct wlr_box full;
	struct wlr_box usable;
};

void stonefly_output_setup(struct stonefly_server *server);

/* DRM seat: STONEFLY_OUTPUT_SCALE. Nested/headless (no session): 1.0. */
float stonefly_output_scale_factor(struct stonefly_server *server);

#endif
