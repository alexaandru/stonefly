#include "wm/layout_sm.h"

#include <stdlib.h>

#define SF_LAYOUT_TOL_DEFAULT 2

void sf_layout_sm_init(struct sf_layout_sm *s) {
	if (s == NULL) {
		return;
	}
	*s = (struct sf_layout_sm){0};
}

bool sf_layout_sm_box_near(const struct wlr_box *a, const struct wlr_box *b, int tol) {
	if (a == NULL || b == NULL) {
		return false;
	}
	if (tol < 0) {
		tol = SF_LAYOUT_TOL_DEFAULT;
	}
	return abs(a->x - b->x) <= tol && abs(a->y - b->y) <= tol &&
	       abs(a->width - b->width) <= tol && abs(a->height - b->height) <= tol;
}

static int ctx_tol(const struct sf_layout_ctx *ctx) {
	if (ctx == NULL || ctx->tol <= 0) {
		return SF_LAYOUT_TOL_DEFAULT;
	}
	return ctx->tol;
}

static bool box_ok(const struct wlr_box *b) {
	return b != NULL && b->width > 0 && b->height > 0;
}

static const struct wlr_box *snap_target(const struct sf_layout_ctx *ctx, enum sf_snap edge) {
	switch (edge) {
	case SF_SNAP_LEFT:
		return &ctx->snap_left;
	case SF_SNAP_RIGHT:
		return &ctx->snap_right;
	case SF_SNAP_TOP:
		return &ctx->snap_top;
	case SF_SNAP_BOTTOM:
		return &ctx->snap_bottom;
	case SF_SNAP_NONE:
		break;
	}
	return NULL;
}

static const struct wlr_box *mode_target(const struct sf_layout_sm *s,
					 const struct sf_layout_ctx *ctx) {
	switch (s->mode) {
	case SF_LAYOUT_MODE_MAX_EDGES:
		return &ctx->max_edges;
	case SF_LAYOUT_MODE_MAX_GAPS:
		return &ctx->max_gaps;
	case SF_LAYOUT_MODE_FULLSCREEN:
		return &ctx->fullscreen;
	case SF_LAYOUT_MODE_SNAP:
		return snap_target(ctx, s->snap);
	case SF_LAYOUT_MODE_FLOAT:
		break;
	}
	return NULL;
}

enum sf_layout_mode sf_layout_sm_match(const struct sf_layout_ctx *ctx, enum sf_snap *out_snap) {
	if (out_snap != NULL) {
		*out_snap = SF_SNAP_NONE;
	}
	if (ctx == NULL || !box_ok(&ctx->current)) {
		return SF_LAYOUT_MODE_FLOAT;
	}
	int tol = ctx_tol(ctx);
	if (box_ok(&ctx->fullscreen) &&
	    sf_layout_sm_box_near(&ctx->current, &ctx->fullscreen, tol)) {
		return SF_LAYOUT_MODE_FULLSCREEN;
	}
	if (box_ok(&ctx->max_edges) && sf_layout_sm_box_near(&ctx->current, &ctx->max_edges, tol)) {
		return SF_LAYOUT_MODE_MAX_EDGES;
	}
	static const enum sf_snap edges[] = {SF_SNAP_LEFT, SF_SNAP_RIGHT, SF_SNAP_TOP,
					     SF_SNAP_BOTTOM};
	for (size_t i = 0; i < sizeof(edges) / sizeof(edges[0]); i++) {
		const struct wlr_box *t = snap_target(ctx, edges[i]);
		if (t != NULL && box_ok(t) && sf_layout_sm_box_near(&ctx->current, t, tol)) {
			if (out_snap != NULL) {
				*out_snap = edges[i];
			}
			return SF_LAYOUT_MODE_SNAP;
		}
	}
	if (box_ok(&ctx->max_gaps) && sf_layout_sm_box_near(&ctx->current, &ctx->max_gaps, tol)) {
		return SF_LAYOUT_MODE_MAX_GAPS;
	}
	return SF_LAYOUT_MODE_FLOAT;
}

static void fx_push(struct sf_layout_result *out, struct sf_layout_effect e) {
	if (out->nfx >= SF_LAYOUT_FX_MAX) {
		return;
	}
	out->fx[out->nfx++] = e;
}

static void fx_apply_box(struct sf_layout_result *out, const struct wlr_box *box, bool morph) {
	if (!box_ok(box)) {
		return;
	}
	fx_push(out, (struct sf_layout_effect){
			     .kind = SF_LAYOUT_FX_APPLY_BOX,
			     .box = *box,
			     .morph = morph,
		     });
}

