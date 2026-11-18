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

/* Tile/morph presented content size (may be smaller than the committed
 * buffer while the client still redraws). SSD + decor crop to this. */
bool sf_decor_present_content_size(struct sf_toplevel *toplevel, int *w, int *h);

/* Sync decoration geometry after an xdg commit / resize.
 * Edge-maximize: border/shadow/corners off; blur stays if window_blur
 * or client_blur (ext-background-effect-v1). Fullscreen: blur off, opaque
 * fs_backdrop sized to the output tile, content centered while the client
 * buffer is still catching up (umbriel letterbox — hides wallpaper).
 * Border hole/client = corner_radius; outer = corner_radius +
 * border_thickness; hole inset 1px for AA hairlines. Crops an oversized
 * committed buffer to the tile so scrolled-off columns cannot paint under
 * the next cell. */
void sf_decor_update_geom(struct sf_toplevel *toplevel);

/* Destroy and recreate border+shadow, then update_geom. Unhide must not
 * reuse a SceneFX rect whose hole was the full window (hide used to zero
 * thickness). */
void sf_decor_rebuild_chrome(struct sf_toplevel *toplevel);

/* Non-tiled only: true when the logical rest box clears usable by ≥ border
 * on all four sides (not the hide/show park pose). Flush on any side → no
 * border/shadow/SSD (all-or-nothing). */
bool sf_toplevel_room_for_chrome(const struct sf_toplevel *toplevel);

/* Re-evaluate SceneFX blur node enable (window_blur / client_blur / fullscreen). */
void sf_decor_refresh_blur(struct sf_toplevel *toplevel);

/* Apply opacity + per-buffer corner radii (corner_radius; only geometry
 * outer corners each buffer covers — avoids foot CSD titlebar/body
 * hourglass; SSD windows also skip client top radii). Edge-max: square
 * corners, no border/shadow, blur+opacity on. Fullscreen: opaque, no blur.
 * While anim.pop, multiplies by toplevel->anim.fade. */
void sf_decor_apply_buffers(struct sf_toplevel *toplevel);

/* Popin/popout: stretch the root xdg buffer to the presented box
 * (umbriel source_box + dest_size). Call after set_clip — wlroots 0.20
 * surface_reconfigure overwrites source and dest when the clip changes.
 * Leave does not write dest_size; it nulls the clip, then sets the
 * resting clip, so reconfigure matches a client commit. */
void sf_decor_pop_set_dest_size(struct sf_toplevel *toplevel, int present_w, int present_h);
/* After a surface commit during pop — scene resets dest_size; re-apply idle. */
void sf_decor_pop_schedule_reapply(struct sf_toplevel *toplevel);
void sf_decor_pop_cancel_reapply(struct sf_toplevel *toplevel);

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

/* Snapshot close-ghost (Ctrl+W unmap and Super+q). Returns true if spawned. */
bool sf_ghost_try_spawn(struct sf_toplevel *toplevel);

/* Advance / destroy finished ghosts (called from anim tick). */
void sf_ghost_tick(struct sf_server *server, const struct timespec *now);

void sf_ghost_finish_all(struct sf_server *server);

#endif
