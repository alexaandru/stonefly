/* Touchpad swipe gesture binds (sf.gesture). */
#ifndef SF_CONFIG_GESTURE_H
#define SF_CONFIG_GESTURE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "config/config.h"

/* Parse direction/axis token. Returns false on unknown. */
bool sf_gesture_dir_parse(const char *name, enum sf_gesture_dir *out);
const char *sf_gesture_dir_name(enum sf_gesture_dir dir);

bool sf_gesture_kind_parse(const char *name, enum sf_gesture_kind *out);
const char *sf_gesture_kind_name(enum sf_gesture_kind kind);

/*
 * Pick the best swipe gesture for fingers + locked axis (H or V) + primary
 * travel (dx when H, dy when V). Prefer exact left/right/up/down over
 * horizontal/vertical over swipe. Ties keep the first listed bind.
 */
const struct sf_gesture *sf_gesture_match(const struct sf_config *cfg, uint32_t fingers,
					  uint32_t mods, enum sf_gesture_dir locked,
					  double primary);

/*
 * Pick the best pinch gesture. scale is libinput's cumulative scale (1.0 at
 * begin). Prefer pinchin/pinchout over pinch. mods is seat SF_MOD_MASK.
 */
const struct sf_gesture *sf_gesture_match_pinch(const struct sf_config *cfg, uint32_t fingers,
						uint32_t mods, double scale);

/* Seed the day-zero 3-finger V=workspace / H=columns_pan pair. */
bool sf_gesture_seed_defaults(struct sf_config *cfg);

#endif