static void fx_xdg_max(struct sf_layout_result *out, bool on) {
	fx_push(out, (struct sf_layout_effect){
			     .kind = SF_LAYOUT_FX_XDG_MAXIMIZED,
			     .flag = on,
		     });
}

static void fx_xdg_fs(struct sf_layout_result *out, bool on) {
	fx_push(out, (struct sf_layout_effect){
			     .kind = SF_LAYOUT_FX_XDG_FULLSCREEN,
			     .flag = on,
		     });
}

static void fx_tiled(struct sf_layout_result *out, unsigned tiled) {
	fx_push(out, (struct sf_layout_effect){
			     .kind = SF_LAYOUT_FX_TILED,
			     .tiled = tiled,
		     });
}

static void fx_clear_auto(struct sf_layout_result *out) {
	fx_push(out, (struct sf_layout_effect){.kind = SF_LAYOUT_FX_CLEAR_AUTO_PLACE});
}

static void fx_decor(struct sf_layout_result *out) {
	fx_push(out, (struct sf_layout_effect){.kind = SF_LAYOUT_FX_DECOR});
}

static void fx_foreign(struct sf_layout_result *out, bool outputs) {
	fx_push(out, (struct sf_layout_effect){.kind = SF_LAYOUT_FX_FOREIGN_STATE});
	if (outputs) {
		fx_push(out, (struct sf_layout_effect){.kind = SF_LAYOUT_FX_FOREIGN_OUTPUTS});
	}
}

static void fx_configure(struct sf_layout_result *out) {
	fx_push(out, (struct sf_layout_effect){.kind = SF_LAYOUT_FX_SCHEDULE_CONFIGURE});
}

static void maybe_save(struct sf_layout_sm *s, const struct sf_layout_ctx *ctx) {
	if (s->has_saved || s->mode != SF_LAYOUT_MODE_FLOAT) {
		return;
	}
	if (!box_ok(&ctx->current)) {
		return;
	}
	s->saved = ctx->current;
	s->has_saved = true;
}

static void leave_protocol(struct sf_layout_result *out, enum sf_layout_mode was) {
	if (was == SF_LAYOUT_MODE_MAX_EDGES) {
		fx_xdg_max(out, false);
	}
	if (was == SF_LAYOUT_MODE_FULLSCREEN) {
		fx_xdg_fs(out, false);
	}
	if (was == SF_LAYOUT_MODE_MAX_EDGES || was == SF_LAYOUT_MODE_SNAP) {
		fx_tiled(out, SF_LAYOUT_TILED_NONE);
	}
}

static void enter_protocol(struct sf_layout_result *out, enum sf_layout_mode mode,
			   enum sf_snap snap) {
	switch (mode) {
	case SF_LAYOUT_MODE_MAX_EDGES:
		fx_xdg_max(out, true);
		fx_tiled(out, SF_LAYOUT_TILED_ALL);
		break;
	case SF_LAYOUT_MODE_MAX_GAPS:
		fx_tiled(out, SF_LAYOUT_TILED_NONE);
		break;
	case SF_LAYOUT_MODE_SNAP:
		fx_tiled(out, (unsigned)snap);
		break;
	case SF_LAYOUT_MODE_FULLSCREEN:
		fx_xdg_fs(out, true);
		fx_tiled(out, SF_LAYOUT_TILED_NONE);
		break;
	case SF_LAYOUT_MODE_FLOAT:
		break;
	}
}

static void restore_saved(struct sf_layout_result *out, struct sf_layout_sm *s,
			  enum sf_layout_mode was) {
	leave_protocol(out, was);
	s->mode = SF_LAYOUT_MODE_FLOAT;
	s->snap = SF_SNAP_NONE;
	fx_clear_auto(out);
	if (s->has_saved && box_ok(&s->saved)) {
		struct wlr_box to = s->saved;
		s->has_saved = false;
		bool morph = was != SF_LAYOUT_MODE_FULLSCREEN &&
			     (was == SF_LAYOUT_MODE_SNAP || was == SF_LAYOUT_MODE_MAX_EDGES ||
			      was == SF_LAYOUT_MODE_MAX_GAPS);
		fx_apply_box(out, &to, morph);
	} else {
		fx_configure(out);
	}
	fx_decor(out);
	fx_foreign(out, was == SF_LAYOUT_MODE_FULLSCREEN || was == SF_LAYOUT_MODE_MAX_EDGES ||
				was == SF_LAYOUT_MODE_MAX_GAPS);
}

