#include "ipc/parse.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "util/json.h"

bool sf_ipc_parse_cmd_arg(const char *line, char *cmd, size_t cmd_cap, char *arg, size_t arg_cap) {
	if (arg != NULL && arg_cap > 0) {
		arg[0] = '\0';
	}

	struct json_scan s;
	json_scan_init(&s, line);
	if (!json_scan_expect(&s, '{')) {
		return false;
	}

	json_scan_skip_ws(&s);
	if (s.p < s.end && *s.p == '}') {
		return false;
	}

	bool found = false;
	for (;;) {
		char key[64];
		if (!json_scan_string(&s, key, sizeof(key)) || !json_scan_expect(&s, ':')) {
			return false;
		}

		char value[128];
		if (!json_scan_string(&s, value, sizeof(value))) {
			return false;
		}
		if (strcmp(key, "cmd") == 0) {
			snprintf(cmd, cmd_cap, "%s", value);
			found = true;
		} else if (strcmp(key, "arg") == 0 && arg != NULL && arg_cap > 0) {
			snprintf(arg, arg_cap, "%s", value);
		}

		json_scan_skip_ws(&s);
		if (s.p < s.end && *s.p == ',') {
			s.p++;
			continue;
		}
		if (!json_scan_expect(&s, '}')) {
			return false;
		}
		return found && cmd[0] != '\0';
	}
}

bool sf_ipc_parse_id_arg(const char *arg, uint64_t *out) {
	if (arg == NULL || arg[0] == '\0' || out == NULL) {
		return false;
	}
	char *end = NULL;
	unsigned long long v = strtoull(arg, &end, 10);
	if (end == arg || (end != NULL && *end != '\0')) {
		return false;
	}
	*out = (uint64_t)v;
	return true;
}

bool sf_ipc_parse_workspace_index(const char *arg, int *out) {
	if (arg == NULL || arg[0] == '\0' || out == NULL) {
		return false;
	}
	char *end = NULL;
	errno = 0;
	long v = strtol(arg, &end, 10);
	if (errno != 0 || end == arg || *end != '\0' || v <= 0 || v > (long)INT_MAX) {
		return false;
	}
	*out = (int)v;
	return true;
}
