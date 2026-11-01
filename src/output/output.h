#ifndef STONEFLY_OUTPUT_H
#define STONEFLY_OUTPUT_H

#include "server/server.h"

struct wlr_output;

struct stonefly_output {
	struct wl_list link;
	struct stonefly_server *server;
	struct wlr_output *wlr_output;
	struct wl_listener frame;
	struct wl_listener request_state;
	struct wl_listener destroy;
};

void stonefly_output_setup(struct stonefly_server *server);

#endif