static void enter_mode(struct sf_layout_result *out, struct sf_layout_sm *s,
		       const struct sf_layout_ctx *ctx, enum sf_layout_mode mode, enum sf_snap snap,
		       bool morph) {
	enum sf_layout_mode was = s->mode;
	if (was == mode && (mode != SF_LAYOUT_MODE_SNAP || s->snap == snap)) {
		fx_configure(out);
		return;
	}
	maybe_save(s, ctx);
	if (was == SF_LAYOUT_MODE_FULLSCREEN && mode != SF_LAYOUT_MODE_FULLSCREEN) {
		fx_xdg_fs(out, false);
	}
	if (was == SF_LAYOUT_MODE_MAX_EDGES && mode != SF_LAYOUT_MODE_MAX_EDGES) {
		fx_xdg_max(out, false);
	}
	s->mode = mode;
	s->snap = mode == SF_LAYOUT_MODE_SNAP ? snap : SF_SNAP_NONE;
	fx_clear_auto(out);
	enter_protocol(out, mode, snap);
	const struct wlr_box *target = mode_target(s, ctx);
	if (mode == SF_LAYOUT_MODE_FULLSCREEN) {
		fx_apply_box(out, target, false);
	} else {
		fx_apply_box(out, target, morph);
	}
	fx_decor(out);
	fx_foreign(out, true);
}

static void to_float_keep_geom(struct sf_layout_result *out, struct sf_layout_sm *s,
			       bool clear_saved) {
	enum sf_layout_mode was = s->mode;
	if (was == SF_LAYOUT_MODE_FLOAT && !clear_saved) {
		return;
	}
	leave_protocol(out, was);
	s->mode = SF_LAYOUT_MODE_FLOAT;
	s->snap = SF_SNAP_NONE;
	if (clear_saved) {
		s->has_saved = false;
		s->saved = (struct wlr_box){0};
	}
	fx_clear_auto(out);
	fx_decor(out);
	fx_foreign(out, was != SF_LAYOUT_MODE_FLOAT);
}

static void reconcile_geom(struct sf_layout_result *out, struct sf_layout_sm *s,
			   const struct sf_layout_ctx *ctx) {
	int tol = ctx_tol(ctx);
	const struct wlr_box *target = mode_target(s, ctx);
	if (s->mode != SF_LAYOUT_MODE_FLOAT) {
		if (target != NULL && box_ok(target) &&
		    sf_layout_sm_box_near(&ctx->current, target, tol)) {
			return;
		}
		/* Fullscreen stays until an explicit leave (client unset /
		 * SET_FULLSCREEN false / FLOAT). Chromium/Netflix often
		 * commits a mid-transition buffer that is not the full
		 * output box; demoting would drop the raise-above-bar layer. */
		if (s->mode == SF_LAYOUT_MODE_FULLSCREEN) {
			return;
		}
		/* Scratch slide / tiling overlay: keep edged until explicit
		 * leave. Mid-slide or CSD-size commits must not demote
		 * edge-max → float (QT borders/CSD on show). */
		if (ctx->no_geom_regain &&
		    (s->mode == SF_LAYOUT_MODE_MAX_EDGES || s->mode == SF_LAYOUT_MODE_MAX_GAPS ||
		     s->mode == SF_LAYOUT_MODE_SNAP)) {
			return;
		}
		/* Geometry left the arranged box → float. */
		to_float_keep_geom(out, s, false);
		return;
	}
	/* Floating: regain edged modes when the box matches again.
	 * Fullscreen is explicit-only (SET/TOGGLE/client request). Chromium
	 * often keeps a full-output buffer for a few frames after unset; GEOM
	 * regain would immediately re-enter and trap Esc/F11.
	 * Arrange-managed / break-out hold skip regain so engine boxes that
	 * match half-snap or gap-max are not stolen into fixed modes. */
	if (ctx->no_geom_regain) {
		return;
	}
	enum sf_snap snap = SF_SNAP_NONE;
	enum sf_layout_mode match = sf_layout_sm_match(ctx, &snap);
	if (match == SF_LAYOUT_MODE_FLOAT || match == SF_LAYOUT_MODE_FULLSCREEN) {
		return;
	}
	s->mode = match;
	s->snap = match == SF_LAYOUT_MODE_SNAP ? snap : SF_SNAP_NONE;
	/* Keep saved float geom for a later explicit restore. */
	enter_protocol(out, match, snap);
	fx_decor(out);
	fx_foreign(out, true);
}

