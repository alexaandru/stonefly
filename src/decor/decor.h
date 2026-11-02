#ifndef STONEFLY_DECOR_H
#define STONEFLY_DECOR_H

#include <stdbool.h>
#include <time.h>

struct stonefly_server;
struct stonefly_toplevel;
struct stonefly_output;

/* Create the optimized-blur node (call after layer trees exist). */
void stonefly_decor_setup_scene(struct stonefly_server *server);

/* Resize optimized blur to the full output layout. */
void stonefly_decor_update_blur_size(struct stonefly_server *server);

/* Build blur / border / shadow under toplevel->scene_tree (wrapper). */
void stonefly_decor_init_toplevel(struct stonefly_toplevel *toplevel);

/* Sync decoration geometry after an xdg commit / resize.
 * Edge-maximize disables blur/border/shadow and zero corner radii.
 * Border is a rounded rect with clipped_region hole (mango); per-window
 * blur stays gated by STONEFLY_WINDOW_BLUR (black plate when broken). */
void stonefly_decor_update_geom(struct stonefly_toplevel *toplevel);

/* Apply opacity + corner radii to all xdg scene buffers (incl. CSD).
 * No chrome when maximized_edges (client-only, opaque, square). */
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

/* Client-initiated unmap: snapshot last buffer and slide it out. */
void stonefly_ghost_try_spawn(struct stonefly_toplevel *toplevel);

/* Advance / destroy finished ghosts (called from anim tick). */
void stonefly_ghost_tick(struct stonefly_server *server,
		const struct timespec *now);

void stonefly_ghost_finish_all(struct stonefly_server *server);

#endif
