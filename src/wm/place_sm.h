/* Pure map/SoftPlace/appear machine — no compositor types.
 * Replaces auto_place / soft_place / appear_pending on sf_toplevel. */
#ifndef SF_WM_PLACE_SM_H
#define SF_WM_PLACE_SM_H

#include <stdbool.h>
#include <stddef.h>

enum sf_place_phase {
	SF_PLACE_SETTLED = 0, /* user / layout owns geometry */
	SF_PLACE_MANAGED,     /* WM may still re-place (parent/center/cascade) */
};

struct sf_place_sm {
	enum sf_place_phase phase;
	bool soft;	  /* SoftPlace: late title/app_id may still center */
	bool appear_hold; /* scene parked until SoftPlace settles */
};

enum sf_place_ev_kind {
	/* Map path: start managed SoftPlace. */
	SF_PLACE_EV_BEGIN,
	/* User move/resize/monitor send — settle and release hold. */
	SF_PLACE_EV_USER_OWN,
	/* Rule max/snap/fs (or equivalent) owns geometry — settle. */
	SF_PLACE_EV_LAYOUT_OWNED,
	/* Center rule no longer possible — drop soft; maybe release hold. */
	SF_PLACE_EV_DROP_SOFT,
	/* Center rule matched — keep/reassert managed + soft. */
	SF_PLACE_EV_SOFT_CENTER,
	/* Decide hold vs show from ctx (map arm_appear). */
	SF_PLACE_EV_ARM_APPEAR,
	/* Release hold when ctx says stop holding. */
	SF_PLACE_EV_TRY_APPEAR,
	/* Clear hold with no show effects (unmap). */
	SF_PLACE_EV_RESET,
};

struct sf_place_ev {
	enum sf_place_ev_kind kind;
};

/* Facts the WM fills in; SM stays free of rules/xdg. */
struct sf_place_ctx {
	bool has_parent;
	bool wants_center;
	bool center_still_possible;
	bool partial_identity; /* title or app_id known */
	bool layout_fixed;
};

enum sf_place_fx_kind {
	SF_PLACE_FX_SCENE_DISABLE = 1,
	SF_PLACE_FX_SCENE_ENABLE,
	SF_PLACE_FX_START_SHOW, /* slide/appear anim (or snap to resting) */
};

struct sf_place_effect {
	enum sf_place_fx_kind kind;
};

#define SF_PLACE_FX_MAX 4

struct sf_place_result {
	struct sf_place_sm state;
	struct sf_place_effect fx[SF_PLACE_FX_MAX];
	size_t nfx;
};

void sf_place_sm_init(struct sf_place_sm *s);

void sf_place_sm_step(const struct sf_place_sm *in, const struct sf_place_ev *ev,
		      const struct sf_place_ctx *ctx, struct sf_place_result *out);

/* Pure hold predicate (also used by tests). */
bool sf_place_sm_should_hold(const struct sf_place_sm *s, const struct sf_place_ctx *ctx);

static inline bool sf_place_sm_is_managed(const struct sf_place_sm *s) {
	return s != NULL && s->phase == SF_PLACE_MANAGED;
}

static inline bool sf_place_sm_is_soft(const struct sf_place_sm *s) {
	return s != NULL && s->soft;
}

static inline bool sf_place_sm_is_holding(const struct sf_place_sm *s) {
	return s != NULL && s->appear_hold;
}

/* Late SoftPlace morph while managed or still soft. */
static inline bool sf_place_sm_may_morph(const struct sf_place_sm *s) {
	return sf_place_sm_is_managed(s) || sf_place_sm_is_soft(s);
}

#endif
