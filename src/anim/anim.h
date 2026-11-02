#ifndef STONEFLY_ANIM_H
#define STONEFLY_ANIM_H

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

struct stonefly_server;
struct stonefly_toplevel;

/* Called when a hide slide finishes (minimize, scratch hide, close). */
typedef void (*stonefly_anim_done_fn)(struct stonefly_toplevel *toplevel);

void stonefly_anim_cancel(struct stonefly_toplevel *toplevel, bool commit_visual);

/* Slide from off-screen into the current scene resting position. */
bool stonefly_anim_start_show(struct stonefly_toplevel *toplevel);

/* Slide from the current visual position off-screen, then on_done. */
bool stonefly_anim_start_hide(struct stonefly_toplevel *toplevel,
		stonefly_anim_done_fn on_done);

/* Live morph (peeble ApplyManage): lerp position+size each tick via
 * set_position + wlr_xdg_toplevel_set_size; SceneFX chrome follows commits. */
bool stonefly_anim_start_morph(struct stonefly_toplevel *toplevel,
		const struct wlr_box *from, const struct wlr_box *to);

bool stonefly_anim_active(const struct stonefly_toplevel *toplevel);
bool stonefly_anim_busy(struct stonefly_server *server);

/* Advance in-flight slides; call once per output frame before commit. */
void stonefly_anim_tick(struct stonefly_server *server, const struct timespec *now);

/* Request frames on every output so slides keep advancing. */
void stonefly_anim_schedule(struct stonefly_server *server);

#endif
