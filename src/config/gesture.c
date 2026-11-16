#include "config/gesture.h"

#include <stdlib.h>
#include <string.h>

struct dir_entry {
	const char *name;
	enum sf_gesture_dir dir;
};

static const struct dir_entry dirs[] = {
	{"swipe", SF_GESTURE_DIR_SWIPE},
	{"any", SF_GESTURE_DIR_SWIPE},
	{"horizontal", SF_GESTURE_DIR_HORIZONTAL},
	{"vertical", SF_GESTURE_DIR_VERTICAL},
	{"left", SF_GESTURE_DIR_LEFT},
	{"right", SF_GESTURE_DIR_RIGHT},
	{"up", SF_GESTURE_DIR_UP},
	{"down", SF_GESTURE_DIR_DOWN},
	{"pinch", SF_GESTURE_DIR_PINCH},
	{"pinchin", SF_GESTURE_DIR_PINCH_IN},
	{"pinchout", SF_GESTURE_DIR_PINCH_OUT},
};

bool sf_gesture_dir_parse(const char *name, enum sf_gesture_dir *out) {
	if (name == NULL || out == NULL) {
		return false;
	}
	for (size_t i = 0; i < sizeof dirs / sizeof dirs[0]; i++) {
		if (strcmp(name, dirs[i].name) == 0) {
			*out = dirs[i].dir;
			return true;
		}
	}
	return false;
}

const char *sf_gesture_dir_name(enum sf_gesture_dir dir) {
	switch (dir) {
	case SF_GESTURE_DIR_SWIPE:
		return "swipe";
	case SF_GESTURE_DIR_HORIZONTAL:
		return "horizontal";
	case SF_GESTURE_DIR_VERTICAL:
		return "vertical";
	case SF_GESTURE_DIR_LEFT:
		return "left";
	case SF_GESTURE_DIR_RIGHT:
		return "right";
	case SF_GESTURE_DIR_UP:
		return "up";
	case SF_GESTURE_DIR_DOWN:
		return "down";
	case SF_GESTURE_DIR_PINCH:
		return "pinch";
	case SF_GESTURE_DIR_PINCH_IN:
		return "pinchin";
	case SF_GESTURE_DIR_PINCH_OUT:
		return "pinchout";
	}
	return "swipe";
}

bool sf_gesture_kind_parse(const char *name, enum sf_gesture_kind *out) {
	if (name == NULL || out == NULL) {
		return false;
	}
	if (strcmp(name, "workspace") == 0) {
		*out = SF_GESTURE_KIND_WORKSPACE;
		return true;
	}
	if (strcmp(name, "columns_pan") == 0) {
		*out = SF_GESTURE_KIND_COLUMNS_PAN;
		return true;
	}
	if (strcmp(name, "overview") == 0) {
		*out = SF_GESTURE_KIND_OVERVIEW;
		return true;
	}
	return false;
}

const char *sf_gesture_kind_name(enum sf_gesture_kind kind) {
	switch (kind) {
	case SF_GESTURE_KIND_WORKSPACE:
		return "workspace";
	case SF_GESTURE_KIND_COLUMNS_PAN:
		return "columns_pan";
	case SF_GESTURE_KIND_OVERVIEW:
		return "overview";
	case SF_GESTURE_KIND_ACTION:
		return "action";
	}
	return "action";
}

/* How well g->dir fits a locked H/V axis and the travel sign. */
static int dir_score(enum sf_gesture_dir want, enum sf_gesture_dir locked, double primary) {
	bool neg = primary < 0.0;
	switch (want) {
	case SF_GESTURE_DIR_SWIPE:
		return 1;
	case SF_GESTURE_DIR_HORIZONTAL:
		return locked == SF_GESTURE_DIR_HORIZONTAL ? 2 : 0;
	case SF_GESTURE_DIR_VERTICAL:
		return locked == SF_GESTURE_DIR_VERTICAL ? 2 : 0;
	case SF_GESTURE_DIR_LEFT:
		return locked == SF_GESTURE_DIR_HORIZONTAL && neg ? 3 : 0;
	case SF_GESTURE_DIR_RIGHT:
		return locked == SF_GESTURE_DIR_HORIZONTAL && !neg ? 3 : 0;
	case SF_GESTURE_DIR_UP:
		return locked == SF_GESTURE_DIR_VERTICAL && neg ? 3 : 0;
	case SF_GESTURE_DIR_DOWN:
		return locked == SF_GESTURE_DIR_VERTICAL && !neg ? 3 : 0;
	case SF_GESTURE_DIR_PINCH:
	case SF_GESTURE_DIR_PINCH_IN:
	case SF_GESTURE_DIR_PINCH_OUT:
		return 0; /* swipe matcher ignores pinch binds */
	}
	return 0;
}

