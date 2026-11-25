#ifndef SF_XDG_H
#define SF_XDG_H

#include <stdint.h>
#include <wayland-server-core.h>

#include "toplevel/toplevel.h"

struct wlr_xdg_popup;
struct wlr_scene_blur;
struct sf_output;
struct sf_server;

struct sf_popup {
	struct sf_server *server;
	struct wlr_xdg_popup *xdg_popup;
	/* Inherited full-geometry blur when parent layer has client_blur. */
	struct wlr_scene_blur *blur;
	struct wl_listener commit;
	struct wl_listener reposition;
	struct wl_listener destroy;
};

void sf_xdg_setup(struct sf_server *server);

/* xdg-shell v4–v7: bounds, wm_capabilities, suspended, constrained. */
void sf_xdg_sync_toplevel(struct sf_toplevel *toplevel);
void sf_xdg_sync_output(struct sf_output *output);
void sf_xdg_sync_all(struct sf_server *server);

/* Flip/slide/resize an xdg_popup into the parent's output usable box. */
void sf_xdg_popup_unconstrain(struct sf_server *server, struct wlr_xdg_popup *popup);

/* Used by seat for interactive move/resize. */
void sf_begin_interactive(struct sf_toplevel *toplevel, enum sf_cursor_mode mode, uint32_t edges);

#endif
