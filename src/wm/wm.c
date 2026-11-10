#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <scenefx/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/box.h>
#include <wlr/util/edges.h>

#include "anim/anim.h"
#include "decor/decor.h"
#include "layer/layer.h"
#include "log/log.h"
#include "output/output.h"
#include "seat/seat.h"
#include "session/session.h"
#include "ssd/ssd.h"
#include "toplevel/foreign.h"
#include "wm/arrange.h"
#include "wm/geom.h"
#include "wm/internal.h"
#include "wm/layout_sm.h"
#include "wm/place_sm.h"
#include "wm/rules.h"
#include "wm/scratchpad.h"
#include "wm/wm.h"
#include "wm/workspace.h"

void sf_toplevel_layout_geom(struct sf_toplevel *toplevel, struct wlr_box *box) {
	struct wlr_box *geo = &toplevel->xdg_toplevel->base->geometry;
	box->x = toplevel->scene_tree->node.x;
	box->y = toplevel->scene_tree->node.y;
	box->width = geo->width;
	box->height = geo->height;
	if (box->width <= 0 || box->height <= 0) {
		box->width = toplevel->xdg_toplevel->base->surface->current.width;
		box->height = toplevel->xdg_toplevel->base->surface->current.height;
	}
}

bool sf_toplevel_output_box(struct sf_toplevel *toplevel, struct wlr_box *box) {
	struct sf_server *server = toplevel->server;
	struct wlr_box geom;
	sf_toplevel_layout_geom(toplevel, &geom);
	double cx = geom.x + geom.width / 2.0;
	double cy = geom.y + geom.height / 2.0;
	if (sf_layout_usable_at(server, cx, cy, box)) {
		return true;
	}
	return sf_layout_usable_at(server, server->cursor->x, server->cursor->y, box);
}

/*
 * Content origin is the scene_tree; SSD is drawn above it. Gap-max / snap fill
 * usable as content — reserve titlebar height so it stays below exclusive
 * zones (Noctalia bar), matching cascade / ensure-on-screen.
 */
static void box_inset_ssd_top(struct sf_toplevel *toplevel, struct wlr_box *box) {
	int inset = sf_ssd_top_inset(toplevel);
	if (inset <= 0 || box->height <= inset) {
		return;
	}
	box->y += inset;
	box->height -= inset;
}

static void sf_cover_fractional_gap(struct sf_server *server, struct wlr_box *box) {
	/* Same class of gap as peeble at non-integer scale: grow 1 logical px. */
	float scale = sf_output_scale_factor(server);
	if (scale != (float)(int)scale) {
		box->width += 1;
		box->height += 1;
	}
}

/* wlroots 0.20 asserts schedule_configure only after the first commit. */
static bool xdg_surface_ready(struct sf_toplevel *toplevel) {
	return toplevel->xdg_toplevel->base->initialized;
}

/* Peeble tiledEdgesAll / tiledEdgesFor — CSD drops shadows/resizers on edges. */
static uint32_t tiled_edges_all(void) {
	return (uint32_t)WLR_EDGE_TOP | (uint32_t)WLR_EDGE_BOTTOM | (uint32_t)WLR_EDGE_LEFT |
	       (uint32_t)WLR_EDGE_RIGHT;
}