static int pinch_score(enum sf_gesture_dir want, double scale) {
	switch (want) {
	case SF_GESTURE_DIR_PINCH:
		return 1;
	case SF_GESTURE_DIR_PINCH_IN:
		return scale < 1.0 ? 3 : 0;
	case SF_GESTURE_DIR_PINCH_OUT:
		return scale > 1.0 ? 3 : 0;
	case SF_GESTURE_DIR_SWIPE:
	case SF_GESTURE_DIR_HORIZONTAL:
	case SF_GESTURE_DIR_VERTICAL:
	case SF_GESTURE_DIR_LEFT:
	case SF_GESTURE_DIR_RIGHT:
	case SF_GESTURE_DIR_UP:
	case SF_GESTURE_DIR_DOWN:
		return 0;
	}
	return 0;
}

const struct sf_gesture *sf_gesture_match(const struct sf_config *cfg, uint32_t fingers,
					  uint32_t mods, enum sf_gesture_dir locked,
					  double primary) {
	if (cfg == NULL || cfg->gestures == NULL || cfg->n_gestures == 0) {
		return NULL;
	}
	if (locked != SF_GESTURE_DIR_HORIZONTAL && locked != SF_GESTURE_DIR_VERTICAL) {
		return NULL;
	}
	mods &= (uint32_t)SF_MOD_MASK;
	const struct sf_gesture *best = NULL;
	int best_score = 0;
	for (size_t i = 0; i < cfg->n_gestures; i++) {
		const struct sf_gesture *g = &cfg->gestures[i];
		if (g->fingers != (int)fingers || g->mods != mods) {
			continue;
		}
		int score = dir_score(g->dir, locked, primary);
		if (score > best_score) {
			best_score = score;
			best = g;
		}
	}
	return best;
}

const struct sf_gesture *sf_gesture_match_pinch(const struct sf_config *cfg, uint32_t fingers,
						uint32_t mods, double scale) {
	if (cfg == NULL || cfg->gestures == NULL || cfg->n_gestures == 0) {
		return NULL;
	}
	mods &= (uint32_t)SF_MOD_MASK;
	const struct sf_gesture *best = NULL;
	int best_score = 0;
	for (size_t i = 0; i < cfg->n_gestures; i++) {
		const struct sf_gesture *g = &cfg->gestures[i];
		if (g->fingers != (int)fingers || g->mods != mods) {
			continue;
		}
		int score = pinch_score(g->dir, scale);
		if (score > best_score) {
			best_score = score;
			best = g;
		}
	}
	return best;
}

bool sf_gesture_seed_defaults(struct sf_config *cfg) {
	if (cfg == NULL) {
		return false;
	}
	struct sf_gesture *g = calloc(3, sizeof *g);
	if (g == NULL) {
		return false;
	}
	g[0].fingers = 3;
	g[0].dir = SF_GESTURE_DIR_VERTICAL;
	g[0].kind = SF_GESTURE_KIND_WORKSPACE;
	g[1].fingers = 3;
	g[1].dir = SF_GESTURE_DIR_HORIZONTAL;
	g[1].kind = SF_GESTURE_KIND_COLUMNS_PAN;
	g[2].fingers = 4;
	g[2].dir = SF_GESTURE_DIR_VERTICAL;
	g[2].kind = SF_GESTURE_KIND_OVERVIEW;
	cfg->gestures = g;
	cfg->n_gestures = 3;
	return true;
}
