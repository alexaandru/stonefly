#include "config/mousebind.h"

#include <linux/input-event-codes.h>
#include <stdlib.h>
#include <string.h>

#include "config/action.h"
#include "wm/arrange_math.h"

static bool arrange_ok(enum sf_arrange filter, enum sf_arrange current) {
	if (filter == SF_ARRANGE_UNSET) {
		return true;
	}
	if (filter == SF_ARRANGE_TILING) {
		return sf_arrange_is_tiling(current);
	}
	return filter == current;
}

const char *sf_mousebind_button_name(uint32_t button) {
	switch (button) {
	case BTN_LEFT:
		return "LMB";
	case BTN_RIGHT:
		return "RMB";
	case BTN_MIDDLE:
		return "MMB";
	case BTN_SIDE:
		return "BTN_SIDE";
	case BTN_EXTRA:
		return "BTN_EXTRA";
	default:
		return "BTN_UNKNOWN";
	}
}

const struct sf_mousebind *sf_mousebind_match(const struct sf_config *cfg, uint32_t mods,
					      uint32_t button, enum sf_arrange cur) {
	if (cfg == NULL || cfg->mousebinds == NULL || cfg->n_mousebinds == 0) {
		return NULL;
	}
	mods &= (uint32_t)SF_MOD_MASK;
	const struct sf_mousebind *fallback = NULL;
	for (size_t i = 0; i < cfg->n_mousebinds; i++) {
		const struct sf_mousebind *b = &cfg->mousebinds[i];
		if (b->mods != mods || b->button != button) {
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

bool sf_mousebind_seed_defaults(struct sf_config *cfg) {
	if (cfg == NULL) {
		return false;
	}
	struct sf_mousebind *m = calloc(3, sizeof *m);
	if (m == NULL) {
		return false;
	}
	if (!sf_action_steps_one(SF_ACTION_MOVE_WINDOW, NULL, &m[0].steps, &m[0].n_steps) ||
	    !sf_action_steps_one(SF_ACTION_RESIZE_WINDOW, NULL, &m[1].steps, &m[1].n_steps) ||
	    !sf_action_steps_one(SF_ACTION_RESIZE_CENTER, NULL, &m[2].steps, &m[2].n_steps)) {
		for (size_t i = 0; i < 3; i++) {
			sf_action_steps_free(m[i].steps, m[i].n_steps);
		}
		free(m);
		return false;
	}
	m[0].mods = SF_SUPER;
	m[0].button = BTN_LEFT;
	m[0].arrange = SF_ARRANGE_UNSET;
	m[1].mods = SF_SUPER;
	m[1].button = BTN_RIGHT;
	m[1].arrange = SF_ARRANGE_UNSET;
	m[2].mods = SF_SUPER | SF_SHIFT;
	m[2].button = BTN_RIGHT;
	m[2].arrange = SF_ARRANGE_UNSET;
	cfg->mousebinds = m;
	cfg->n_mousebinds = 3;
	return true;
}