static uint32_t tiled_edges_for(enum sf_snap edge) {
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

static void toplevel_set_tiled(struct sf_toplevel *toplevel, uint32_t edges) {
	if (!xdg_surface_ready(toplevel)) {
		return;
	}
	wlr_xdg_toplevel_set_tiled(toplevel->xdg_toplevel, edges);
}

static void sf_toplevel_apply_box(struct sf_toplevel *toplevel, const struct wlr_box *box) {
	sf_anim_cancel(toplevel, false);
	wlr_scene_node_set_position(&toplevel->scene_tree->node, box->x, box->y);
	toplevel->resting_x = box->x;
	toplevel->resting_y = box->y;
	if (!xdg_surface_ready(toplevel)) {
		return;
	}
	wlr_xdg_toplevel_set_size(toplevel->xdg_toplevel, box->width, box->height);
}

/* Snap / gap-max / edge-max share peeble ApplyManage morph. Fullscreen stays
 * instant (chrome policy). Falls back to apply_box when morph is disabled. */
static bool toplevel_try_morph_to(struct sf_toplevel *toplevel, const struct wlr_box *to) {
	struct wlr_box from;
	sf_toplevel_layout_geom(toplevel, &from);
	if (from.width > 0 && from.height > 0 && sf_anim_start_morph(toplevel, &from, to)) {
		return true;
	}
	sf_toplevel_apply_box(toplevel, to);
	return false;
}

void sf_toplevel_move_resize(struct sf_toplevel *toplevel, const struct wlr_box *box, bool morph) {
	if (toplevel == NULL || box == NULL) {
		return;
	}
	if (morph) {
		toplevel_try_morph_to(toplevel, box);
	} else {
		sf_toplevel_apply_box(toplevel, box);
	}
}

static bool toplevel_full_output_box(struct sf_toplevel *toplevel, struct wlr_output *hint,
				     struct wlr_box *box);

static void layout_fill_targets(struct sf_toplevel *toplevel, const struct wlr_box *usable,
				struct wlr_output *fs_hint, struct sf_layout_ctx *ctx) {
	*ctx = (struct sf_layout_ctx){0};
	sf_toplevel_layout_geom(toplevel, &ctx->current);
	ctx->tol = 2;
	if (usable != NULL && usable->width > 0 && usable->height > 0) {
		ctx->max_edges = *usable;
		sf_cover_fractional_gap(toplevel->server, &ctx->max_edges);
		ctx->max_gaps = *usable;
		sf_box_inset_gap(&ctx->max_gaps, sf_cfg(toplevel->server)->gap_size);
		box_inset_ssd_top(toplevel, &ctx->max_gaps);
		int gap = sf_cfg(toplevel->server)->gap_size;
		ctx->snap_left = sf_wm_snap_box(SF_SNAP_LEFT, usable, gap);
		box_inset_ssd_top(toplevel, &ctx->snap_left);
		ctx->snap_right = sf_wm_snap_box(SF_SNAP_RIGHT, usable, gap);
		box_inset_ssd_top(toplevel, &ctx->snap_right);
		ctx->snap_top = sf_wm_snap_box(SF_SNAP_TOP, usable, gap);
		box_inset_ssd_top(toplevel, &ctx->snap_top);
		ctx->snap_bottom = sf_wm_snap_box(SF_SNAP_BOTTOM, usable, gap);
		box_inset_ssd_top(toplevel, &ctx->snap_bottom);
	}
	toplevel_full_output_box(toplevel, fs_hint, &ctx->fullscreen);
}

static bool layout_fill_ctx(struct sf_toplevel *toplevel, struct wlr_output *fs_hint,
			    struct sf_layout_ctx *ctx) {
	struct wlr_box usable;
	const struct wlr_box *u = NULL;
	if (sf_toplevel_output_box(toplevel, &usable)) {
		u = &usable;
	}
	layout_fill_targets(toplevel, u, fs_hint, ctx);
	if (toplevel->arrange_managed) {
		ctx->no_geom_regain = true;
	} else if (toplevel->geom_regain_hold) {
		if (sf_layout_sm_box_near(&ctx->current, &toplevel->geom_regain_box, ctx->tol)) {
			ctx->no_geom_regain = true;
		} else {
			toplevel->geom_regain_hold = false;
		}
	}
	return true;
}

static uint32_t layout_tiled_edges(unsigned tiled) {
	if (tiled == SF_LAYOUT_TILED_NONE) {
		return 0;
	}
	if (tiled == SF_LAYOUT_TILED_ALL) {
		return tiled_edges_all();
	}
	return tiled_edges_for((enum sf_snap)tiled);
}

void sf_toplevel_sync_scene_layer(struct sf_toplevel *toplevel) {
	if (toplevel == NULL || toplevel->scene_tree == NULL || toplevel->server == NULL) {
		return;
	}
	struct wlr_scene_tree *want;
	bool fs = sf_layout_sm_is_fullscreen(&toplevel->layout);
	if (fs) {
		want = toplevel->server->layers.fullscreen;
	} else if (toplevel->workspace != NULL && toplevel->workspace->tree != NULL) {
		want = toplevel->workspace->tree;
	} else {
		want = toplevel->server->layers.toplevel;
	}
	if (want == NULL) {
		return;
	}
	if (toplevel->scene_tree->node.parent != want) {
		wlr_scene_node_reparent(&toplevel->scene_tree->node, want);
	}
	if (fs) {
		wlr_scene_node_raise_to_top(&toplevel->scene_tree->node);
	}
}

static void layout_apply_result(struct sf_toplevel *toplevel, const struct sf_layout_result *r) {
	toplevel->layout = r->state;
	for (size_t i = 0; i < r->nfx; i++) {
		const struct sf_layout_effect *e = &r->fx[i];
		switch (e->kind) {
		case SF_LAYOUT_FX_APPLY_BOX:
			if (e->morph) {
				toplevel_try_morph_to(toplevel, &e->box);
			} else {
				sf_toplevel_apply_box(toplevel, &e->box);
			}
			break;
		case SF_LAYOUT_FX_XDG_MAXIMIZED:
			if (xdg_surface_ready(toplevel)) {
				wlr_xdg_toplevel_set_maximized(toplevel->xdg_toplevel, e->flag);
			}
			break;
		case SF_LAYOUT_FX_XDG_FULLSCREEN:
			if (xdg_surface_ready(toplevel)) {
				wlr_xdg_toplevel_set_fullscreen(toplevel->xdg_toplevel, e->flag);
			}
			break;
		case SF_LAYOUT_FX_TILED:
			toplevel_set_tiled(toplevel, layout_tiled_edges(e->tiled));
			break;
		case SF_LAYOUT_FX_CLEAR_AUTO_PLACE:
			sf_toplevel_clear_auto_place(toplevel);
			break;
		case SF_LAYOUT_FX_DECOR:
			sf_decor_update_geom(toplevel);
			break;
		case SF_LAYOUT_FX_FOREIGN_STATE:
			sf_foreign_toplevel_on_state_change(toplevel);
			break;
		case SF_LAYOUT_FX_FOREIGN_OUTPUTS:
			sf_foreign_toplevel_on_outputs_change(toplevel);
			break;
		case SF_LAYOUT_FX_SCHEDULE_CONFIGURE:
			if (xdg_surface_ready(toplevel)) {
				wlr_xdg_surface_schedule_configure(toplevel->xdg_toplevel->base);
			}
			break;
		}
	}
	sf_toplevel_sync_scene_layer(toplevel);
}

static void layout_dispatch(struct sf_toplevel *toplevel, const struct sf_layout_ev *ev,
			    struct wlr_output *fs_hint) {
	struct sf_layout_ctx ctx;
	layout_fill_ctx(toplevel, fs_hint, &ctx);
	struct sf_layout_result out;
	sf_layout_sm_step(&toplevel->layout, ev, &ctx, &out);
	layout_apply_result(toplevel, &out);
}

static void layout_dispatch_usable(struct sf_toplevel *toplevel, const struct sf_layout_ev *ev,
				   const struct wlr_box *usable) {
	struct sf_layout_ctx ctx;
	layout_fill_targets(toplevel, usable, NULL, &ctx);
	struct sf_layout_result out;
	sf_layout_sm_step(&toplevel->layout, ev, &ctx, &out);
	layout_apply_result(toplevel, &out);
}

static bool toplevel_full_output_box(struct sf_toplevel *toplevel, struct wlr_output *hint,
				     struct wlr_box *box) {
	struct sf_server *server = toplevel->server;
	struct wlr_output *wlr_output = hint;
	if (wlr_output == NULL) {
		struct wlr_box geom;
		sf_toplevel_layout_geom(toplevel, &geom);
		double cx = geom.x + geom.width / 2.0;
		double cy = geom.y + geom.height / 2.0;
		wlr_output = wlr_output_layout_output_at(server->output_layout, cx, cy);
		if (wlr_output == NULL) {
			wlr_output = wlr_output_layout_output_at(
				server->output_layout, server->cursor->x, server->cursor->y);
		}
	}
	if (wlr_output == NULL) {
		return false;
	}
	wlr_output_layout_get_box(server->output_layout, wlr_output, box);
	return box->width > 0 && box->height > 0;
}

void sf_toplevel_restore_saved(struct sf_toplevel *toplevel) {
	if (toplevel == NULL || !toplevel->layout.has_saved) {
		return;
	}
	struct sf_layout_ev ev = {0};
	switch (toplevel->layout.mode) {
	case SF_LAYOUT_MODE_MAX_EDGES:
		ev.kind = SF_LAYOUT_EV_SET_MAX_EDGES;
		ev.want = false;
		break;
	case SF_LAYOUT_MODE_MAX_GAPS:
		ev.kind = SF_LAYOUT_EV_SET_MAX_GAPS;
		ev.want = false;
		break;
	case SF_LAYOUT_MODE_SNAP:
		ev.kind = SF_LAYOUT_EV_UNSNAP;
		break;
	case SF_LAYOUT_MODE_FULLSCREEN:
		ev.kind = SF_LAYOUT_EV_SET_FULLSCREEN;
		ev.want = false;
		break;
	case SF_LAYOUT_MODE_FLOAT:
		/* Float with a stored PreMax: apply it and clear. */
		{
			struct wlr_box to = toplevel->layout.saved;
			toplevel->layout.has_saved = false;
			sf_toplevel_apply_box(toplevel, &to);
			sf_decor_update_geom(toplevel);
		}
		return;
	}
	layout_dispatch(toplevel, &ev, NULL);
}

void sf_toplevel_capture_geom(struct sf_toplevel *toplevel, const struct wlr_box *box) {
	if (toplevel == NULL || box == NULL || box->width <= 0 || box->height <= 0) {
		return;
	}
	if (toplevel->layout.has_saved || !sf_layout_sm_is_float(&toplevel->layout)) {
		return;
	}
	toplevel->layout.saved = *box;
	toplevel->layout.has_saved = true;
}

void sf_toplevel_apply_saved_at(struct sf_toplevel *toplevel, int x, int y) {
	if (toplevel == NULL || !toplevel->layout.has_saved) {
		return;
	}
	struct wlr_box box = toplevel->layout.saved;
	box.x = x;
	box.y = y;
	struct sf_layout_ev ev = {.kind = SF_LAYOUT_EV_FLOAT, .clear_saved = false};
	layout_dispatch(toplevel, &ev, NULL);
	sf_toplevel_apply_box(toplevel, &box);
	sf_decor_update_geom(toplevel);
}

void sf_toplevel_layout_float(struct sf_toplevel *toplevel, bool clear_saved) {
	if (toplevel == NULL) {
		return;
	}
	if (toplevel->arrange_managed) {
		sf_arrange_break_out(toplevel);
	}
	/* Break max/snap only (not fullscreen). clear_saved on an already-float
	 * window drops PreMax when a rule asks for float. */
	if (!sf_layout_sm_is_edged(&toplevel->layout)) {
		if (!(sf_layout_sm_is_float(&toplevel->layout) && clear_saved &&
		      toplevel->layout.has_saved)) {
			return;
		}
	}
	struct sf_layout_ev ev = {.kind = SF_LAYOUT_EV_FLOAT, .clear_saved = clear_saved};
	layout_dispatch(toplevel, &ev, NULL);
}

void sf_toplevel_float_for_resize(struct sf_toplevel *toplevel) {
	sf_toplevel_layout_float(toplevel, true);
}

void sf_toplevel_set_max_edges(struct sf_toplevel *toplevel, bool want) {
	if (toplevel == NULL || !xdg_surface_ready(toplevel)) {
		return;
	}
	struct sf_layout_ev ev = {.kind = SF_LAYOUT_EV_SET_MAX_EDGES, .want = want};
	layout_dispatch(toplevel, &ev, NULL);
}

void sf_toplevel_toggle_max_edges(struct sf_toplevel *toplevel) {
	if (toplevel == NULL || !xdg_surface_ready(toplevel)) {
		return;
	}
	struct sf_layout_ev ev = {.kind = SF_LAYOUT_EV_TOGGLE_MAX_EDGES};
	layout_dispatch(toplevel, &ev, NULL);
}

void sf_toplevel_set_fullscreen(struct sf_toplevel *toplevel, bool want, struct wlr_output *hint) {
	if (toplevel == NULL || !xdg_surface_ready(toplevel)) {
		return;
	}
	struct sf_layout_ev ev = {.kind = SF_LAYOUT_EV_SET_FULLSCREEN, .want = want};
	layout_dispatch(toplevel, &ev, hint);
}

void sf_toplevel_toggle_fullscreen(struct sf_toplevel *toplevel) {
	if (toplevel == NULL || !xdg_surface_ready(toplevel)) {
		return;
	}
	struct sf_layout_ev ev = {.kind = SF_LAYOUT_EV_TOGGLE_FULLSCREEN};
	layout_dispatch(toplevel, &ev, NULL);
}

void sf_toplevel_set_max_gaps(struct sf_toplevel *toplevel, bool want) {
	if (toplevel == NULL || !xdg_surface_ready(toplevel)) {
		return;
	}
	struct sf_layout_ev ev = {.kind = SF_LAYOUT_EV_SET_MAX_GAPS, .want = want};
	layout_dispatch(toplevel, &ev, NULL);
}

void sf_toplevel_toggle_max_gaps(struct sf_toplevel *toplevel) {
	if (toplevel == NULL || !xdg_surface_ready(toplevel)) {
		return;
	}
	struct sf_layout_ev ev = {.kind = SF_LAYOUT_EV_TOGGLE_MAX_GAPS};
	layout_dispatch(toplevel, &ev, NULL);
}

void sf_toplevel_snap_in(struct sf_toplevel *toplevel, enum sf_snap edge,
			 const struct wlr_box *usable) {
	if (toplevel == NULL || edge == SF_SNAP_NONE || usable == NULL ||
	    !xdg_surface_ready(toplevel)) {
		return;
	}
	struct sf_layout_ev ev = {.kind = SF_LAYOUT_EV_SNAP, .snap = edge};
	layout_dispatch_usable(toplevel, &ev, usable);
}

void sf_toplevel_snap(struct sf_toplevel *toplevel, enum sf_snap edge) {
	struct wlr_box out;
	if (!sf_toplevel_output_box(toplevel, &out)) {
		return;
	}
	sf_toplevel_snap_in(toplevel, edge, &out);
}

void sf_toplevel_layout_observe_box(struct sf_toplevel *toplevel, const struct wlr_box *current) {
	if (toplevel == NULL || toplevel->scene_tree == NULL || current == NULL) {
		return;
	}
	struct sf_layout_ctx ctx;
	layout_fill_ctx(toplevel, NULL, &ctx);
	ctx.current = *current;
	struct sf_layout_ev ev = {.kind = SF_LAYOUT_EV_GEOM};
	struct sf_layout_result out;
	sf_layout_sm_step(&toplevel->layout, &ev, &ctx, &out);
	layout_apply_result(toplevel, &out);
}

void sf_toplevel_layout_observe(struct sf_toplevel *toplevel) {
	if (toplevel == NULL || toplevel->scene_tree == NULL) {
		return;
	}
	struct wlr_box cur;
	sf_toplevel_layout_geom(toplevel, &cur);
	/*
	 * Show/hide slides park the scene node off-screen; morph lerps through
	 * intermediate boxes. GEOM reconcile must use the logical target, or
	 * mid-slide commits demote edge-max → float (borders/SSD flash) and
	 * regain on settle (the ~1cm bounce when the titlebar drops).
	 */
	if (toplevel->anim.active) {
		if (toplevel->anim.morph) {
			cur.x = toplevel->anim.to_x;
			cur.y = toplevel->anim.to_y;
			if (toplevel->anim.to_w > 0 && toplevel->anim.to_h > 0) {
				cur.width = toplevel->anim.to_w;
				cur.height = toplevel->anim.to_h;
			}
		} else {
			cur.x = toplevel->resting_x;
			cur.y = toplevel->resting_y;
		}
	}
	sf_toplevel_layout_observe_box(toplevel, &cur);
}

static bool toplevel_on_output(struct sf_toplevel *toplevel, struct sf_output *output) {
	struct wlr_box geom;
	sf_toplevel_layout_geom(toplevel, &geom);
	double cx = geom.x + geom.width / 2.0;
	double cy = geom.y + geom.height / 2.0;
	struct wlr_output *at =
		wlr_output_layout_output_at(toplevel->server->output_layout, cx, cy);
	return at == output->wlr_output;
}

static void toplevel_set_resting(struct sf_toplevel *toplevel, int x, int y) {
	toplevel->resting_x = x;
	toplevel->resting_y = y;
	if (toplevel->anim.active && !toplevel->anim_hide) {
		/* Retarget an in-flight appear slide (peeble RetargetShowResting). */
		toplevel->anim.to_x = x;
		toplevel->anim.to_y = y;
		return;
	}
	if (toplevel->anim.active) {
		return;
	}
	/* SoftPlace hold: logical resting only until try_appear slides in. */
	if (sf_place_sm_is_holding(&toplevel->place)) {
		return;
	}
	if (toplevel->scene_tree == NULL) {
		return;
	}
	/* Late SoftPlace / managed recenter after appear finished: morph
	 * instead of snapping (Chrome profile title can arrive after the slide).
	 * Skip while still disabled on the map path — morphing from the default
	 * (0,0) left ensure_on_screen clamping every new client to usable
	 * top-left and erased cascade offsets. */
	if (sf_place_sm_may_morph(&toplevel->place) && toplevel->scene_tree->node.enabled &&
	    (toplevel->scene_tree->node.x != x || toplevel->scene_tree->node.y != y)) {
		struct wlr_box from, to;
		sf_toplevel_layout_geom(toplevel, &from);
		if (from.width > 0 && from.height > 0) {
			to = from;
			to.x = x;
			to.y = y;
			if (sf_anim_start_morph(toplevel, &from, &to)) {
				return;
			}
		}
	}
	wlr_scene_node_set_position(&toplevel->scene_tree->node, x, y);
}

void sf_toplevel_ensure_on_screen(struct sf_toplevel *toplevel) {
	if (toplevel == NULL || toplevel->minimized || toplevel->anim_hide) {
		return;
	}
	if (sf_layout_sm_is_edged(&toplevel->layout)) {
		return;
	}
	if (toplevel->arrange_managed && toplevel->workspace != NULL &&
	    sf_arrange_is_tiling(sf_workspace_arrange(toplevel->workspace))) {
		return;
	}
	if (toplevel->server->grabbed_toplevel == toplevel) {
		return;
	}

	struct wlr_box usable;
	if (!sf_toplevel_output_box(toplevel, &usable)) {
		return;
	}
	struct wlr_box area = usable;
	sf_box_inset_gap(&area, sf_cfg(toplevel->server)->gap_size);
	if (area.width <= 0 || area.height <= 0) {
		return;
	}

	struct wlr_box geom;
	sf_toplevel_layout_geom(toplevel, &geom);
	int top_inset = sf_ssd_top_inset(toplevel);
	/* Prefer logical resting while a morph/slide still shows an old pose —
	 * otherwise clamp can yank a brand-new cascade back to usable origin. */
	if (toplevel->anim.active ||
	    (toplevel->scene_tree != NULL && !toplevel->scene_tree->node.enabled)) {
		geom.x = toplevel->resting_x;
		geom.y = toplevel->resting_y;
	}
	if (geom.width <= 0 || geom.height <= 0) {
		/* Size unknown: at least park the cascade origin inside usable. */
		int x = toplevel->resting_x;
		int y = toplevel->resting_y;
		if (x < area.x) {
			x = area.x;
		}
		if (y < area.y + top_inset) {
			y = area.y + top_inset;
		}
		if (x != toplevel->resting_x || y != toplevel->resting_y) {
			toplevel_set_resting(toplevel, x, y);
		}
		return;
	}

	if (geom.width > area.width || geom.height + top_inset > area.height) {
		sf_toplevel_set_max_gaps(toplevel, true);
		return;
	}
	int x = geom.x;
	int y = geom.y;
	if (x + geom.width > area.x + area.width) {
		x = area.x + area.width - geom.width;
	}
	if (y + geom.height > area.y + area.height) {
		y = area.y + area.height - geom.height;
	}
	if (x < area.x) {
		x = area.x;
	}
	if (y < area.y + top_inset) {
		y = area.y + top_inset;
	}
	if (x == geom.x && y == geom.y) {
		return;
	}
	toplevel_set_resting(toplevel, x, y);
}

void sf_wm_relayout_output(struct sf_output *output) {
	if (output == NULL) {
		return;
	}
	struct sf_server *server = output->server;
	struct sf_toplevel *toplevel;
	wl_list_for_each(toplevel, &server->toplevels, link) {
		if (toplevel->minimized || !toplevel_on_output(toplevel, output)) {
			continue;
		}
		struct wlr_box out;
		if (!sf_output_usable_box(output, &out)) {
			continue;
		}
		if (sf_layout_sm_is_fixed(&toplevel->layout)) {
			struct sf_layout_ev ev = {.kind = SF_LAYOUT_EV_REAPPLY};
			layout_dispatch_usable(toplevel, &ev, &out);
		} else if (toplevel->arrange_managed && toplevel->workspace != NULL &&
			   sf_arrange_is_tiling(sf_workspace_arrange(toplevel->workspace))) {
			/* Engine-owned: schedule arrange instead of floater clamp. */
			continue;
		} else {
			/* Floaters: exclusive zone often arrives after PlaceNew. */
			sf_toplevel_ensure_on_screen(toplevel);
		}
	}
	if (output->active != NULL) {
		sf_arrange_schedule(output->active);
	}
}

void sf_toplevel_unsnap(struct sf_toplevel *toplevel) {
	if (toplevel == NULL || !sf_layout_sm_is_edged(&toplevel->layout)) {
		return;
	}
	struct sf_layout_ev ev = {.kind = SF_LAYOUT_EV_UNSNAP};
	layout_dispatch(toplevel, &ev, NULL);
}

static void place_fill_ctx(const struct sf_toplevel *toplevel, struct sf_place_ctx *ctx) {
	*ctx = (struct sf_place_ctx){0};
	if (toplevel == NULL) {
		return;
	}
	ctx->layout_fixed = sf_layout_sm_is_fixed(&toplevel->layout);
	if (toplevel->xdg_toplevel != NULL) {
		ctx->has_parent = toplevel->xdg_toplevel->parent != NULL;
		const char *title = toplevel->xdg_toplevel->title;
		const char *app = toplevel->xdg_toplevel->app_id;
		ctx->partial_identity =
			(title != NULL && title[0] != '\0') || (app != NULL && app[0] != '\0');
	}
	ctx->wants_center = sf_rules_wants_center(toplevel);
	ctx->center_still_possible = sf_rules_center_still_possible(toplevel);
}

static void place_apply_result(struct sf_toplevel *toplevel, const struct sf_place_result *r) {
	toplevel->place = r->state;
	for (size_t i = 0; i < r->nfx; i++) {
		switch (r->fx[i].kind) {
		case SF_PLACE_FX_SCENE_DISABLE:
			if (toplevel->scene_tree != NULL) {
				wlr_scene_node_set_enabled(&toplevel->scene_tree->node, false);
			}
			break;
		case SF_PLACE_FX_SCENE_ENABLE:
			if (toplevel->scene_tree != NULL) {
				wlr_scene_node_set_enabled(&toplevel->scene_tree->node, true);
			}
			break;
		case SF_PLACE_FX_START_SHOW:
			if (!sf_anim_start_show(toplevel) && toplevel->scene_tree != NULL) {
				wlr_scene_node_set_position(&toplevel->scene_tree->node,
							    toplevel->resting_x,
							    toplevel->resting_y);
			}
			break;
		}
	}
}

static void place_dispatch(struct sf_toplevel *toplevel, enum sf_place_ev_kind kind) {
	struct sf_place_ctx ctx;
	place_fill_ctx(toplevel, &ctx);
	struct sf_place_ev ev = {.kind = kind};
	struct sf_place_result out;
	sf_place_sm_step(&toplevel->place, &ev, &ctx, &out);
	place_apply_result(toplevel, &out);
}

void sf_toplevel_clear_auto_place(struct sf_toplevel *toplevel) {
	if (toplevel == NULL) {
		return;
	}
	place_dispatch(toplevel, SF_PLACE_EV_USER_OWN);
}

void sf_toplevel_place_drop_soft(struct sf_toplevel *toplevel) {
	if (toplevel == NULL) {
		return;
	}
	place_dispatch(toplevel, SF_PLACE_EV_DROP_SOFT);
}

void sf_toplevel_place_soft_center(struct sf_toplevel *toplevel) {
	if (toplevel == NULL) {
		return;
	}
	place_dispatch(toplevel, SF_PLACE_EV_SOFT_CENTER);
}

void sf_toplevel_try_appear(struct sf_toplevel *toplevel) {
	if (toplevel == NULL) {
		return;
	}
	place_dispatch(toplevel, SF_PLACE_EV_TRY_APPEAR);
}

void sf_toplevel_arm_appear(struct sf_toplevel *toplevel) {
	if (toplevel == NULL || toplevel->scene_tree == NULL) {
		return;
	}
	place_dispatch(toplevel, SF_PLACE_EV_ARM_APPEAR);
}

void sf_toplevel_place_cascade(struct sf_toplevel *toplevel) {
	struct sf_server *server = toplevel->server;
	struct wlr_box out = {0};
	/* Prefer the usable area under the cursor for brand-new maps. */
	if (!sf_layout_usable_at(server, server->cursor->x, server->cursor->y, &out)) {
		if (!sf_toplevel_output_box(toplevel, &out)) {
			return;
		}
	}
	const struct sf_config *cfg = sf_cfg(server);
	sf_box_inset_gap(&out, cfg->gap_size);

	/* Count siblings already on the strip (map inserts before place). */
	size_t n = 0;
	struct sf_toplevel *t;
	wl_list_for_each(t, &toplevel->server->toplevels, link) {
		if (t != toplevel && !t->minimized && !sf_toplevel_is_scratchpad(t)) {
			n++;
		}
	}
	int wrap = cfg->cascade_wrap > 0 ? cfg->cascade_wrap : 1;
	int offset = (int)(n % (size_t)wrap) * cfg->cascade_step;
	int top_inset = sf_ssd_top_inset(toplevel);
	int x = out.x + offset;
	int y = out.y + top_inset + offset;
	toplevel_set_resting(toplevel, x, y);
}

static struct sf_toplevel *toplevel_from_xdg_parent(struct wlr_xdg_toplevel *xdg) {
	if (xdg == NULL || xdg->base == NULL || xdg->base->data == NULL) {
		return NULL;
	}
	struct wlr_scene_tree *tree = xdg->base->data;
	if (tree->node.data == NULL) {
		return NULL;
	}
	struct sf_toplevel *t = tree->node.data;
	if (t->kind != SF_NODE_TOPLEVEL || t->xdg_toplevel != xdg) {
		return NULL;
	}
	return t;
}

/* Visible parent suitable as a centering anchor (mapped, not hidden). */
static struct sf_toplevel *visible_parent(struct sf_toplevel *toplevel) {
	if (toplevel == NULL || toplevel->xdg_toplevel == NULL) {
		return NULL;
	}
	struct sf_toplevel *parent = toplevel_from_xdg_parent(toplevel->xdg_toplevel->parent);
	if (parent == NULL || parent->minimized || parent->anim_hide) {
		return NULL;
	}
	if (parent->scene_tree == NULL || !parent->scene_tree->node.enabled) {
		return NULL;
	}
	return parent;
}

static void placement_size(struct sf_toplevel *toplevel, int *w, int *h) {
	struct wlr_box geom;
	sf_toplevel_layout_geom(toplevel, &geom);
	*w = geom.width;
	*h = geom.height;
	if (*w > 0 && *h > 0) {
		return;
	}
	/* Prefer client min hints over 1×1 so late commits re-center cleanly. */
	if (toplevel->xdg_toplevel != NULL) {
		int32_t mw = toplevel->xdg_toplevel->current.min_width;
		int32_t mh = toplevel->xdg_toplevel->current.min_height;
		if (*w <= 0 && mw > 0) {
			*w = mw;
		}
		if (*h <= 0 && mh > 0) {
			*h = mh;
		}
	}
	if (*w <= 0) {
		*w = 1;
	}
	if (*h <= 0) {
		*h = 1;
	}
}

static void clamp_pos_to_area(int *x, int *y, int w, int h, const struct wlr_box *area,
			      int top_inset) {
	if (area == NULL || area->width <= 0 || area->height <= 0) {
		return;
	}
	int min_y = area->y + top_inset;
	if (*x + w > area->x + area->width) {
		*x = area->x + area->width - w;
	}
	if (*y + h > area->y + area->height) {
		*y = area->y + area->height - h;
	}
	if (*x < area->x) {
		*x = area->x;
	}
	if (*y < min_y) {
		*y = min_y;
	}
}

static bool place_on_parent(struct sf_toplevel *toplevel) {
	struct sf_toplevel *parent = visible_parent(toplevel);
	if (parent == NULL) {
		return false;
	}

	struct wlr_box parent_geom;
	sf_toplevel_layout_geom(parent, &parent_geom);

	struct wlr_box area = {0};
	double pcx = parent_geom.x + parent_geom.width / 2.0;
	double pcy = parent_geom.y + parent_geom.height / 2.0;
	if (!sf_layout_usable_at(toplevel->server, pcx, pcy, &area)) {
		if (!sf_toplevel_output_box(toplevel, &area)) {
			return false;
		}
	}
	sf_box_inset_gap(&area, sf_cfg(toplevel->server)->gap_size);

	struct wlr_box anchor = parent_geom;
	if (anchor.width <= 0 || anchor.height <= 0) {
		anchor = area;
	}

	int w, h;
	placement_size(toplevel, &w, &h);
	int top_inset = sf_ssd_top_inset(toplevel);
	int x = anchor.x + (anchor.width - w) / 2;
	int y = anchor.y + (anchor.height - h) / 2;
	clamp_pos_to_area(&x, &y, w, h, &area, top_inset);
	toplevel_set_resting(toplevel, x, y);

	/* Stack above the parent in the same scene tree when possible. */
	if (toplevel->scene_tree != NULL && parent->scene_tree != NULL &&
	    toplevel->scene_tree->node.parent == parent->scene_tree->node.parent) {
		wlr_scene_node_place_above(&toplevel->scene_tree->node, &parent->scene_tree->node);
		sf_pinned_restack(toplevel->server);
	} else {
		sf_toplevel_raise(toplevel);
	}
	return true;
}

static bool place_on_usable_center(struct sf_toplevel *toplevel) {
	struct wlr_box area = {0};
	struct sf_server *server = toplevel->server;
	if (!sf_layout_usable_at(server, server->cursor->x, server->cursor->y, &area)) {
		if (!sf_toplevel_output_box(toplevel, &area)) {
			return false;
		}
	}
	sf_box_inset_gap(&area, sf_cfg(server)->gap_size);
	if (area.width <= 0 || area.height <= 0) {
		return false;
	}
	int w, h;
	placement_size(toplevel, &w, &h);
	int top_inset = sf_ssd_top_inset(toplevel);
	int x = area.x + (area.width - w) / 2;
	int y = area.y + (area.height - h) / 2;
	clamp_pos_to_area(&x, &y, w, h, &area, top_inset);
	toplevel_set_resting(toplevel, x, y);
	return true;
}

void sf_toplevel_place_transient(struct sf_toplevel *toplevel) {
	if (toplevel == NULL || !sf_place_sm_is_managed(&toplevel->place) ||
	    toplevel->scene_tree == NULL) {
		return;
	}
	if (sf_layout_sm_is_fixed(&toplevel->layout)) {
		return;
	}
	if (place_on_parent(toplevel)) {
		sf_toplevel_ensure_on_screen(toplevel);
		return;
	}
	if (sf_rules_wants_center(toplevel) && place_on_usable_center(toplevel)) {
		sf_toplevel_ensure_on_screen(toplevel);
	}
}

void sf_toplevel_place_new(struct sf_toplevel *toplevel) {
	if (toplevel == NULL || toplevel->scene_tree == NULL) {
		return;
	}
	place_dispatch(toplevel, SF_PLACE_EV_BEGIN);
	sf_rules_apply(toplevel);
	/* Rule :action may have maximized/snapped — do not center/cascade over it
	 * (would gap-inset + retarget morph → offset full-size window). */
	if (sf_layout_sm_is_fixed(&toplevel->layout)) {
		place_dispatch(toplevel, SF_PLACE_EV_LAYOUT_OWNED);
		return;
	}
	if (place_on_parent(toplevel)) {
		sf_toplevel_ensure_on_screen(toplevel);
		return;
	}
	if (sf_rules_wants_center(toplevel) && place_on_usable_center(toplevel)) {
		sf_toplevel_ensure_on_screen(toplevel);
		return;
	}
	sf_toplevel_place_cascade(toplevel);
}

/* minimize_link is either self-linked (off stack) or in server->minimized. */
static void minimize_link_detach(struct sf_toplevel *toplevel) {
	if (toplevel->minimize_link.next != &toplevel->minimize_link) {
		wl_list_remove(&toplevel->minimize_link);
	}
	wl_list_init(&toplevel->minimize_link);
}

static bool toplevel_focusable(const struct sf_toplevel *toplevel) {
	/* anim_hide: sliding out but minimized not set yet — do not re-pick. */
	return !toplevel->minimized && !toplevel->anim_hide &&
	       sf_toplevel_workspace_visible(toplevel);
}

void sf_toplevel_apply_hidden(struct sf_toplevel *toplevel, enum sf_hide_kind kind) {
	struct sf_server *server = toplevel->server;
	const bool was_hidden = toplevel->minimized;
	const bool was_focused = sf_focused_toplevel(server) == toplevel;

	minimize_link_detach(toplevel);
	if (kind == SF_HIDE_LIFO) {
		wl_list_insert(&server->minimized, &toplevel->minimize_link);
	}

	if (!was_hidden) {
		toplevel->minimized = true;
		wlr_scene_node_set_enabled(&toplevel->scene_tree->node, false);
	}

	if (was_focused) {
		if (xdg_surface_ready(toplevel)) {
			wlr_xdg_toplevel_set_activated(toplevel->xdg_toplevel, false);
		}
		sf_decor_set_focused(toplevel, false);
		sf_focus_top(server);
	}
	sf_foreign_toplevel_on_state_change(toplevel);
}

void sf_toplevel_apply_shown(struct sf_toplevel *toplevel, bool activate) {
	if (toplevel->minimized) {
		/* LIFO unminimize lands on the output's current active workspace. */
		if (!sf_toplevel_is_scratchpad(toplevel)) {
			struct sf_output *out = NULL;
			if (toplevel->workspace != NULL) {
				out = toplevel->workspace->output;
			}
			if (out == NULL) {
				out = sf_output_for_toplevel(toplevel);
			}
			if (out != NULL && out->active != NULL &&
			    toplevel->workspace != out->active) {
				sf_workspace_assign(toplevel, out->active);
			}
		}
		toplevel->minimized = false;
		minimize_link_detach(toplevel);
		/* Enable at resting, then slide in from off-screen. */
		wlr_scene_node_set_position(&toplevel->scene_tree->node, toplevel->resting_x,
					    toplevel->resting_y);
		wlr_scene_node_set_enabled(&toplevel->scene_tree->node, true);
		sf_anim_start_show(toplevel);
		sf_foreign_toplevel_on_state_change(toplevel);
	}
	if (activate) {
		sf_focus_toplevel(toplevel);
	}
}

static void minimize_now(struct sf_toplevel *toplevel) {
	sf_toplevel_apply_hidden(toplevel, SF_HIDE_LIFO);
}

void sf_toplevel_unminimize(struct sf_toplevel *toplevel) {
	sf_toplevel_apply_shown(toplevel, false);
}

void sf_pinned_restack(struct sf_server *server) {
	if (server == NULL) {
		return;
	}
	struct sf_toplevel *t;
	/* Oldest pinned first so MRU pinned ends on top. */
	wl_list_for_each_reverse(t, &server->toplevels, link) {
		if (!t->always_on_top || t->minimized || t->scene_tree == NULL) {
			continue;
		}
		if (!sf_toplevel_workspace_visible(t)) {
			continue;
		}
		wlr_scene_node_raise_to_top(&t->scene_tree->node);
	}
	/* Hide/close slides stay above sticky (peeble reapplyHideAnimating). */
	wl_list_for_each_reverse(t, &server->toplevels, link) {
		if (!t->anim_hide || t->scene_tree == NULL) {
			continue;
		}
		wlr_scene_node_raise_to_top(&t->scene_tree->node);
	}
}

void sf_toplevel_raise(struct sf_toplevel *toplevel) {
	if (toplevel == NULL || toplevel->scene_tree == NULL) {
		return;
	}
	wlr_scene_node_raise_to_top(&toplevel->scene_tree->node);
	sf_pinned_restack(toplevel->server);
}

void sf_toplevel_set_always_on_top(struct sf_toplevel *toplevel, bool on) {
	if (toplevel == NULL || toplevel->always_on_top == on) {
		return;
	}
	toplevel->always_on_top = on;
	if (on) {
		sf_toplevel_raise(toplevel);
	}
}

void sf_toplevel_toggle_always_on_top(struct sf_toplevel *toplevel) {
	if (toplevel == NULL) {
		return;
	}
	sf_toplevel_set_always_on_top(toplevel, !toplevel->always_on_top);
}

void sf_focus_toplevel(struct sf_toplevel *toplevel) {
	/* Note: this function only deals with keyboard focus. */
	if (toplevel == NULL) {
		return;
	}
	struct sf_server *server = toplevel->server;
	if (sf_session_locked(server)) {
		return;
	}
	/* Show before reveal so LIFO unminimize can reassign to the active
	 * workspace first (otherwise reveal would switch to the old strip). */
	if (toplevel->minimized) {
		sf_toplevel_apply_shown(toplevel, false);
	}
	sf_workspace_reveal_toplevel(toplevel);
	server->exclusive_layer = NULL;
	struct wlr_seat *seat = server->seat;
	struct wlr_surface *prev_surface = seat->keyboard_state.focused_surface;
	struct wlr_surface *surface = toplevel->xdg_toplevel->base->surface;
	if (prev_surface == surface) {
		/* Already focused (e.g. taskbar click on the active window before
		 * minimize). Still raise so the request is visible. */
		wl_list_remove(&toplevel->link);
		wl_list_insert(&server->toplevels, &toplevel->link);
		sf_toplevel_raise(toplevel);
		return;
	}
	struct sf_toplevel *prev_toplevel = NULL;
	if (prev_surface) {
		/*
		 * Deactivate the previously focused surface. This lets the client know
		 * it no longer has focus and the client will repaint accordingly, e.g.
		 * stop displaying a caret.
		 */
		struct wlr_xdg_toplevel *prev_xdg =
			wlr_xdg_toplevel_try_from_wlr_surface(prev_surface);
		if (prev_xdg != NULL) {
			wlr_xdg_toplevel_set_activated(prev_xdg, false);
			struct wlr_surface *root = wlr_surface_get_root_surface(prev_surface);
			struct sf_toplevel *t;
			wl_list_for_each(t, &server->toplevels, link) {
				if (t->xdg_toplevel->base->surface == root) {
					sf_decor_set_focused(t, false);
					prev_toplevel = t;
					break;
				}
			}
		}
	}
	struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(seat);
	/* MRU list first so pinned restack sees this window as front. */
	wl_list_remove(&toplevel->link);
	wl_list_insert(&server->toplevels, &toplevel->link);
	sf_toplevel_raise(toplevel);
	/* Activate the new surface */
	wlr_xdg_toplevel_set_activated(toplevel->xdg_toplevel, true);
	sf_decor_set_focused(toplevel, true);
	/*
	 * Tell the seat to have the keyboard enter this surface. wlroots will keep
	 * track of this and automatically send key events to the appropriate
	 * clients without additional work on your part.
	 */
	if (keyboard != NULL) {
		wlr_seat_keyboard_notify_enter(seat, surface, keyboard->keycodes,
					       keyboard->num_keycodes, &keyboard->modifiers);
	}
	if (prev_toplevel != NULL) {
		sf_foreign_toplevel_on_state_change(prev_toplevel);
	}
	sf_foreign_toplevel_on_state_change(toplevel);
	/* After the new window owns focus so auto_hide cannot focus_top-steal. */
	sf_scratchpad_on_focus_change(toplevel);
	/* Columns scroll / monocle focus need a re-pack. */
	if (toplevel->workspace != NULL) {
		sf_arrange_schedule(toplevel->workspace);
	}
}

struct sf_toplevel *sf_focused_toplevel(struct sf_server *server) {
	struct wlr_surface *focused = server->seat->keyboard_state.focused_surface;
	if (focused == NULL) {
		return NULL;
	}
	struct wlr_surface *root = wlr_surface_get_root_surface(focused);
	struct sf_toplevel *toplevel;
	wl_list_for_each(toplevel, &server->toplevels, link) {
		if (toplevel->xdg_toplevel->base->surface == root) {
			return toplevel;
		}
	}
	return NULL;
}

void sf_focus_top(struct sf_server *server) {
	if (sf_session_locked(server)) {
		return;
	}
	struct sf_toplevel *toplevel;
	wl_list_for_each(toplevel, &server->toplevels, link) {
		if (toplevel_focusable(toplevel) && !sf_toplevel_is_scratchpad(toplevel)) {
			sf_focus_toplevel(toplevel);
			return;
		}
	}
	/* Fall back to a visible scratchpad if nothing else remains. */
	wl_list_for_each(toplevel, &server->toplevels, link) {
		if (toplevel_focusable(toplevel)) {
			sf_focus_toplevel(toplevel);
			return;
		}
	}
	wlr_seat_keyboard_notify_clear_focus(server->seat);
}

void sf_toplevel_minimize(struct sf_toplevel *toplevel) {
	if (toplevel->minimized) {
		return;
	}
	/* Mid-show reverses into a hide (peeble beginSlide). */
	if (sf_anim_start_hide(toplevel, minimize_now)) {
		return;
	}
	minimize_now(toplevel);
}

void sf_unminimize_most_recent(struct sf_server *server) {
	if (wl_list_empty(&server->minimized)) {
		return;
	}
	struct sf_toplevel *toplevel =
		wl_container_of(server->minimized.next, toplevel, minimize_link);
	sf_focus_toplevel(toplevel);
}

static void close_after_hide(struct sf_toplevel *toplevel) {
	wlr_xdg_toplevel_send_close(toplevel->xdg_toplevel);
}

void sf_close_toplevel(struct sf_toplevel *toplevel) {
	if (toplevel == NULL) {
		return;
	}
	if (sf_anim_start_hide(toplevel, close_after_hide)) {
		return;
	}
	wlr_xdg_toplevel_send_close(toplevel->xdg_toplevel);
}

void sf_spawn_cmd(const char *cmd) {
	if (fork() == 0) {
		execl("/bin/sh", "/bin/sh", "-c", cmd, (void *)NULL);
		_exit(1);
	}
}

static bool focus_cycle_eligible(struct sf_toplevel *t, bool include_minimized) {
	if (sf_toplevel_is_scratchpad(t) || t->anim_hide) {
		return false;
	}
	if (!include_minimized && !sf_toplevel_workspace_visible(t)) {
		return false;
	}
	return include_minimized || !t->minimized;
}

void sf_focus_cycle(struct sf_server *server, bool backward, bool include_minimized) {
	if (wl_list_empty(&server->toplevels)) {
		return;
	}
	struct sf_toplevel *current = sf_focused_toplevel(server);
	struct sf_toplevel *candidate = NULL;

	if (current == NULL) {
		wl_list_for_each(candidate, &server->toplevels, link) {
			if (focus_cycle_eligible(candidate, include_minimized)) {
				sf_focus_toplevel(candidate);
				sf_pointer_follow_focus(server);
				return;
			}
		}
		return;
	}

	struct sf_toplevel *t = current;
	do {
		if (backward) {
			t = wl_container_of(t->link.next, t, link);
			if (&t->link == &server->toplevels) {
				t = wl_container_of(server->toplevels.next, t, link);
			}
		} else {
			/* Top of list is most recently focused; next is previous link. */
			t = wl_container_of(t->link.prev, t, link);
			if (&t->link == &server->toplevels) {
				t = wl_container_of(server->toplevels.prev, t, link);
			}
		}
		if (focus_cycle_eligible(t, include_minimized)) {
			sf_focus_toplevel(t);
			sf_pointer_follow_focus(server);
			return;
		}
	} while (t != current);
}
