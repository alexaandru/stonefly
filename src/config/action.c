#include "config/action.h"

#include <stdlib.h>
#include <string.h>

#include "util/str.h"

struct sf_action_entry {
	const char *name;
	enum sf_action action;
};

static const struct sf_action_entry sf_actions[] = {
	{"none", SF_ACTION_NONE},
	{"quit", SF_ACTION_QUIT},
	{"close", SF_ACTION_CLOSE},
	{"focus_next", SF_ACTION_FOCUS_NEXT},
	{"focus_prev", SF_ACTION_FOCUS_PREV},
	{"focus_next_all", SF_ACTION_FOCUS_NEXT_ALL},
	{"focus_prev_all", SF_ACTION_FOCUS_PREV_ALL},
	{"maximize", SF_ACTION_MAXIMIZE},
	{"maximize_to_edges", SF_ACTION_MAXIMIZE_TO_EDGES},
	{"unmaximize", SF_ACTION_UNMAXIMIZE},
	{"toggle_maximize", SF_ACTION_TOGGLE_MAXIMIZE},
	{"toggle_maximize_to_edges", SF_ACTION_TOGGLE_MAXIMIZE_TO_EDGES},
	{"minimize", SF_ACTION_MINIMIZE},
	{"unminimize", SF_ACTION_UNMINIMIZE},
	{"snap_left", SF_ACTION_SNAP_LEFT},
	{"snap_right", SF_ACTION_SNAP_RIGHT},
	{"snap_top", SF_ACTION_SNAP_TOP},
	{"snap_bottom", SF_ACTION_SNAP_BOTTOM},
	{"unsnap", SF_ACTION_UNSNAP},
	{"exec", SF_ACTION_EXEC},
	{"scratch_toggle", SF_ACTION_SCRATCH_TOGGLE},
	{"scratch_claim", SF_ACTION_SCRATCH_CLAIM},
	{"workspace_next", SF_ACTION_WORKSPACE_NEXT},
	{"workspace_prev", SF_ACTION_WORKSPACE_PREV},
	{"workspace_switch", SF_ACTION_WORKSPACE_SWITCH},
	{"move_to_workspace_next", SF_ACTION_MOVE_TO_WORKSPACE_NEXT},
	{"move_to_workspace_prev", SF_ACTION_MOVE_TO_WORKSPACE_PREV},
	{"move_to_workspace", SF_ACTION_MOVE_TO_WORKSPACE},
	{"pointer_center", SF_ACTION_POINTER_CENTER},
	{"focus_monitor", SF_ACTION_FOCUS_MONITOR},
	{"send_to_monitor", SF_ACTION_SEND_TO_MONITOR},
};

const char *sf_action_name(enum sf_action action) {
	for (size_t i = 0; i < sizeof sf_actions / sizeof sf_actions[0]; i++) {
		if (sf_actions[i].action == action) {
			return sf_actions[i].name;
		}
	}
	return "unknown";
}

enum sf_action sf_action_parse(const char *name) {
	enum sf_action action = SF_ACTION_NONE;
	(void)sf_action_parse_ok(name, &action);
	return action;
}

bool sf_action_parse_ok(const char *name, enum sf_action *out) {
	if (name == NULL || out == NULL) {
		return false;
	}
	for (size_t i = 0; i < sizeof sf_actions / sizeof sf_actions[0]; i++) {
		if (strcmp(name, sf_actions[i].name) == 0) {
			*out = sf_actions[i].action;
			return true;
		}
	}
	return false;
}

void sf_action_steps_free(struct sf_action_step *steps, size_t n) {
	if (steps == NULL) {
		return;
	}
	for (size_t i = 0; i < n; i++) {
		free(steps[i].cmd);
	}
	free(steps);
}

bool sf_action_steps_dup(const struct sf_action_step *src, size_t n, struct sf_action_step **out,
			 size_t *out_n) {
	if (out == NULL || out_n == NULL) {
		return false;
	}
	if (n == 0 || src == NULL) {
		*out = NULL;
		*out_n = 0;
		return true;
	}
	struct sf_action_step *steps = calloc(n, sizeof *steps);
	if (steps == NULL) {
		return false;
	}
	for (size_t i = 0; i < n; i++) {
		steps[i].action = src[i].action;
		if (src[i].cmd != NULL) {
			steps[i].cmd = sf_xstrdup(src[i].cmd);
			if (steps[i].cmd == NULL && src[i].cmd[0] != '\0') {
				sf_action_steps_free(steps, i);
				return false;
			}
		}
	}
	*out = steps;
	*out_n = n;
	return true;
}

bool sf_action_steps_one(enum sf_action action, const char *cmd, struct sf_action_step **out,
			 size_t *out_n) {
	struct sf_action_step one = {.action = action, .cmd = (char *)cmd};
	return sf_action_steps_dup(&one, 1, out, out_n);
}
