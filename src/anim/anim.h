#ifndef SF_ANIM_H
#define SF_ANIM_H

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

struct sf_server;
struct sf_toplevel;

/* Called when a hide slide finishes (minimize, scratch hide, close). */
typedef void (*sf_anim_done_fn)(struct sf_toplevel *toplevel);

void sf_anim_cancel(struct sf_toplevel *toplevel, bool commit_visual);

/* Slide from off-screen into the current scene resting position. */
bool sf_anim_start_show(struct sf_toplevel *toplevel);

/* Slide from the current visual position off-screen, then on_done. */
bool sf_anim_start_hide(struct sf_toplevel *toplevel, sf_anim_done_fn on_done);

/* Live morph (peeble ApplyManage): lerp position+size each tick via
 * set_position + wlr_xdg_toplevel_set_size; SceneFX chrome follows commits. */
bool sf_anim_start_morph(struct sf_toplevel *toplevel, const struct wlr_box *from,
			 const struct wlr_box *to);

bool sf_anim_active(const struct sf_toplevel *toplevel);
bool sf_anim_busy(struct sf_server *server);

/* Advance in-flight slides; call once per output frame before commit. */
void sf_anim_tick(struct sf_server *server, const struct timespec *now);

/* Request frames on every output so slides keep advancing. */
void sf_anim_schedule(struct sf_server *server);

/* Off-screen end of a slide from a resting box (usable at box center, then
 * cursor). Box-only path uses closest-edge; tiled show/hide always use the
 * column's right inter-gap (resting_x + w), never the screen edge. */
void sf_anim_offscreen_for_box(struct sf_server *server, int resting_x, int resting_y, int w, int h,
			       int *ox, int *oy);

#endif
