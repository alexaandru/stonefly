#ifndef STONEFLY_DECOR_H
#define STONEFLY_DECOR_H

#include <stdbool.h>
#include <time.h>

struct stonefly_server;
struct stonefly_toplevel;
struct stonefly_output;

/* Create the optimized-blur node (call after layer trees exist). */
void stonefly_decor_setup_scene(struct stonefly_server *server);

/* Apply SceneFX global blur_data from config (after scene + config load). */
void stonefly_decor_apply_blur_params(struct stonefly_server *server);

/* Resize/position optimized blur to the full output layout. */
void stonefly_decor_update_blur_size(struct stonefly_server *server);

/* Re-render optimized blur cache (wallpaper / bottom layer changed). */
void stonefly_decor_mark_blur_dirty(struct stonefly_server *server);

/* Build blur / border / shadow under toplevel->scene_tree (wrapper). */
void stonefly_decor_init_toplevel(struct stonefly_toplevel *toplevel);

/* Sync decoration geometry after an xdg commit / resize.
 * Edge-maximize: border/shadow/corners off; blur stays if window_blur.
 * Fullscreen: blur off too. Border hole/client = corner_radius; outer =
 * corner_radius + border_thickness; hole inset 1px for AA hairlines. */
void stonefly_decor_update_geom(struct stonefly_toplevel *toplevel);

/* Apply opacity + per-buffer corner radii (corner_radius; only geometry
 * outer corners each buffer covers — avoids foot CSD titlebar/body
 * hourglass). Edge-max: square corners, no border/shadow, blur+opacity on.
 * Fullscreen: opaque, no blur. */
void stonefly_decor_apply_buffers(struct stonefly_toplevel *toplevel);

/* Refresh border from focus / urgent / bell-flash state. */
void stonefly_decor_refresh_border(struct stonefly_toplevel *toplevel);

/* Neon blue when focused (clears urgency), silver otherwise. */
void stonefly_decor_set_focused(struct stonefly_toplevel *toplevel, bool focused);

/* Demand-attention until next focus (xdg-activation). */
void stonefly_decor_set_urgent(struct stonefly_toplevel *toplevel, bool urgent);

/* Brief amber flash for xdg-system-bell-v1. */
void stonefly_decor_ring_bell(struct stonefly_toplevel *toplevel);

/* Cancel bell timer and destroy the wrapper scene tree (border/blur/shadow).
 * Call from toplevel destroy — otherwise close leaves a leftover border. */
void stonefly_decor_finish_toplevel(struct stonefly_toplevel *toplevel);

/* Lock the main toplevel buffer for close-ghost snapshot slides. */
bool stonefly_decor_lock_snapshot(struct stonefly_toplevel *toplevel,
				  struct wlr_buffer **out_locked, int *out_w, int *out_h);

/* Client-initiated unmap: snapshot last buffer and slide it out. */
void stonefly_ghost_try_spawn(struct stonefly_toplevel *toplevel);

/* Advance / destroy finished ghosts (called from anim tick). */
void stonefly_ghost_tick(struct stonefly_server *server, const struct timespec *now);

void stonefly_ghost_finish_all(struct stonefly_server *server);

#endif
