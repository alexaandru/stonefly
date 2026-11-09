#ifndef STONEFLY_SSD_H
#define STONEFLY_SSD_H

#include <stdbool.h>
#include <stdint.h>

#include "server/server.h"

struct stonefly_toplevel;
struct wlr_xdg_toplevel_decoration_v1;
struct wlr_pointer_button_event;
struct wlr_scene_node;

struct stonefly_ssd {
	enum stonefly_node_kind kind; /* STONEFLY_NODE_SSD */
	struct stonefly_toplevel *toplevel;
	struct wlr_scene_buffer *scene_buf;
	struct wlr_buffer *buffer;
	uint8_t *pixels;
	int width;
	int height;
	bool visible;
};

/* fcft init; call once from server setup. */
void stonefly_ssd_setup(struct stonefly_server *server);
void stonefly_ssd_finish(struct stonefly_server *server);

/* True when a titlebar should be shown (want_ssd and not fullscreen/edge-max). */
bool stonefly_ssd_should_show(const struct stonefly_toplevel *toplevel);

/* Extra top usable inset (ssd_height) when the titlebar is shown. */
int stonefly_ssd_top_inset(const struct stonefly_toplevel *toplevel);

/* Create/destroy/repaint titlebar from current want_ssd + geometry. */
void stonefly_ssd_update(struct stonefly_toplevel *toplevel);
void stonefly_ssd_destroy(struct stonefly_toplevel *toplevel);

/* Repaint fill/title for focus change (seat focus may not have moved yet). */
void stonefly_ssd_set_focused(struct stonefly_toplevel *toplevel, bool focused);

/* xdg-decoration: honor client request_mode within ssd_mode; sync titlebar. */
void stonefly_ssd_decoration_apply(struct stonefly_toplevel *toplevel,
				   struct wlr_xdg_toplevel_decoration_v1 *wlr_deco);

/* Seat: true if node is this compositor's SSD titlebar buffer. */
bool stonefly_ssd_node_is_titlebar(struct wlr_scene_node *node,
				   struct stonefly_toplevel **out_toplevel);

/* Handle press/release on titlebar. Returns true if consumed (do not forward). */
bool stonefly_ssd_pointer_button(struct stonefly_server *server, struct stonefly_toplevel *toplevel,
				 double sx, double sy,
				 const struct wlr_pointer_button_event *event);

/* Clear in-flight titlebar press state (e.g. after a titlebar-started move). */
void stonefly_ssd_pointer_reset(void);

#endif /* STONEFLY_SSD_H */
