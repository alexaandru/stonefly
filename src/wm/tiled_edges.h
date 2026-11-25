/* Shared xdg tiled-edge bits for layout FX and xdg-shell state sync. */
#ifndef SF_WM_TILED_EDGES_H
#define SF_WM_TILED_EDGES_H

#include <stdint.h>
#include <wlr/util/edges.h>

#include "wm/snap.h"

static inline uint32_t sf_tiled_edges_all(void) {
	return (uint32_t)WLR_EDGE_TOP | (uint32_t)WLR_EDGE_BOTTOM | (uint32_t)WLR_EDGE_LEFT |
	       (uint32_t)WLR_EDGE_RIGHT;
}

static inline uint32_t sf_tiled_edges_for_snap(enum sf_snap edge) {
	switch (edge) {
	case SF_SNAP_LEFT:
		return (uint32_t)WLR_EDGE_LEFT | (uint32_t)WLR_EDGE_TOP | (uint32_t)WLR_EDGE_BOTTOM;
	case SF_SNAP_RIGHT:
		return (uint32_t)WLR_EDGE_RIGHT | (uint32_t)WLR_EDGE_TOP |
		       (uint32_t)WLR_EDGE_BOTTOM;
	case SF_SNAP_TOP:
		return (uint32_t)WLR_EDGE_TOP | (uint32_t)WLR_EDGE_LEFT | (uint32_t)WLR_EDGE_RIGHT;
	case SF_SNAP_BOTTOM:
		return (uint32_t)WLR_EDGE_BOTTOM | (uint32_t)WLR_EDGE_LEFT |
		       (uint32_t)WLR_EDGE_RIGHT;
	case SF_SNAP_NONE:
		break;
	}
	return 0;
}

#endif