static void reapply(struct sf_layout_result *out, struct sf_layout_sm *s,
		    const struct sf_layout_ctx *ctx) {
	if (s->mode == SF_LAYOUT_MODE_FLOAT) {
		return;
	}
	const struct wlr_box *target = mode_target(s, ctx);
	fx_apply_box(out, target, false);
	fx_decor(out);
	fx_foreign(out, true);
}

void sf_layout_sm_step(const struct sf_layout_sm *in, const struct sf_layout_ev *ev,
		       const struct sf_layout_ctx *ctx, struct sf_layout_result *out) {
	if (out == NULL) {
		return;
	}
	*out = (struct sf_layout_result){0};
	if (in != NULL) {
		out->state = *in;
	}
	if (ev == NULL || ctx == NULL) {
		return;
	}
	struct sf_layout_sm *s = &out->state;

	switch (ev->kind) {
	case SF_LAYOUT_EV_SET_MAX_EDGES:
		if (ev->want) {
			enter_mode(out, s, ctx, SF_LAYOUT_MODE_MAX_EDGES, SF_SNAP_NONE, true);
		} else if (s->mode == SF_LAYOUT_MODE_MAX_EDGES) {
			restore_saved(out, s, SF_LAYOUT_MODE_MAX_EDGES);
		}
		break;
	case SF_LAYOUT_EV_TOGGLE_MAX_EDGES:
		if (s->mode == SF_LAYOUT_MODE_MAX_EDGES) {
			restore_saved(out, s, SF_LAYOUT_MODE_MAX_EDGES);
		} else {
			enter_mode(out, s, ctx, SF_LAYOUT_MODE_MAX_EDGES, SF_SNAP_NONE, true);
		}
		break;
	case SF_LAYOUT_EV_SET_MAX_GAPS:
		if (ev->want) {
			enter_mode(out, s, ctx, SF_LAYOUT_MODE_MAX_GAPS, SF_SNAP_NONE, true);
		} else if (s->mode == SF_LAYOUT_MODE_MAX_GAPS) {
			restore_saved(out, s, SF_LAYOUT_MODE_MAX_GAPS);
		}
		break;
	case SF_LAYOUT_EV_TOGGLE_MAX_GAPS:
		if (s->mode == SF_LAYOUT_MODE_MAX_GAPS) {
			restore_saved(out, s, SF_LAYOUT_MODE_MAX_GAPS);
		} else {
			enter_mode(out, s, ctx, SF_LAYOUT_MODE_MAX_GAPS, SF_SNAP_NONE, true);
		}
		break;
	case SF_LAYOUT_EV_SNAP:
		if (ev->snap == SF_SNAP_NONE) {
			break;
		}
		enter_mode(out, s, ctx, SF_LAYOUT_MODE_SNAP, ev->snap, true);
		break;
	case SF_LAYOUT_EV_UNSNAP:
		if (s->mode == SF_LAYOUT_MODE_SNAP || s->mode == SF_LAYOUT_MODE_MAX_EDGES ||
		    s->mode == SF_LAYOUT_MODE_MAX_GAPS) {
			restore_saved(out, s, s->mode);
		}
		break;
	case SF_LAYOUT_EV_SET_FULLSCREEN:
		if (ev->want) {
			enter_mode(out, s, ctx, SF_LAYOUT_MODE_FULLSCREEN, SF_SNAP_NONE, false);
		} else if (s->mode == SF_LAYOUT_MODE_FULLSCREEN) {
			restore_saved(out, s, SF_LAYOUT_MODE_FULLSCREEN);
		}
		break;
	case SF_LAYOUT_EV_TOGGLE_FULLSCREEN:
		if (s->mode == SF_LAYOUT_MODE_FULLSCREEN) {
			restore_saved(out, s, SF_LAYOUT_MODE_FULLSCREEN);
		} else {
			enter_mode(out, s, ctx, SF_LAYOUT_MODE_FULLSCREEN, SF_SNAP_NONE, false);
		}
		break;
	case SF_LAYOUT_EV_FLOAT:
		to_float_keep_geom(out, s, ev->clear_saved);
		break;
	case SF_LAYOUT_EV_GEOM:
		reconcile_geom(out, s, ctx);
		break;
	case SF_LAYOUT_EV_REAPPLY:
		reapply(out, s, ctx);
		break;
	}
}
