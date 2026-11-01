#ifndef SF_XWAYLAND_H
#define SF_XWAYLAND_H

#include <stdbool.h>

struct sf_server;
struct wl_client;
struct wl_global;

/* Start lazy on-demand Xwayland and set DISPLAY if available. */
void sf_xwayland_setup(struct sf_server *server);

/* Tear down Xwayland server and listeners. */
void sf_xwayland_finish(struct sf_server *server);

/* Global filter check: expose xwayland_shell_v1 only to the Xwayland client.
 * Returns false to hide the global, true to allow other filter checks. */
bool sf_xwayland_allow_global(const struct sf_server *server, const struct wl_client *client,
			      const struct wl_global *global);

#endif
