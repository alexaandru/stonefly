#include "config/action.h"

#include <stddef.h>
#include <string.h>

struct stonefly_action_entry {
	const char *name;
	enum stonefly_action action;
};

static const struct stonefly_action_entry stonefly_actions[] = {
	{"none", STONEFLY_ACTION_NONE},
	{"quit", STONEFLY_ACTION_QUIT},
	{"close", STONEFLY_ACTION_CLOSE},
	{"focus_next", STONEFLY_ACTION_FOCUS_NEXT},
	{"focus_prev", STONEFLY_ACTION_FOCUS_PREV},
	{"focus_next_all", STONEFLY_ACTION_FOCUS_NEXT_ALL},
	{"focus_prev_all", STONEFLY_ACTION_FOCUS_PREV_ALL},
	{"toggle_max_edges", STONEFLY_ACTION_TOGGLE_MAX_EDGES},
	{"toggle_max_gaps", STONEFLY_ACTION_TOGGLE_MAX_GAPS},
	{"minimize", STONEFLY_ACTION_MINIMIZE},
	{"unminimize", STONEFLY_ACTION_UNMINIMIZE},
	{"snap_left", STONEFLY_ACTION_SNAP_LEFT},
	{"snap_right", STONEFLY_ACTION_SNAP_RIGHT},
	{"snap_top", STONEFLY_ACTION_SNAP_TOP},
	{"snap_bottom", STONEFLY_ACTION_SNAP_BOTTOM},
	{"unsnap", STONEFLY_ACTION_UNSNAP},
	{"exec", STONEFLY_ACTION_EXEC},
	{"scratch_toggle", STONEFLY_ACTION_SCRATCH_TOGGLE},
	{"scratch_claim", STONEFLY_ACTION_SCRATCH_CLAIM},
	{"workspace_next", STONEFLY_ACTION_WORKSPACE_NEXT},
	{"workspace_prev", STONEFLY_ACTION_WORKSPACE_PREV},
	{"workspace_switch", STONEFLY_ACTION_WORKSPACE_SWITCH},
	{"move_to_workspace_next", STONEFLY_ACTION_MOVE_TO_WORKSPACE_NEXT},
	{"move_to_workspace_prev", STONEFLY_ACTION_MOVE_TO_WORKSPACE_PREV},
	{"move_to_workspace", STONEFLY_ACTION_MOVE_TO_WORKSPACE},
};

const char *stonefly_action_name(enum stonefly_action action) {
	for (size_t i = 0; i < sizeof stonefly_actions / sizeof stonefly_actions[0]; i++) {
		if (stonefly_actions[i].action == action) {
			return stonefly_actions[i].name;
		}
	}
	return "unknown";
}

enum stonefly_action stonefly_action_parse(const char *name) {
	if (name == NULL) {
		return STONEFLY_ACTION_NONE;
	}
	for (size_t i = 0; i < sizeof stonefly_actions / sizeof stonefly_actions[0]; i++) {
		if (strcmp(name, stonefly_actions[i].name) == 0) {
			return stonefly_actions[i].action;
		}
	}
	return STONEFLY_ACTION_NONE;
}
