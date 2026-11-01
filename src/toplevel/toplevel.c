#include "toplevel/toplevel.h"

#include <stdint.h>
#include <wlr/config.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_xdg_shell.h>
#if WLR_HAS_XWAYLAND
#include <xcb/xcb_icccm.h>
#include <wlr/xwayland.h>
#endif

#include "wm/layout_sm.h"

bool sf_toplevel_has_backend(const struct sf_toplevel *toplevel) {
	if (toplevel == NULL) {
		return false;
	}
	return toplevel->xdg_toplevel != NULL || toplevel->xwayland_surface != NULL;
}

struct wlr_surface *sf_toplevel_wlr_surface(const struct sf_toplevel *toplevel) {
	if (toplevel == NULL) {
		return NULL;
	}
	if (toplevel->xdg_toplevel != NULL && toplevel->xdg_toplevel->base != NULL) {
		return toplevel->xdg_toplevel->base->surface;
	}
#if WLR_HAS_XWAYLAND
	if (toplevel->xwayland_surface != NULL) {
		return toplevel->xwayland_surface->surface;
	}
#endif
	return NULL;
}

bool sf_toplevel_is_mapped(const struct sf_toplevel *toplevel) {
	struct wlr_surface *s = sf_toplevel_wlr_surface(toplevel);
	return s != NULL && s->mapped;
}

bool sf_toplevel_is_initialized(const struct sf_toplevel *toplevel) {
	if (toplevel == NULL) {
		return false;
	}
	if (toplevel->xdg_toplevel != NULL && toplevel->xdg_toplevel->base != NULL) {
		return toplevel->xdg_toplevel->base->initialized;
	}
#if WLR_HAS_XWAYLAND
	if (toplevel->xwayland_surface != NULL) {
		return toplevel->xwayland_surface->surface != NULL;
	}
#endif
	return false;
}

const char *sf_toplevel_title(const struct sf_toplevel *toplevel) {
	if (toplevel == NULL) {
		return NULL;
	}
	if (toplevel->xdg_toplevel != NULL) {
		return toplevel->xdg_toplevel->title;
	}
#if WLR_HAS_XWAYLAND
	if (toplevel->xwayland_surface != NULL) {
		return toplevel->xwayland_surface->title;
	}
#endif
	return NULL;
}

const char *sf_toplevel_app_id(const struct sf_toplevel *toplevel) {
	if (toplevel == NULL) {
		return NULL;
	}
	if (toplevel->xdg_toplevel != NULL) {
		return toplevel->xdg_toplevel->app_id;
	}
#if WLR_HAS_XWAYLAND
	if (toplevel->xwayland_surface != NULL) {
		if (toplevel->xwayland_surface->class != NULL &&
		    toplevel->xwayland_surface->class[0] != '\0') {
			return toplevel->xwayland_surface->class;
		}
		return toplevel->xwayland_surface->instance;
	}
#endif
	return NULL;
}

void sf_toplevel_surface_geom(const struct sf_toplevel *toplevel, struct wlr_box *out) {
	*out = (struct wlr_box){0};
	if (toplevel == NULL) {
		return;
	}
	if (toplevel->xdg_toplevel != NULL && toplevel->xdg_toplevel->base != NULL) {
		*out = toplevel->xdg_toplevel->base->geometry;
		if (out->width <= 0 || out->height <= 0) {
			struct wlr_surface *s = toplevel->xdg_toplevel->base->surface;
			if (s != NULL) {
				out->width = s->current.width;
				out->height = s->current.height;
			}
		}
		return;
	}
#if WLR_HAS_XWAYLAND
	if (toplevel->xwayland_surface != NULL) {
		struct wlr_surface *s = toplevel->xwayland_surface->surface;
		if (s != NULL && s->current.width > 0 && s->current.height > 0) {
			out->width = s->current.width;
			out->height = s->current.height;
		} else {
			out->width = toplevel->xwayland_surface->width;
			out->height = toplevel->xwayland_surface->height;
		}
	}
#endif
}

bool sf_toplevel_has_parent(const struct sf_toplevel *toplevel) {
	if (toplevel == NULL) {
		return false;
	}
	if (toplevel->xdg_toplevel != NULL) {
		return toplevel->xdg_toplevel->parent != NULL;
	}
#if WLR_HAS_XWAYLAND
	if (toplevel->xwayland_surface != NULL) {
		return toplevel->xwayland_surface->parent != NULL;
	}
#endif
	return false;
}

