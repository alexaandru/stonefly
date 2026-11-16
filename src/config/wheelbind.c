#include "config/wheelbind.h"

#include <stdlib.h>
#include <string.h>

#include "config/action.h"
#include "wm/arrange_math.h"

const char *sf_wheelbind_dir_name(enum sf_wheel_dir dir) {
	switch (dir) {
	case SF_WHEEL_UP:
		return "WheelUp";
	case SF_WHEEL_DOWN:
		return "WheelDown";
	case SF_WHEEL_LEFT:
		return "WheelLeft";
	case SF_WHEEL_RIGHT:
		return "WheelRight";
	}
	return "WheelUp";
}

static bool arrange_ok(enum sf_arrange filter, enum sf_arrange current) {
	if (filter == SF_ARRANGE_UNSET) {
		return true;
	}
	if (filter == SF_ARRANGE_TILING) {
		return sf_arrange_is_tiling(current);
	}
	return filter == current;
}

const struct sf_wheelbind *sf_wheelbind_match(const struct sf_config *cfg, uint32_t mods,
					      enum sf_wheel_dir dir, enum sf_arrange cur) {
	if (cfg == NULL || cfg->wheelbinds == NULL || cfg->n_wheelbinds == 0) {
		return NULL;
	}
	mods &= (uint32_t)SF_MOD_MASK;
	if (mods == 0) {
		return NULL;
	}
	const struct sf_wheelbind *fallback = NULL;
	for (size_t i = 0; i < cfg->n_wheelbinds; i++) {
		const struct sf_wheelbind *b = &cfg->wheelbinds[i];
		if (b->mods != mods || b->dir != dir) {
			continue;
		}
		if (!arrange_ok(b->arrange, cur)) {
			continue;
		}
		if (b->arrange != SF_ARRANGE_UNSET) {
			return b;
		}
		if (fallback == NULL) {
			fallback = b;
		}
	}
	return fallback;
}

bool sf_wheelbind_seed_defaults(struct sf_config *cfg) {
	if (cfg == NULL) {
		return false;
	}
	struct sf_wheelbind *w = calloc(4, sizeof *w);
	if (w == NULL) {
		return false;
	}
	if (!sf_action_steps_one(SF_ACTION_WORKSPACE_PREV, NULL, &w[0].steps, &w[0].n_steps) ||
	    !sf_action_steps_one(SF_ACTION_WORKSPACE_NEXT, NULL, &w[1].steps, &w[1].n_steps) ||
	    !sf_action_steps_one(SF_ACTION_FOCUS_DIR, "left", &w[2].steps, &w[2].n_steps) ||
	    !sf_action_steps_one(SF_ACTION_FOCUS_DIR, "right", &w[3].steps, &w[3].n_steps)) {
		for (size_t i = 0; i < 4; i++) {
			sf_action_steps_free(w[i].steps, w[i].n_steps);
		}
		free(w);
		return false;
	}
	w[0].mods = SF_SUPER;
	w[0].dir = SF_WHEEL_UP;
	w[0].arrange = SF_ARRANGE_UNSET;
	w[1].mods = SF_SUPER;
	w[1].dir = SF_WHEEL_DOWN;
	w[1].arrange = SF_ARRANGE_UNSET;
	w[2].mods = SF_SUPER | SF_SHIFT;
	w[2].dir = SF_WHEEL_UP;
	w[2].arrange = SF_ARRANGE_TILING;
	w[3].mods = SF_SUPER | SF_SHIFT;
	w[3].dir = SF_WHEEL_DOWN;
	w[3].arrange = SF_ARRANGE_TILING;
	cfg->wheelbinds = w;
	cfg->n_wheelbinds = 4;
	return true;
}
