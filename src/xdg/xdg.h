#ifndef STONEFLY_XDG_H
#define STONEFLY_XDG_H

#include <stdint.h>
#include <wayland-server-core.h>

#include "toplevel/toplevel.h"

struct wlr_xdg_popup;

struct stonefly_popup {
	struct wlr_xdg_popup *xdg_popup;
	struct wl_listener commit;
	struct wl_listener destroy;
};

void stonefly_xdg_setup(struct stonefly_server *server);

/* Used by seat for interactive move/resize. */
void stonefly_begin_interactive(struct stonefly_toplevel *toplevel, enum stonefly_cursor_mode mode,
				uint32_t edges);

#endif
