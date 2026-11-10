/* WM action verb catalog — table-driven sf_run_action implementation.
 * Public entry points remain declared in wm.h. */
#ifndef SF_WM_ACTION_RUN_H
#define SF_WM_ACTION_RUN_H

#include <stdbool.h>

#include "config/config.h"

/* Shared precondition bits (visible next to each verb in the catalog). */
enum sf_action_flag {
	SF_ACTION_F_WIN = 1u << 0, /* need a resolved toplevel */
	SF_ACTION_F_CMD = 1u << 1, /* need non-empty cmd; warn otherwise */
};

/* Introspection for unit tests / tooling. */
unsigned sf_action_run_flags(enum sf_action action);
bool sf_action_run_has_handler(enum sf_action action);

#endif
