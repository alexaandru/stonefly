/* Pointer-wheel binds (sf.bind "Super+WheelUp" …). */
#ifndef SF_CONFIG_WHEELBIND_H
#define SF_CONFIG_WHEELBIND_H

#include <stdbool.h>
#include <stdint.h>

#include "config/config.h"

const struct sf_wheelbind *sf_wheelbind_match(const struct sf_config *cfg, uint32_t mods,
					      enum sf_wheel_dir dir, enum sf_arrange cur);

/* Super+Wheel → workspace; Super+Shift+Wheel → focus_dir (tiling). */
bool sf_wheelbind_seed_defaults(struct sf_config *cfg);

const char *sf_wheelbind_dir_name(enum sf_wheel_dir dir);

#endif
