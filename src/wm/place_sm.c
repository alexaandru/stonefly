#include "wm/place_sm.h"

void sf_place_sm_init(struct sf_place_sm *s) {
	if (s == NULL) {
		return;
	}
	*s = (struct sf_place_sm){0};
}

bool sf_place_sm_should_hold(const struct sf_place_sm *s, const struct sf_place_ctx *ctx) {
	if (s == NULL || ctx == NULL || !s->soft) {
		return false;
	}
	/* Parented dialogs must show immediately. */
	if (ctx->has_parent) {
		return false;
	}
	if (ctx->wants_center || ctx->wants_float) {
		return false;
	}
	if (!ctx->center_still_possible && !ctx->float_still_possible) {
		return false;
	}
	/* Park only for a late title (Chrome app_id first, then "^Google Chrome$").
	 * Empty app_id must not hold: Chrome Task Manager never sets app_id, and
	 * any app_id float rule would leave the scene disabled forever (taskbar +
	 * overview yes, desktop no). Late app_id can still morph via soft place. */
	if (ctx->title_known) {
		return false;
	}
	return ctx->partial_identity;
}

static void fx_push(struct sf_place_result *out, enum sf_place_fx_kind kind) {
	if (out->nfx >= SF_PLACE_FX_MAX) {
		return;
	}
	out->fx[out->nfx++] = (struct sf_place_effect){.kind = kind};
}

static void release_hold(struct sf_place_result *out, struct sf_place_sm *s) {
	if (!s->appear_hold) {
		return;
	}
	s->appear_hold = false;
	fx_push(out, SF_PLACE_FX_SCENE_ENABLE);
	fx_push(out, SF_PLACE_FX_START_SHOW);
}

static void settle(struct sf_place_result *out, struct sf_place_sm *s) {
	s->phase = SF_PLACE_SETTLED;
	s->soft = false;
	release_hold(out, s);
}

void sf_place_sm_step(const struct sf_place_sm *in, const struct sf_place_ev *ev,
		      const struct sf_place_ctx *ctx, struct sf_place_result *out) {
	if (out == NULL) {
		return;
	}
	*out = (struct sf_place_result){0};
	if (in != NULL) {
		out->state = *in;
	}
	if (ev == NULL) {
		return;
	}
	struct sf_place_sm *s = &out->state;
	struct sf_place_ctx empty = {0};
	const struct sf_place_ctx *c = ctx != NULL ? ctx : &empty;

	switch (ev->kind) {
	case SF_PLACE_EV_BEGIN:
		s->phase = SF_PLACE_MANAGED;
		s->soft = true;
		/* appear_hold decided by ARM_APPEAR after place. */
		break;
	case SF_PLACE_EV_USER_OWN:
	case SF_PLACE_EV_LAYOUT_OWNED:
		settle(out, s);
		break;
	case SF_PLACE_EV_DROP_SOFT:
		s->soft = false;
		if (s->appear_hold && !sf_place_sm_should_hold(s, c)) {
			release_hold(out, s);
		}
		break;
	case SF_PLACE_EV_SOFT_CENTER:
		s->phase = SF_PLACE_MANAGED;
		s->soft = true;
		if (s->appear_hold && !sf_place_sm_should_hold(s, c)) {
			release_hold(out, s);
		}
		break;
	case SF_PLACE_EV_ARM_APPEAR:
		if (sf_place_sm_should_hold(s, c)) {
			s->appear_hold = true;
			fx_push(out, SF_PLACE_FX_SCENE_DISABLE);
		} else {
			s->appear_hold = false;
			fx_push(out, SF_PLACE_FX_SCENE_ENABLE);
			fx_push(out, SF_PLACE_FX_START_SHOW);
		}
		break;
	case SF_PLACE_EV_TRY_APPEAR:
		if (!s->appear_hold) {
			break;
		}
		if (sf_place_sm_should_hold(s, c)) {
			break;
		}
		release_hold(out, s);
		break;
	case SF_PLACE_EV_RESET:
		s->phase = SF_PLACE_SETTLED;
		s->soft = false;
		s->appear_hold = false;
		break;
	}
}
