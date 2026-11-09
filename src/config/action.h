/* Action name ↔ enum (binds, taps, IPC config dump). */
#ifndef STONEFLY_CONFIG_ACTION_H
#define STONEFLY_CONFIG_ACTION_H

#include "config/config.h"

const char *stonefly_action_name(enum stonefly_action action);
enum stonefly_action stonefly_action_parse(const char *name);

#endif