struct sf_toplevel *sf_toplevel_parent(const struct sf_toplevel *toplevel) {
	if (toplevel == NULL) {
		return NULL;
	}
	if (toplevel->xdg_toplevel != NULL) {
		struct wlr_xdg_toplevel *px = toplevel->xdg_toplevel->parent;
		if (px == NULL || px->base == NULL || px->base->data == NULL) {
			return NULL;
		}
		struct sf_toplevel *t = px->base->data;
		if (t->kind != SF_NODE_TOPLEVEL || t->xdg_toplevel != px) {
			return NULL;
		}
		return t;
	}
#if WLR_HAS_XWAYLAND
	if (toplevel->xwayland_surface != NULL) {
		struct wlr_xwayland_surface *px = toplevel->xwayland_surface->parent;
		if (px == NULL || px->data == NULL) {
			return NULL;
		}
		struct sf_toplevel *t = px->data;
		if (t->kind != SF_NODE_TOPLEVEL || t->xwayland_surface != px) {
			return NULL;
		}
		return t;
	}
#endif
	return NULL;
}

bool sf_toplevel_looks_tiled(const struct sf_toplevel *toplevel) {
	if (toplevel == NULL) {
		return true;
	}
	/* Parented transients float (xdg set_parent / X11 transient). */
	if (sf_toplevel_has_parent(toplevel)) {
		return false;
	}
#if WLR_HAS_XWAYLAND
	if (toplevel->xwayland_surface != NULL) {
		struct wlr_xwayland_surface *xs = toplevel->xwayland_surface;
		if (xs->modal) {
			return false;
		}
		static const enum wlr_xwayland_net_wm_window_type float_types[] = {
			WLR_XWAYLAND_NET_WM_WINDOW_TYPE_DIALOG,
			WLR_XWAYLAND_NET_WM_WINDOW_TYPE_SPLASH,
			WLR_XWAYLAND_NET_WM_WINDOW_TYPE_TOOLBAR,
			WLR_XWAYLAND_NET_WM_WINDOW_TYPE_UTILITY,
		};
		for (size_t i = 0; i < sizeof(float_types) / sizeof(float_types[0]); i++) {
			if (wlr_xwayland_surface_has_window_type(xs, float_types[i])) {
				return false;
			}
		}
	}
#endif
	/* Fullscreen (requested or applied) stays tiled-shaped — Wine fixed-size
	 * games must not become floating dialogs from size hints alone. */
	if (sf_layout_sm_is_fullscreen(&toplevel->layout)) {
		return true;
	}
	if (toplevel->xdg_toplevel != NULL && toplevel->xdg_toplevel->requested.fullscreen) {
		return true;
	}
#if WLR_HAS_XWAYLAND
	if (toplevel->xwayland_surface != NULL && toplevel->xwayland_surface->fullscreen) {
		return true;
	}
#endif
	if (toplevel->xdg_toplevel != NULL) {
		const struct wlr_xdg_toplevel_state *st = &toplevel->xdg_toplevel->current;
		bool fixed_w = st->max_width > 0 && st->min_width == st->max_width;
		bool fixed_h = st->max_height > 0 && st->min_height == st->max_height;
		/* Umbriel: either fixed axis → float (splash / dialog). */
		return !fixed_w && !fixed_h;
	}
#if WLR_HAS_XWAYLAND
	if (toplevel->xwayland_surface != NULL) {
		const xcb_size_hints_t *hints = toplevel->xwayland_surface->size_hints;
		uint32_t flags = hints != NULL ? (uint32_t)hints->flags : 0;
		if (hints == NULL || (flags & (uint32_t)XCB_ICCCM_SIZE_HINT_P_MIN_SIZE) == 0 ||
		    (flags & (uint32_t)XCB_ICCCM_SIZE_HINT_P_MAX_SIZE) == 0) {
			return true;
		}
		bool fixed_w = hints->max_width > 0 && hints->min_width == hints->max_width;
		bool fixed_h = hints->max_height > 0 && hints->min_height == hints->max_height;
		return !fixed_w && !fixed_h;
	}
#endif
	return true;
}

void sf_toplevel_min_size(const struct sf_toplevel *toplevel, int *min_w, int *min_h) {
	*min_w = 0;
	*min_h = 0;
	if (toplevel == NULL) {
		return;
	}
	if (toplevel->xdg_toplevel != NULL) {
		*min_w = toplevel->xdg_toplevel->current.min_width;
		*min_h = toplevel->xdg_toplevel->current.min_height;
		return;
	}
#if WLR_HAS_XWAYLAND
	if (toplevel->xwayland_surface != NULL && toplevel->xwayland_surface->size_hints != NULL) {
		if (toplevel->xwayland_surface->size_hints->min_width > 0) {
			*min_w = toplevel->xwayland_surface->size_hints->min_width;
		}
		if (toplevel->xwayland_surface->size_hints->min_height > 0) {
			*min_h = toplevel->xwayland_surface->size_hints->min_height;
		}
	}
#endif
}

