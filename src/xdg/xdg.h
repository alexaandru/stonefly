#ifndef SF_XDG_H
#define SF_XDG_H

#include <stdint.h>
#include <wayland-server-core.h>

#include "toplevel/toplevel.h"

struct wlr_xdg_popup;

struct sf_popup {
	struct wlr_xdg_popup *xdg_popup;
	struct wl_listener commit;
	struct wl_listener destroy;
};

void sf_xdg_setup(struct sf_server *server);

/* Used by seat for interactive move/resize. */
void sf_begin_interactive(struct sf_toplevel *toplevel, enum sf_cursor_mode mode, uint32_t edges);

#endif
