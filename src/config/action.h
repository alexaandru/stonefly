/* Action name ↔ enum (binds, taps, rules, IPC config dump). */
#ifndef SF_CONFIG_ACTION_H
#define SF_CONFIG_ACTION_H

#include <stdbool.h>
#include <stddef.h>

#include "config/config.h"

const char *sf_action_name(enum sf_action action);
enum sf_action sf_action_parse(const char *name);
/* Length of the name↔enum table (should equal SF_ACTION_COUNT). */
size_t sf_action_name_table_len(void);

/* true if name is a known action (including "none"). */
bool sf_action_parse_ok(const char *name, enum sf_action *out);

void sf_action_steps_free(struct sf_action_step *steps, size_t n);
/* Deep-copy; returns false on OOM (out untouched). */
bool sf_action_steps_dup(const struct sf_action_step *src, size_t n, struct sf_action_step **out,
			 size_t *out_n);
/* One-step heap list; false on OOM. */
bool sf_action_steps_one(enum sf_action action, const char *cmd, struct sf_action_step **out,
			 size_t *out_n);

#endif
