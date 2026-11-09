#ifndef SF_DECOR_H
#define SF_DECOR_H

#include <stdbool.h>
#include <time.h>

struct sf_server;
struct sf_toplevel;
struct sf_output;
struct wlr_buffer;

/* Create the optimized-blur node (call after layer trees exist). */
void sf_decor_setup_scene(struct sf_server *server);

/* Apply SceneFX global blur_data from config (after scene + config load). */
void sf_decor_apply_blur_params(struct sf_server *server);

/* Resize/position optimized blur to the full output layout. */
void sf_decor_update_blur_size(struct sf_server *server);

/* Re-render optimized blur cache (wallpaper / bottom layer changed). */
void sf_decor_mark_blur_dirty(struct sf_server *server);

/* Build blur / border / shadow under toplevel->scene_tree (wrapper). */
void sf_decor_init_toplevel(struct sf_toplevel *toplevel);

/* Sync decoration geometry after an xdg commit / resize.
 * Edge-maximize: border/shadow/corners off; blur stays if window_blur
 * or client_blur (ext-background-effect-v1). Fullscreen: blur off too.
 * Border hole/client = corner_radius; outer = corner_radius +
 * border_thickness; hole inset 1px for AA hairlines. */
void sf_decor_update_geom(struct sf_toplevel *toplevel);

/* Re-evaluate SceneFX blur node enable (window_blur / client_blur / fullscreen). */
void sf_decor_refresh_blur(struct sf_toplevel *toplevel);

/* Apply opacity + per-buffer corner radii (corner_radius; only geometry
 * outer corners each buffer covers — avoids foot CSD titlebar/body
 * hourglass; SSD windows also skip client top radii). Edge-max: square
 * corners, no border/shadow, blur+opacity on. Fullscreen: opaque, no blur. */
void sf_decor_apply_buffers(struct sf_toplevel *toplevel);

/* Refresh border from focus / urgent / bell-flash state. */
void sf_decor_refresh_border(struct sf_toplevel *toplevel);

/* Neon blue when focused (clears urgency), silver otherwise. */
void sf_decor_set_focused(struct sf_toplevel *toplevel, bool focused);

/* Demand-attention until next focus (xdg-activation). */
void sf_decor_set_urgent(struct sf_toplevel *toplevel, bool urgent);

/* Brief amber flash for xdg-system-bell-v1. */
void sf_decor_ring_bell(struct sf_toplevel *toplevel);

/* Cancel bell timer and destroy the wrapper scene tree (border/blur/shadow).
 * Call from toplevel destroy — otherwise close leaves a leftover border. */
void sf_decor_finish_toplevel(struct sf_toplevel *toplevel);

/* Lock a live scene buffer for close-ghost (fails after NULL-buffer unmap). */
bool sf_decor_lock_snapshot(struct sf_toplevel *toplevel, struct wlr_buffer **out_locked,
			    int *out_w, int *out_h);

/* Keep last good frame while mapped; retain prior frame if pick fails. */
void sf_decor_refresh_close_snap(struct sf_toplevel *toplevel);

/* Unlock and clear toplevel->close_snap. */
void sf_decor_clear_close_snap(struct sf_toplevel *toplevel);

/* Client-initiated unmap: slide cached last frame (FORK.md close-ghost). */
void sf_ghost_try_spawn(struct sf_toplevel *toplevel);

/* Advance / destroy finished ghosts (called from anim tick). */
void sf_ghost_tick(struct sf_server *server, const struct timespec *now);

void sf_ghost_finish_all(struct sf_server *server);

#endif
