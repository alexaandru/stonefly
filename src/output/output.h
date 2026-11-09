#ifndef SF_OUTPUT_H
#define SF_OUTPUT_H

#include <wayland-server-core.h>
#include <wlr/util/box.h>

#include "server/server.h"
#include "wm/workspace.h"

struct wlr_output;
struct wlr_ext_workspace_group_handle_v1;

struct sf_output {
	struct wl_list link;
	struct sf_server *server;
	struct wlr_output *wlr_output;
	struct wl_listener frame;
	struct wl_listener request_state;
	struct wl_listener destroy;

	/* Layer-shell surfaces on this output, indexed by zwlr layer enum. */
	struct wl_list layers[4];
	struct wlr_box full;
	struct wlr_box usable;

	/* Dynamic per-output workspace strip. */
	struct wl_list workspaces;
	struct sf_workspace *active;
	enum sf_layout layout; /* UNSET = inherit global */
	struct wlr_ext_workspace_group_handle_v1 *ext_group;
};

void sf_output_setup(struct sf_server *server);
void sf_output_finish(struct sf_server *server);

/* Push current heads to zwlr_output_manager_v1 clients. */
void sf_output_mgr_send_config(struct sf_server *server);

/* DRM seat: config output_scale. Nested/headless (no session): 1.0. */
float sf_output_scale_factor(struct sf_server *server);

#endif
