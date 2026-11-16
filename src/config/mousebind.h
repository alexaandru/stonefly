/* Pointer-button binds (sf.bind "Super+LMB" …). */
#ifndef SF_CONFIG_MOUSEBIND_H
#define SF_CONFIG_MOUSEBIND_H

#include <stdbool.h>
#include <stdint.h>

#include "config/config.h"

/*
 * Exact mods + button match. Prefer arrange-specific over UNSET (same as
 * keybinds). Returns NULL when nothing matches.
 */
const struct sf_mousebind *sf_mousebind_match(const struct sf_config *cfg, uint32_t mods,
					      uint32_t button, enum sf_arrange cur);

/* Seed Super+LMB move / Super+RMB resize / Super+Shift+RMB center-lock. */
bool sf_mousebind_seed_defaults(struct sf_config *cfg);

/* Canonical button token for IPC / docs (LMB/RMB/…). */
const char *sf_mousebind_button_name(uint32_t button);

#endif
