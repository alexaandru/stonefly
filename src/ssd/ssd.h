#ifndef SF_SSD_H
#define SF_SSD_H

#include <stdbool.h>
#include <stdint.h>

#include "server/server.h"

struct sf_toplevel;
struct wlr_xdg_toplevel_decoration_v1;
struct wlr_pointer_button_event;
struct wlr_scene_node;

struct sf_ssd {
	enum sf_node_kind kind; /* SF_NODE_SSD */
	struct sf_toplevel *toplevel;
	struct wlr_scene_buffer *scene_buf;
	struct wlr_buffer *buffer;
	uint8_t *pixels;
	int width;
	int height;
	bool visible;
};

/* fcft init; call once from server setup. */
void sf_ssd_setup(struct sf_server *server);
void sf_ssd_finish(struct sf_server *server);

/* True when a titlebar should be shown (want_ssd and not fullscreen/edge-max). */
bool sf_ssd_should_show(const struct sf_toplevel *toplevel);

/* Extra top usable inset (ssd_height) when the titlebar is shown. */
int sf_ssd_top_inset(const struct sf_toplevel *toplevel);

/* Create/destroy/repaint titlebar from current want_ssd + geometry. */
void sf_ssd_update(struct sf_toplevel *toplevel);
void sf_ssd_destroy(struct sf_toplevel *toplevel);

/* Repaint fill/title for focus change (seat focus may not have moved yet). */
void sf_ssd_set_focused(struct sf_toplevel *toplevel, bool focused);

/* xdg-decoration: honor client request_mode within ssd_mode; sync titlebar. */
void sf_ssd_decoration_apply(struct sf_toplevel *toplevel,
			     struct wlr_xdg_toplevel_decoration_v1 *wlr_deco);

/* Seat: true if node is this compositor's SSD titlebar buffer. */
bool sf_ssd_node_is_titlebar(struct wlr_scene_node *node, struct sf_toplevel **out_toplevel);

/* Handle press/release on titlebar. Returns true if consumed (do not forward). */
bool sf_ssd_pointer_button(struct sf_server *server, struct sf_toplevel *toplevel, double sx,
			   double sy, const struct wlr_pointer_button_event *event);

/* Clear in-flight titlebar press state (e.g. after a titlebar-started move). */
void sf_ssd_pointer_reset(void);

#endif /* SF_SSD_H */
