/* Pure window-layout mode machine: commands + geometry observations.
 * No wlroots / compositor types beyond struct wlr_box (test-stub friendly). */
#ifndef SF_WM_LAYOUT_SM_H
#define SF_WM_LAYOUT_SM_H

#include <stdbool.h>
#include <stddef.h>
#include <wlr/util/box.h>

#include "wm/snap.h"

enum sf_layout_mode {
	SF_LAYOUT_MODE_FLOAT = 0,
	SF_LAYOUT_MODE_MAX_EDGES,
	SF_LAYOUT_MODE_MAX_GAPS,
	SF_LAYOUT_MODE_SNAP,
	SF_LAYOUT_MODE_FULLSCREEN,
};

struct sf_layout_sm {
	enum sf_layout_mode mode;
	enum sf_snap snap; /* meaningful when mode == SNAP */
	struct wlr_box saved;
	bool has_saved;
};

enum sf_layout_ev_kind {
	/* Commands */
	SF_LAYOUT_EV_SET_MAX_EDGES, /* .want */
	SF_LAYOUT_EV_TOGGLE_MAX_EDGES,
	SF_LAYOUT_EV_SET_MAX_GAPS, /* .want */
	SF_LAYOUT_EV_TOGGLE_MAX_GAPS,
	SF_LAYOUT_EV_SNAP, /* .snap */
	SF_LAYOUT_EV_UNSNAP,
	SF_LAYOUT_EV_SET_FULLSCREEN, /* .want */
	SF_LAYOUT_EV_TOGGLE_FULLSCREEN,
	/* Drop max/snap/fs; keep current geom. .clear_saved drops PreMax. */
	SF_LAYOUT_EV_FLOAT,
	/* Observations */
	/* Reconcile mode vs ctx->current. Fullscreen is sticky (no demote). */
	SF_LAYOUT_EV_GEOM,
	/* Usable/output changed: re-emit apply for the current mode. */
	SF_LAYOUT_EV_REAPPLY,
};

struct sf_layout_ev {
	enum sf_layout_ev_kind kind;
	bool want;
	enum sf_snap snap;
	bool clear_saved;
};

/*
 * Targets are computed by the WM (usable, gap, SSD inset, fractional cover)
 * so the machine stays free of compositor policy.
 */
struct sf_layout_ctx {
	struct wlr_box current;
	struct wlr_box max_edges;
	struct wlr_box max_gaps;
	struct wlr_box fullscreen;
	struct wlr_box snap_left;
	struct wlr_box snap_right;
	struct wlr_box snap_top;
	struct wlr_box snap_bottom;
	/* Boxes match within this many px per edge (≤0 → default 2). */
	int tol;
	/* Skip FLOAT→edged regain and edged→FLOAT demotion (arrange-managed,
	 * scratch/slide, post break-out hold). */
	bool no_geom_regain;
};

enum sf_layout_fx_kind {
	SF_LAYOUT_FX_APPLY_BOX = 1,  /* .box, .morph */
	SF_LAYOUT_FX_XDG_MAXIMIZED,  /* .flag */
	SF_LAYOUT_FX_XDG_FULLSCREEN, /* .flag */
	SF_LAYOUT_FX_TILED,	     /* .tiled: NONE / ALL / SF_SNAP_* */
	SF_LAYOUT_FX_CLEAR_AUTO_PLACE,
	SF_LAYOUT_FX_DECOR,
	SF_LAYOUT_FX_FOREIGN_STATE,
	SF_LAYOUT_FX_FOREIGN_OUTPUTS,
	SF_LAYOUT_FX_SCHEDULE_CONFIGURE,
};

/* .tiled values for SF_LAYOUT_FX_TILED */
#define SF_LAYOUT_TILED_NONE 0u
#define SF_LAYOUT_TILED_ALL 0xffu

struct sf_layout_effect {
	enum sf_layout_fx_kind kind;
	struct wlr_box box;
	bool morph;
	bool flag;
	unsigned tiled;
};

#define SF_LAYOUT_FX_MAX 12

struct sf_layout_result {
	struct sf_layout_sm state;
	struct sf_layout_effect fx[SF_LAYOUT_FX_MAX];
	size_t nfx;
};

void sf_layout_sm_init(struct sf_layout_sm *s);

/* Reduce: in + event + ctx → next state + effects. out may alias in's storage
 * only after copy; pass distinct out. */
void sf_layout_sm_step(const struct sf_layout_sm *in, const struct sf_layout_ev *ev,
		       const struct sf_layout_ctx *ctx, struct sf_layout_result *out);

bool sf_layout_sm_box_near(const struct wlr_box *a, const struct wlr_box *b, int tol);

/* Which special mode ctx->current matches, if any. */
enum sf_layout_mode sf_layout_sm_match(const struct sf_layout_ctx *ctx, enum sf_snap *out_snap);

static inline bool sf_layout_sm_is_max_edges(const struct sf_layout_sm *s) {
	return s != NULL && s->mode == SF_LAYOUT_MODE_MAX_EDGES;
}

static inline bool sf_layout_sm_is_max_gaps(const struct sf_layout_sm *s) {
	return s != NULL && s->mode == SF_LAYOUT_MODE_MAX_GAPS;
}

static inline bool sf_layout_sm_is_fullscreen(const struct sf_layout_sm *s) {
	return s != NULL && s->mode == SF_LAYOUT_MODE_FULLSCREEN;
}

static inline bool sf_layout_sm_is_float(const struct sf_layout_sm *s) {
	return s == NULL || s->mode == SF_LAYOUT_MODE_FLOAT;
}

static inline enum sf_snap sf_layout_sm_snap(const struct sf_layout_sm *s) {
	if (s == NULL || s->mode != SF_LAYOUT_MODE_SNAP) {
		return SF_SNAP_NONE;
	}
	return s->snap;
}

/* Edge-max, gap-max, or half-snap (not fullscreen). Renamed from is_arranged
 * so it does not collide with workspace arrange engines. */
static inline bool sf_layout_sm_is_edged(const struct sf_layout_sm *s) {
	return sf_layout_sm_is_max_edges(s) || sf_layout_sm_is_max_gaps(s) ||
	       sf_layout_sm_snap(s) != SF_SNAP_NONE;
}

static inline bool sf_layout_sm_is_fixed(const struct sf_layout_sm *s) {
	return sf_layout_sm_is_edged(s) || sf_layout_sm_is_fullscreen(s);
}

#endif
