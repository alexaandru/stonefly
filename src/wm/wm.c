#include <stdlib.h>
#include <string.h>
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
#include "wm/column.h"
#include "wm/dialog.h"
#include "wm/geom.h"
#include "wm/internal.h"
#include "wm/layout_sm.h"
#include "wm/place_sm.h"
#include "wm/rules.h"
#include "wm/scratchpad.h"
#include "wm/wm.h"
#include "wm/workspace.h"
#include "xdg/xdg.h"

void sf_toplevel_layout_geom(struct sf_toplevel *toplevel, struct wlr_box *box) {
	struct wlr_box geo;
	sf_toplevel_surface_geom(toplevel, &geo);
	/* Absolute layout coords — attached modals sit under parent.attachment_tree. */
	int lx = 0;
	int ly = 0;
	if (toplevel->scene_tree != NULL) {
		wlr_scene_node_coords(&toplevel->scene_tree->node, &lx, &ly);
	}
	box->x = lx;
	box->y = ly;
	box->width = geo.width;
	box->height = geo.height;
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

/* wlroots 0.20 asserts schedule_configure only after the first commit. */
static bool xdg_surface_ready(struct sf_toplevel *toplevel) {
	return sf_toplevel_is_initialized(toplevel);
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
	sf_toplevel_set_tiled_edges(toplevel, edges);
}

static void sf_toplevel_apply_box(struct sf_toplevel *toplevel, const struct wlr_box *box) {
	sf_anim_cancel(toplevel, false);
	wlr_scene_node_set_position(&toplevel->scene_tree->node, box->x, box->y);
	toplevel->resting_x = box->x;
	toplevel->resting_y = box->y;
	if (!xdg_surface_ready(toplevel)) {
		return;
	}
	sf_toplevel_set_pos(toplevel, box->x, box->y);
	sf_toplevel_set_size(toplevel, box->width, box->height);
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
	/* Mid appear slide/popin: never cancel into a short morph — retarget end. */
	if (toplevel->arrange_appear_pending && toplevel->anim.active && !toplevel->anim.morph &&
	    !toplevel->anim_hide) {
		toplevel->resting_x = box->x;
		toplevel->resting_y = box->y;
		toplevel->anim.to_x = box->x;
		toplevel->anim.to_y = box->y;
		if (toplevel->anim.pop && box->width > 0 && box->height > 0) {
			toplevel->anim.to_w = box->width;
			toplevel->anim.to_h = box->height;
		}
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
		sf_box_cover_fractional_gap(&ctx->max_edges,
					    sf_output_scale_factor(toplevel->server));
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
	if (toplevel->arrange_managed || toplevel->anim.active ||
	    sf_toplevel_is_scratchpad(toplevel)) {
		/* Engine tiles, show/hide slides, and scratchpads (QT): do not
		 * GEOM-demote edged modes from mid-slide / CSD size noise. */
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
	/* Modal dialogs stay under parent->attachment_tree. */
	if (toplevel->modal_attached_to != NULL) {
		return;
	}
	struct wlr_scene_tree *want;
	bool fs = sf_layout_sm_is_fullscreen(&toplevel->layout);
	if (fs) {
		want = toplevel->server->layers.fullscreen;
		/* Overview disables this tree while open; entering FS must not
		 * park the window under a disabled parent (looks like a no-op). */
		if (want != NULL && !toplevel->server->overview.active) {
			wlr_scene_node_set_enabled(&want->node, true);
		}
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
				sf_toplevel_set_maximized_state(toplevel, e->flag);
			}
			break;
		case SF_LAYOUT_FX_XDG_FULLSCREEN:
			if (e->flag) {
				/* Strip membership stays; drop cell present-size so
				 * decor cannot crop the FS buffer to the old tile. */
				toplevel->arrange_prop_w = 0;
				toplevel->arrange_prop_h = 0;
				toplevel->arrange_prop_waits = 0;
			}
			if (xdg_surface_ready(toplevel)) {
				sf_toplevel_set_fullscreen_state(toplevel, e->flag);
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
			if (toplevel->xdg_toplevel != NULL &&
			    toplevel->xdg_toplevel->base->initialized) {
				wlr_xdg_surface_schedule_configure(toplevel->xdg_toplevel->base);
			}
			break;
		}
	}
	sf_toplevel_sync_scene_layer(toplevel);
	sf_xdg_sync_toplevel(toplevel);
}

static void layout_dispatch(struct sf_toplevel *toplevel, const struct sf_layout_ev *ev,
			    struct wlr_output *fs_hint) {
	bool was_fs = sf_layout_sm_is_fullscreen(&toplevel->layout);
	struct sf_layout_ctx ctx;
	layout_fill_ctx(toplevel, fs_hint, &ctx);
	struct sf_layout_result out;
	sf_layout_sm_step(&toplevel->layout, ev, &ctx, &out);
	layout_apply_result(toplevel, &out);
	/* FS enter/exit changes whether the row paints in the strip. */
	if (was_fs != sf_layout_sm_is_fullscreen(&toplevel->layout) &&
	    toplevel->workspace != NULL && toplevel->column != NULL &&
	    sf_arrange_is_tiling(sf_workspace_arrange(toplevel->workspace))) {
		sf_arrange_schedule(toplevel->workspace);
	}
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
	struct sf_output *sf_out = NULL;
	/*
	 * Prefer cached output->full via workspace / sf_output_for_toplevel.
	 * Geom-centre layout lookup fails when columns scroll a cell off-usable
	 * (F11 no-op) and can miss floating Chrome too — mode then goes FS
	 * without APPLY_BOX, leaving the old ~90% window (Netflix symptom).
	 */
	if (hint != NULL) {
		struct sf_output *o;
		wl_list_for_each(o, &server->outputs, link) {
			if (o->wlr_output == hint) {
				sf_out = o;
				break;
			}
		}
	}
	if (sf_out == NULL) {
		sf_out = sf_output_for_toplevel(toplevel);
	}
	if (sf_out == NULL) {
		sf_out = sf_output_at_cursor(server);
	}
	if (sf_out != NULL && sf_out->full.width > 0 && sf_out->full.height > 0) {
		*box = sf_out->full;
	} else {
		struct wlr_output *wlr_output = hint;
		if (wlr_output == NULL && sf_out != NULL) {
			wlr_output = sf_out->wlr_output;
		}
		if (wlr_output == NULL) {
			struct wlr_box geom;
			sf_toplevel_layout_geom(toplevel, &geom);
			wlr_output = wlr_output_layout_output_at(server->output_layout,
								 geom.x + geom.width / 2.0,
								 geom.y + geom.height / 2.0);
			if (wlr_output == NULL) {
				wlr_output = wlr_output_layout_output_at(server->output_layout,
									 server->cursor->x,
									 server->cursor->y);
			}
		}
		if (wlr_output == NULL) {
			return false;
		}
		wlr_output_layout_get_box(server->output_layout, wlr_output, box);
		if (box->width <= 0 || box->height <= 0) {
			return false;
		}
	}
	/*
	 * Exact output logical size for the client configure. Fractional +1
	 * cover belongs on the opaque fs_backdrop only — growing the configure
	 * made Chrome lay out 2134×1334 on a 2133×1333 output and clipped the
	 * bottom transport. Max-edges still covers via layout_fill_targets.
	 */
	return true;
}

/* After FS enter: if APPLY_BOX was skipped (empty target), force output full. */
static void fullscreen_ensure_box(struct sf_toplevel *toplevel, struct wlr_output *hint) {
	if (toplevel == NULL || !sf_layout_sm_is_fullscreen(&toplevel->layout)) {
		return;
	}
	struct wlr_box want;
	if (!toplevel_full_output_box(toplevel, hint, &want)) {
		SF_LOGW("fullscreen: no output box", sf_i64("id", (int64_t)toplevel->id));
		return;
	}
	struct wlr_box cur;
	sf_toplevel_layout_geom(toplevel, &cur);
	/* Exact match: tol 2 would skip the +1 fractional cover. */
	if (cur.x == want.x && cur.y == want.y && cur.width == want.width &&
	    cur.height == want.height) {
		sf_decor_update_geom(toplevel);
		return;
	}
	SF_LOGI("fullscreen: apply output box", sf_i64("id", (int64_t)toplevel->id),
		sf_i64("x", want.x), sf_i64("y", want.y), sf_i64("w", want.width),
		sf_i64("h", want.height));
	/* Drop tiled present-size so decor cannot crop the FS buffer to a cell. */
	toplevel->arrange_prop_w = 0;
	toplevel->arrange_prop_h = 0;
	toplevel->arrange_prop_waits = 0;
	sf_toplevel_apply_box(toplevel, &want);
	sf_decor_update_geom(toplevel);
	sf_toplevel_sync_scene_layer(toplevel);
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
		sf_arrange_break_out(toplevel, true);
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

void sf_toplevel_break_edged_for_user_geom(struct sf_toplevel *toplevel) {
	if (toplevel == NULL || !xdg_surface_ready(toplevel)) {
		return;
	}
	/* Keep PreMax so an explicit unmax/toggle can still restore. */
	if (sf_layout_sm_is_edged(&toplevel->layout)) {
		sf_toplevel_layout_float(toplevel, false);
	}
	if (toplevel->arrange_max_edges_rejoin) {
		/* Transient tile overlay the user resized → sticky float at the
		 * new size (do not rejoin on focus / hide-show). */
		toplevel->arrange_max_edges_rejoin = false;
		toplevel->arrange_floating = true;
		sf_arrange_remember_float_box(toplevel);
		toplevel->arrange_float_custom = true;
		sf_decor_refresh_border(toplevel);
	}
}

static enum sf_arrange toplevel_ws_arrange(const struct sf_toplevel *toplevel) {
	if (toplevel == NULL || toplevel->workspace == NULL) {
		return SF_ARRANGE_STACKING;
	}
	return sf_workspace_arrange(toplevel->workspace);
}

void sf_toplevel_release_transient_max_edges(struct sf_toplevel *toplevel) {
	if (toplevel == NULL || !toplevel->arrange_max_edges_rejoin) {
		return;
	}
	/* Scratch/QT edge-max is sticky and layout-independent. */
	if (sf_toplevel_is_scratchpad(toplevel)) {
		toplevel->arrange_max_edges_rejoin = false;
		return;
	}
	/* Hide/show must restore the overlay as it left. apply_hidden →
	 * focus_top would otherwise demote on the way out, and apply_shown
	 * would then join the window as a tile. */
	if (toplevel->minimized || toplevel->anim_hide) {
		return;
	}
	if (!sf_layout_sm_is_max_edges(&toplevel->layout)) {
		toplevel->arrange_max_edges_rejoin = false;
		return;
	}
	sf_toplevel_set_max_edges(toplevel, false);
}

void sf_toplevel_set_max_edges(struct sf_toplevel *toplevel, bool want) {
	if (toplevel == NULL || !xdg_surface_ready(toplevel)) {
		return;
	}
	bool scratch = sf_toplevel_is_scratchpad(toplevel);
	/* Scratch/QT: leave the strip entirely; never arm transient rejoin. */
	if (want && scratch) {
		if (toplevel->arrange_managed || toplevel->column != NULL) {
			struct sf_workspace *ws = toplevel->workspace;
			sf_arrange_unlink(toplevel);
			sf_arrange_slot_clear(toplevel);
			toplevel->arrange_managed = false;
			toplevel->arrange_floating = false;
			toplevel->arrange_max_edges_rejoin = false;
			if (ws != NULL) {
				sf_arrange_schedule(ws);
			}
		} else {
			toplevel->arrange_max_edges_rejoin = false;
			toplevel->arrange_floating = false;
		}
	} else if (want && toplevel->arrange_floating) {
		/* Sticky float (rule / Super+v): edge-max is an overlay on the
		 * float — never arm tile rejoin or clear arrange_floating
		 * (Slack hide/show was joining the strip). */
		toplevel->arrange_max_edges_rejoin = false;
	} else if (want && toplevel->arrange_managed) {
		/* Tiling: break out + raise so the strip cannot cover it.
		 * Tile-originated max-edges is a transient overlay
		 * (arrange_max_edges_rejoin) — not sticky arrange_floating —
		 * so Super+m / pink float chrome stay honest and focus change
		 * can drop it back into its cell. Prior floats stay floating. */
		sf_arrange_break_out(toplevel, true);
		toplevel->arrange_max_edges_rejoin = true;
		toplevel->arrange_floating = false;
		sf_decor_refresh_border(toplevel);
	}
	if (want && sf_layout_sm_is_max_edges(&toplevel->layout)) {
		/* Already edged (QT show): re-apply usable box + chrome. */
		struct sf_layout_ev ev = {.kind = SF_LAYOUT_EV_REAPPLY};
		layout_dispatch(toplevel, &ev, NULL);
		sf_toplevel_raise(toplevel);
		return;
	}
	struct sf_layout_ev ev = {.kind = SF_LAYOUT_EV_SET_MAX_EDGES, .want = want};
	layout_dispatch(toplevel, &ev, NULL);
	if (want) {
		sf_toplevel_raise(toplevel);
	} else if (!scratch && toplevel->workspace != NULL && !toplevel->arrange_floating &&
		   sf_arrange_is_tiling(toplevel_ws_arrange(toplevel))) {
		/* Tile or arrange-switch survivor with a saved slot: rejoin.
		 * Sticky floats keep arrange_floating and stay out. */
		toplevel->arrange_max_edges_rejoin = false;
		sf_arrange_join_toplevel(toplevel);
		sf_arrange_schedule(toplevel->workspace);
		sf_decor_refresh_border(toplevel);
	} else {
		toplevel->arrange_max_edges_rejoin = false;
	}
}

void sf_toplevel_toggle_max_edges(struct sf_toplevel *toplevel) {
	if (toplevel == NULL || !xdg_surface_ready(toplevel)) {
		return;
	}
	if (sf_layout_sm_is_max_edges(&toplevel->layout)) {
		sf_toplevel_set_max_edges(toplevel, false);
	} else {
		sf_toplevel_set_max_edges(toplevel, true);
	}
}

void sf_toplevel_set_client_maximized(struct sf_toplevel *toplevel, bool want) {
	if (toplevel == NULL || toplevel->server == NULL) {
		return;
	}
	if (sf_cfg(toplevel->server)->client_maximize == SF_CLIENT_MAXIMIZE_EDGES) {
		sf_toplevel_set_max_edges(toplevel, want);
	} else {
		sf_toplevel_set_max_gaps(toplevel, want);
	}
}

void sf_toplevel_toggle_client_maximize(struct sf_toplevel *toplevel) {
	if (toplevel == NULL || toplevel->server == NULL) {
		return;
	}
	if (sf_cfg(toplevel->server)->client_maximize == SF_CLIENT_MAXIMIZE_EDGES) {
		sf_toplevel_toggle_max_edges(toplevel);
	} else {
		sf_toplevel_toggle_max_gaps(toplevel);
	}
}

static struct sf_output *toplevel_focus_output(struct sf_toplevel *toplevel) {
	if (toplevel == NULL) {
		return NULL;
	}
	if (toplevel->workspace != NULL && toplevel->workspace->output != NULL) {
		return toplevel->workspace->output;
	}
	return sf_output_for_toplevel(toplevel);
}

/* Exclusive fullscreen: at most one mapped FS window per output. */
static struct sf_toplevel *output_fullscreen_toplevel(struct sf_output *output) {
	if (output == NULL || output->server == NULL) {
		return NULL;
	}
	struct sf_toplevel *t;
	wl_list_for_each(t, &output->server->toplevels, link) {
		if (t->minimized || t->anim_hide) {
			continue;
		}
		if (!sf_layout_sm_is_fullscreen(&t->layout)) {
			continue;
		}
		if (toplevel_focus_output(t) == output) {
			return t;
		}
	}
	return NULL;
}

bool sf_toplevel_focus_obscured(struct sf_toplevel *toplevel) {
	if (toplevel == NULL || sf_layout_sm_is_fullscreen(&toplevel->layout)) {
		return false;
	}
	struct sf_output *output = toplevel_focus_output(toplevel);
	struct sf_toplevel *fs = output_fullscreen_toplevel(output);
	if (fs == NULL || fs == toplevel) {
		return false;
	}
	/* Parented dialog of the fullscreen window stays reachable. */
	if (sf_toplevel_parent(toplevel) == fs) {
		return false;
	}
	return true;
}

static void fullscreen_demote_peers(struct sf_toplevel *toplevel) {
	struct sf_output *output = toplevel_focus_output(toplevel);
	if (output == NULL || output->server == NULL) {
		return;
	}
	struct sf_toplevel *peers[64];
	size_t n = 0;
	struct sf_toplevel *t;
	wl_list_for_each(t, &output->server->toplevels, link) {
		if (t == toplevel || t->minimized || t->anim_hide) {
			continue;
		}
		if (!sf_layout_sm_is_fullscreen(&t->layout)) {
			continue;
		}
		if (toplevel_focus_output(t) != output) {
			continue;
		}
		if (n < sizeof(peers) / sizeof(peers[0])) {
			peers[n++] = t;
		}
	}
	for (size_t i = 0; i < n; i++) {
		struct sf_layout_ev ev = {.kind = SF_LAYOUT_EV_SET_FULLSCREEN, .want = false};
		layout_dispatch(peers[i], &ev, NULL);
	}
}

void sf_toplevel_set_fullscreen(struct sf_toplevel *toplevel, bool want, struct wlr_output *hint) {
	if (toplevel == NULL || !xdg_surface_ready(toplevel)) {
		return;
	}
	if (want) {
		fullscreen_demote_peers(toplevel);
	}
	struct sf_layout_ev ev = {.kind = SF_LAYOUT_EV_SET_FULLSCREEN, .want = want};
	layout_dispatch(toplevel, &ev, hint);
	if (want) {
		fullscreen_ensure_box(toplevel, hint);
	}
}

void sf_toplevel_toggle_fullscreen(struct sf_toplevel *toplevel) {
	if (toplevel == NULL || !xdg_surface_ready(toplevel)) {
		return;
	}
	if (!sf_layout_sm_is_fullscreen(&toplevel->layout)) {
		fullscreen_demote_peers(toplevel);
	}
	struct sf_layout_ev ev = {.kind = SF_LAYOUT_EV_TOGGLE_FULLSCREEN};
	layout_dispatch(toplevel, &ev, NULL);
	fullscreen_ensure_box(toplevel, NULL);
}

void sf_toplevel_set_max_gaps(struct sf_toplevel *toplevel, bool want) {
	if (toplevel == NULL || !xdg_surface_ready(toplevel)) {
		return;
	}
	enum sf_arrange kind = toplevel_ws_arrange(toplevel);
	/* Transient max-edges stole the column; drop it back first. */
	if (kind == SF_ARRANGE_COLUMNS && toplevel->arrange_max_edges_rejoin) {
		sf_toplevel_release_transient_max_edges(toplevel);
	}
	/* Columns: widen this column to usable (still tiled). Not layout_sm. */
	if (kind == SF_ARRANGE_COLUMNS && toplevel->column != NULL) {
		sf_column_set_full_width(toplevel->column, want);
		sf_arrange_schedule(toplevel->workspace);
		return;
	}
	/* Stacking (and floats): normal gap-max overlay. */
	struct sf_layout_ev ev = {.kind = SF_LAYOUT_EV_SET_MAX_GAPS, .want = want};
	layout_dispatch(toplevel, &ev, NULL);
	if (want) {
		sf_toplevel_raise(toplevel);
	}
}

void sf_toplevel_toggle_max_gaps(struct sf_toplevel *toplevel) {
	if (toplevel == NULL || !xdg_surface_ready(toplevel)) {
		return;
	}
	enum sf_arrange kind = toplevel_ws_arrange(toplevel);
	if (kind == SF_ARRANGE_COLUMNS && toplevel->arrange_max_edges_rejoin) {
		sf_toplevel_release_transient_max_edges(toplevel);
	}
	if (kind == SF_ARRANGE_COLUMNS && toplevel->column != NULL) {
		sf_column_toggle_full_width(toplevel->column);
		sf_arrange_schedule(toplevel->workspace);
		return;
	}
	if (sf_layout_sm_is_max_gaps(&toplevel->layout)) {
		sf_toplevel_set_max_gaps(toplevel, false);
	} else {
		sf_toplevel_set_max_gaps(toplevel, true);
	}
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
	if (toplevel == NULL) {
		return;
	}
	/* Half-snap is a stacking action; tiling uses focus_dir / swap. */
	if (toplevel->arrange_managed) {
		return;
	}
	if (toplevel->workspace != NULL &&
	    sf_arrange_is_tiling(sf_workspace_arrange(toplevel->workspace))) {
		return;
	}
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
	/* Hidden scratch/LIFO: ignore commits until shown again. */
	if (toplevel->minimized) {
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
	sf_xdg_sync_output(output);
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
	if (sf_toplevel_has_backend(toplevel)) {
		ctx->has_parent = sf_toplevel_has_parent(toplevel);
		const char *title = sf_toplevel_title(toplevel);
		const char *app = sf_toplevel_app_id(toplevel);
		ctx->title_known = title != NULL && title[0] != '\0';
		bool app_known = app != NULL && app[0] != '\0';
		ctx->partial_identity = ctx->title_known || app_known;
	}
	ctx->wants_center = sf_rules_wants_center(toplevel);
	ctx->center_still_possible = sf_rules_center_still_possible(toplevel);
	ctx->wants_float = sf_rules_wants_float(toplevel) || !sf_toplevel_looks_tiled(toplevel);
	ctx->float_still_possible = sf_rules_float_still_possible(toplevel);
}

/* Tiled appear waits for the client to commit near the packed cell size so
 * the slide runs at final height (not the natural mid-column square). */
static bool tiled_appear_size_ready(struct sf_toplevel *toplevel) {
	if (toplevel == NULL || !toplevel->arrange_managed) {
		return true;
	}
	if (toplevel->arrange_prop_w <= 0 || toplevel->arrange_prop_h <= 0) {
		return true;
	}
	struct wlr_box geom;
	sf_toplevel_layout_geom(toplevel, &geom);
	if (geom.width <= 0 || geom.height <= 0) {
		return false;
	}
	const int tol = 2;
	int dw = geom.width - toplevel->arrange_prop_w;
	int dh = geom.height - toplevel->arrange_prop_h;
	if (dw < 0) {
		dw = -dw;
	}
	if (dh < 0) {
		dh = -dh;
	}
	if (dw <= tol && dh <= tol) {
		return true;
	}
	/* Terminals often commit cell-snapped sizes that never match within 2px.
	 * After one post-propose commit, show anyway — invisible forever is worse. */
	return toplevel->arrange_prop_waits >= 1;
}

static bool place_fx_has(const struct sf_place_result *r, enum sf_place_fx_kind kind) {
	for (size_t i = 0; i < r->nfx; i++) {
		if (r->fx[i].kind == kind) {
			return true;
		}
	}
	return false;
}

static void appear_start_show(struct sf_toplevel *toplevel) {
	toplevel->arrange_prop_waits = 0;
	toplevel->arrange_appear_pending = true;
	/* Start popin/slide first, then enable — avoids a resting chrome flash. */
	bool slid = sf_anim_start_show(toplevel);
	if (toplevel->scene_tree != NULL) {
		if (!slid) {
			wlr_scene_node_set_position(&toplevel->scene_tree->node,
						    toplevel->resting_x, toplevel->resting_y);
			toplevel->arrange_appear_pending = false;
		}
		wlr_scene_node_set_enabled(&toplevel->scene_tree->node, true);
	}
	/* Neighbors deferred while appear-held: morph now on the opener's clock. */
	if (toplevel->workspace != NULL) {
		sf_arrange_release_deferred(toplevel->workspace);
	}
}

static void place_apply_result(struct sf_toplevel *toplevel, const struct sf_place_result *r) {
	toplevel->place = r->state;
	bool start_show = place_fx_has(r, SF_PLACE_FX_START_SHOW);
	for (size_t i = 0; i < r->nfx; i++) {
		switch (r->fx[i].kind) {
		case SF_PLACE_FX_SCENE_DISABLE:
			if (toplevel->scene_tree != NULL) {
				wlr_scene_node_set_enabled(&toplevel->scene_tree->node, false);
			}
			break;
		case SF_PLACE_FX_SCENE_ENABLE:
			/* START_SHOW parks then enables — do not flash at rest. */
			if (start_show || !tiled_appear_size_ready(toplevel)) {
				break;
			}
			if (toplevel->scene_tree != NULL) {
				wlr_scene_node_set_enabled(&toplevel->scene_tree->node, true);
			}
			break;
		case SF_PLACE_FX_START_SHOW:
			if (!tiled_appear_size_ready(toplevel)) {
				/* Pack proposed the cell; wait for commit. */
				toplevel->place.appear_hold = true;
				if (toplevel->scene_tree != NULL) {
					wlr_scene_node_set_enabled(&toplevel->scene_tree->node,
								   false);
				}
				break;
			}
			appear_start_show(toplevel);
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
	if (!tiled_appear_size_ready(toplevel)) {
		return;
	}
	if (sf_place_sm_is_holding(&toplevel->place)) {
		place_dispatch(toplevel, SF_PLACE_EV_TRY_APPEAR);
		return;
	}
	/* Size-wait without SoftPlace: START_SHOW armed appear_hold on the
	 * place copy, but a later settle can clear it while the scene is still
	 * disabled under arrange_appear_pending — force the slide. */
	if (toplevel->arrange_appear_pending && toplevel->scene_tree != NULL &&
	    !toplevel->scene_tree->node.enabled && !toplevel->anim.active) {
		appear_start_show(toplevel);
	}
}

void sf_toplevel_arm_appear(struct sf_toplevel *toplevel) {
	if (toplevel == NULL || toplevel->scene_tree == NULL) {
		return;
	}
	/* Pack the cell before the appear slide so travel uses final w×h.
	 * Flag pending *before* flush so neighbors defer on this pack — otherwise
	 * they morph into the gap and the opener's later slide looks sequential. */
	if (toplevel->arrange_managed && toplevel->workspace != NULL &&
	    sf_arrange_is_tiling(sf_workspace_arrange(toplevel->workspace))) {
		toplevel->arrange_appear_pending = true;
		sf_arrange_flush(toplevel->workspace);
	}
	place_dispatch(toplevel, SF_PLACE_EV_ARM_APPEAR);
}

void sf_toplevel_cascade_origin(struct sf_toplevel *toplevel, size_t index, int *x, int *y) {
	if (toplevel == NULL || x == NULL || y == NULL) {
		return;
	}
	struct sf_server *server = toplevel->server;
	if (server == NULL) {
		*x = toplevel->resting_x;
		*y = toplevel->resting_y;
		return;
	}
	struct wlr_box out = {0};
	if (server->cursor != NULL &&
	    sf_layout_usable_at(server, server->cursor->x, server->cursor->y, &out)) {
		/* Prefer usable under the cursor. */
	} else if (!sf_toplevel_output_box(toplevel, &out)) {
		*x = toplevel->resting_x;
		*y = toplevel->resting_y;
		return;
	}
	const struct sf_config *cfg = sf_cfg(server);
	sf_box_inset_gap(&out, cfg->gap_size);
	int wrap = cfg->cascade_wrap > 0 ? cfg->cascade_wrap : 1;
	int offset = (int)(index % (size_t)wrap) * cfg->cascade_step;
	int top_inset = sf_ssd_top_inset(toplevel);
	*x = out.x + offset;
	*y = out.y + top_inset + offset;
}

void sf_toplevel_place_cascade(struct sf_toplevel *toplevel) {
	if (toplevel == NULL || toplevel->server == NULL) {
		return;
	}
	/* Count siblings already on the strip (map inserts before place). */
	size_t n = 0;
	struct sf_toplevel *t;
	wl_list_for_each(t, &toplevel->server->toplevels, link) {
		if (t != toplevel && !t->minimized && !sf_toplevel_is_scratchpad(t)) {
			n++;
		}
	}
	int x = 0;
	int y = 0;
	sf_toplevel_cascade_origin(toplevel, n, &x, &y);
	toplevel_set_resting(toplevel, x, y);
}

/* Visible parent suitable as a centering anchor (mapped, not hidden). */
static struct sf_toplevel *visible_parent(struct sf_toplevel *toplevel) {
	struct sf_toplevel *parent = sf_toplevel_parent(toplevel);
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
	int mw = 0;
	int mh = 0;
	sf_toplevel_min_size(toplevel, &mw, &mh);
	if (*w <= 0 && mw > 0) {
		*w = mw;
	}
	if (*h <= 0 && mh > 0) {
		*h = mh;
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
	/* Umbriel looksTiled: sticky float + center (parent, else usable). */
	if (!sf_toplevel_looks_tiled(toplevel)) {
		if (!toplevel->arrange_floating) {
			sf_arrange_float(toplevel);
		}
		if (place_on_parent(toplevel) || place_on_usable_center(toplevel)) {
			sf_toplevel_ensure_on_screen(toplevel);
		}
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
	/* Umbriel looksTiled float heuristic (parent / X types / fixed size). */
	if (!sf_toplevel_looks_tiled(toplevel)) {
		sf_arrange_float(toplevel);
		if (place_on_parent(toplevel) || place_on_usable_center(toplevel)) {
			sf_toplevel_ensure_on_screen(toplevel);
		} else {
			sf_toplevel_place_cascade(toplevel);
		}
	} else if (place_on_parent(toplevel) ||
		   (sf_rules_wants_center(toplevel) && place_on_usable_center(toplevel))) {
		sf_toplevel_ensure_on_screen(toplevel);
	} else {
		sf_toplevel_place_cascade(toplevel);
	}
	/* Natural size before columns pack — rule float / stacking leave restore
	 * this, not the packed cell. Skip empty geom (client has not sized yet). */
	if (!toplevel->arrange_floating && !toplevel->arrange_float_saved &&
	    (toplevel->arrange_managed || toplevel->column != NULL ||
	     (toplevel->workspace != NULL &&
	      sf_workspace_arrange(toplevel->workspace) == SF_ARRANGE_COLUMNS))) {
		struct wlr_box natural;
		sf_toplevel_layout_geom(toplevel, &natural);
		if (natural.width >= 50 && natural.height >= 50) {
			sf_arrange_remember_float_box(toplevel);
		}
	}
	/* Join may have been deferred while float SoftPlace was undecided. */
	if (!toplevel->arrange_floating && toplevel->workspace != NULL &&
	    sf_arrange_is_tiling(sf_workspace_arrange(toplevel->workspace))) {
		sf_arrange_join_toplevel(toplevel);
		sf_arrange_schedule(toplevel->workspace);
	}
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
		sf_toplevel_set_minimized_state(toplevel, true);
		wlr_scene_node_set_enabled(&toplevel->scene_tree->node, false);
	}

	if (was_focused) {
		if (xdg_surface_ready(toplevel)) {
			sf_toplevel_set_activated(toplevel, false);
		}
		sf_decor_set_focused(toplevel, false);
		sf_focus_top(server);
	}
	sf_foreign_toplevel_on_state_change(toplevel);
	sf_xdg_sync_toplevel(toplevel);
	/* Columns pack only visible rows — reflow so a lone visible column fills. */
	if (!was_hidden && toplevel->workspace != NULL &&
	    sf_arrange_is_tiling(sf_workspace_arrange(toplevel->workspace))) {
		sf_arrange_schedule(toplevel->workspace);
	}
}

void sf_toplevel_apply_shown(struct sf_toplevel *toplevel, bool activate) {
	if (toplevel->minimized) {
		/* Default active: carry to the output's current workspace (hide on
		 * WS1, switch, unhide → lands with you). home: stay on hide WS. */
		if (!sf_toplevel_is_scratchpad(toplevel) && toplevel->server != NULL &&
		    sf_cfg(toplevel->server)->unminimize_workspace == SF_UNMINIMIZE_WS_ACTIVE) {
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
		/* Edge-max (QT via taskbar, column transient overlay, floats that
		 * were edge-max): reassert usable box while still hidden so
		 * apply_box updates resting before the show slide. A post-show
		 * REAPPLY cancels the slide (see scratch_show). */
		if (sf_layout_sm_is_max_edges(&toplevel->layout) ||
		    toplevel->arrange_max_edges_rejoin) {
			sf_toplevel_set_max_edges(toplevel, true);
		}
		toplevel->minimized = false;
		sf_toplevel_set_minimized_state(toplevel, false);
		minimize_link_detach(toplevel);
		/* Park off-screen, then enable (no resting chrome flash). */
		if (!sf_anim_start_show(toplevel)) {
			wlr_scene_node_set_position(&toplevel->scene_tree->node,
						    toplevel->resting_x, toplevel->resting_y);
		}
		wlr_scene_node_set_enabled(&toplevel->scene_tree->node, true);
		sf_foreign_toplevel_on_state_change(toplevel);
		/* Scratch/QT stays layout-independent. Tiling: non-floats must
		 * rejoin the strip (umbriel: cannot hold a hidden tile under
		 * the columns). join_toplevel already skips max-edges / rejoin.
		 * Rule :float (e.g. Slack) must stay sticky across hide/show. */
		if (!sf_toplevel_is_scratchpad(toplevel) && toplevel->workspace != NULL &&
		    !toplevel->arrange_floating && !sf_rules_wants_float(toplevel) &&
		    sf_toplevel_looks_tiled(toplevel) &&
		    sf_arrange_is_tiling(sf_workspace_arrange(toplevel->workspace))) {
			sf_arrange_join_toplevel(toplevel);
			sf_arrange_schedule(toplevel->workspace);
		}
	}
	if (activate) {
		sf_focus_toplevel(toplevel);
	}
	/* Slide show never reaches pop_finish. Pop show already update_geom'd. */
	if (!toplevel->minimized && toplevel->scene_tree != NULL &&
	    toplevel->scene_tree->node.enabled && !(toplevel->anim.active && toplevel->anim.pop)) {
		sf_decor_update_geom(toplevel);
	}
	sf_xdg_sync_toplevel(toplevel);
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
	/* Umbriel: placement root for reveal; keyboard on deepest modal child. */
	while (toplevel->modal_attached_to != NULL) {
		toplevel = toplevel->modal_attached_to;
	}
	toplevel = sf_toplevel_modal_input_delegate(toplevel);
	if (toplevel == NULL) {
		return;
	}
	/* Explicit focus (taskbar / click / IPC): leave fullscreen first so the
	 * target is visible. Tab/focus_dir skip buried windows instead. */
	if (sf_toplevel_focus_obscured(toplevel)) {
		struct sf_toplevel *fs =
			output_fullscreen_toplevel(toplevel_focus_output(toplevel));
		if (fs != NULL) {
			sf_toplevel_set_fullscreen(fs, false, NULL);
		}
	}
	/* Show before reveal so LIFO unminimize can reassign to the active
	 * workspace first (otherwise reveal would switch to the old strip). */
	if (toplevel->minimized) {
		sf_toplevel_apply_shown(toplevel, false);
	}
	/* SoftPlace/appear can leave the scene parked (foreign + overview still
	 * list it). Explicit focus must unpark — raise alone does not enable. */
	if (!toplevel->minimized && toplevel->scene_tree != NULL &&
	    !toplevel->scene_tree->node.enabled) {
		toplevel->place.appear_hold = false;
		appear_start_show(toplevel);
	}
	sf_workspace_reveal_toplevel(toplevel);
	server->exclusive_layer = NULL;
	struct wlr_seat *seat = server->seat;
	struct wlr_surface *prev_surface = seat->keyboard_state.focused_surface;
	struct wlr_surface *surface = sf_toplevel_wlr_surface(toplevel);
	if (surface == NULL) {
		return;
	}
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
		struct wlr_surface *root = wlr_surface_get_root_surface(prev_surface);
		struct sf_toplevel *t;
		wl_list_for_each(t, &server->toplevels, link) {
			if (sf_toplevel_wlr_surface(t) == root) {
				sf_toplevel_set_activated(t, false);
				sf_decor_set_focused(t, false);
				prev_toplevel = t;
				break;
			}
		}
	}
	/* Columns max-edges is transient: leaving focus drops it back into its cell. */
	if (prev_toplevel != NULL && prev_toplevel != toplevel) {
		sf_toplevel_release_transient_max_edges(prev_toplevel);
	}

	struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(seat);
	/* MRU list first so pinned restack sees this window as front. */
	wl_list_remove(&toplevel->link);
	wl_list_insert(&server->toplevels, &toplevel->link);
	sf_toplevel_raise(toplevel);
	/* Activate the new surface */
	sf_toplevel_set_activated(toplevel, true);
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
	/* Columns scroll on focus needs a re-pack. */
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
		if (sf_toplevel_wlr_surface(toplevel) == root) {
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

void sf_close_toplevel(struct sf_toplevel *toplevel) {
	/* A close request can be declined. Unmap owns the ghost and reflow. */
	if (toplevel != NULL) {
		sf_toplevel_send_close(toplevel);
	}
}

static bool focus_cycle_eligible(struct sf_toplevel *t, bool include_minimized) {
	if (sf_toplevel_is_scratchpad(t) || t->anim_hide) {
		return false;
	}
	if (!include_minimized && !sf_toplevel_workspace_visible(t)) {
		return false;
	}
	if (!(include_minimized || !t->minimized)) {
		return false;
	}
	/* Do not Tab onto windows hidden behind an exclusive fullscreen. */
	if (sf_toplevel_focus_obscured(t)) {
		return false;
	}
	return true;
}

void sf_focus_cycle(struct sf_server *server, bool backward, bool include_minimized) {
	if (wl_list_empty(&server->toplevels)) {
		return;
	}
	struct sf_toplevel *current = sf_focused_toplevel(server);
	struct sf_toplevel *candidate = NULL;

	if (current == NULL) {
		for (struct wl_list *n = server->toplevels.next;
		     n != NULL && n != &server->toplevels; n = n->next) {
			candidate = wl_container_of(n, candidate, link);
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