void sf_toplevel_set_size(struct sf_toplevel *toplevel, int w, int h) {
	if (toplevel == NULL) {
		return;
	}
	if (toplevel->xdg_toplevel != NULL && toplevel->xdg_toplevel->base != NULL &&
	    toplevel->xdg_toplevel->base->initialized) {
		wlr_xdg_toplevel_set_size(toplevel->xdg_toplevel, w, h);
		return;
	}
#if WLR_HAS_XWAYLAND
	if (toplevel->xwayland_surface != NULL) {
		uint16_t uw = w > 0 ? (uint16_t)w : toplevel->xwayland_surface->width;
		uint16_t uh = h > 0 ? (uint16_t)h : toplevel->xwayland_surface->height;
		if (uw == 0) {
			uw = 1;
		}
		if (uh == 0) {
			uh = 1;
		}
		wlr_xwayland_surface_configure(toplevel->xwayland_surface,
					       (int16_t)toplevel->resting_x,
					       (int16_t)toplevel->resting_y, uw, uh);
	}
#endif
}

void sf_toplevel_set_pos(struct sf_toplevel *toplevel, int x, int y) {
	if (toplevel == NULL) {
		return;
	}
#if WLR_HAS_XWAYLAND
	if (toplevel->xwayland_surface != NULL) {
		struct wlr_box geo;
		sf_toplevel_surface_geom(toplevel, &geo);
		uint16_t uw = geo.width > 0 ? (uint16_t)geo.width : 1;
		uint16_t uh = geo.height > 0 ? (uint16_t)geo.height : 1;
		if (toplevel->xwayland_surface->x != (int16_t)x ||
		    toplevel->xwayland_surface->y != (int16_t)y) {
			wlr_xwayland_surface_configure(toplevel->xwayland_surface, (int16_t)x,
						       (int16_t)y, uw, uh);
		}
	}
#else
	(void)x;
	(void)y;
#endif
}

void sf_toplevel_set_activated(struct sf_toplevel *toplevel, bool active) {
	if (toplevel == NULL) {
		return;
	}
	if (toplevel->xdg_toplevel != NULL) {
		wlr_xdg_toplevel_set_activated(toplevel->xdg_toplevel, active);
		return;
	}
#if WLR_HAS_XWAYLAND
	if (toplevel->xwayland_surface != NULL) {
		wlr_xwayland_surface_activate(toplevel->xwayland_surface, active);
		if (active) {
			wlr_xwayland_surface_restack(toplevel->xwayland_surface, NULL,
						     XCB_STACK_MODE_ABOVE);
		}
	}
#endif
}

void sf_toplevel_set_maximized_state(struct sf_toplevel *toplevel, bool maximized) {
	if (toplevel == NULL) {
		return;
	}
	if (toplevel->xdg_toplevel != NULL) {
		wlr_xdg_toplevel_set_maximized(toplevel->xdg_toplevel, maximized);
		return;
	}
#if WLR_HAS_XWAYLAND
	if (toplevel->xwayland_surface != NULL) {
		wlr_xwayland_surface_set_maximized(toplevel->xwayland_surface, maximized,
						   maximized);
	}
#endif
}

void sf_toplevel_set_fullscreen_state(struct sf_toplevel *toplevel, bool fullscreen) {
	if (toplevel == NULL) {
		return;
	}
	if (toplevel->xdg_toplevel != NULL) {
		wlr_xdg_toplevel_set_fullscreen(toplevel->xdg_toplevel, fullscreen);
		return;
	}
#if WLR_HAS_XWAYLAND
	if (toplevel->xwayland_surface != NULL) {
		wlr_xwayland_surface_set_fullscreen(toplevel->xwayland_surface, fullscreen);
	}
#endif
}

void sf_toplevel_set_minimized_state(struct sf_toplevel *toplevel, bool minimized) {
	if (toplevel == NULL) {
		return;
	}
#if WLR_HAS_XWAYLAND
	if (toplevel->xwayland_surface != NULL) {
		wlr_xwayland_surface_set_minimized(toplevel->xwayland_surface, minimized);
	}
#else
	(void)minimized;
#endif
}

void sf_toplevel_set_tiled_edges(struct sf_toplevel *toplevel, uint32_t edges) {
	if (toplevel == NULL) {
		return;
	}
	if (toplevel->xdg_toplevel != NULL && toplevel->xdg_toplevel->base != NULL &&
	    toplevel->xdg_toplevel->base->initialized) {
		wlr_xdg_toplevel_set_tiled(toplevel->xdg_toplevel, edges);
	}
}

void sf_toplevel_send_close(struct sf_toplevel *toplevel) {
	if (toplevel == NULL) {
		return;
	}
	if (toplevel->xdg_toplevel != NULL) {
		wlr_xdg_toplevel_send_close(toplevel->xdg_toplevel);
		return;
	}
#if WLR_HAS_XWAYLAND
	if (toplevel->xwayland_surface != NULL) {
		wlr_xwayland_surface_close(toplevel->xwayland_surface);
	}
#endif
}
