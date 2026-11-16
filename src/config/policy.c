#include "config/config.h"

#include <string.h>

enum sf_client_maximize sf_client_maximize_parse(const char *s) {
	if (s == NULL || s[0] == '\0') {
		return SF_CLIENT_MAXIMIZE_GAPS;
	}
	if (strcmp(s, "edges") == 0 || strcmp(s, "edge") == 0) {
		return SF_CLIENT_MAXIMIZE_EDGES;
	}
	if (strcmp(s, "gaps") == 0 || strcmp(s, "gap") == 0 || strcmp(s, "column") == 0) {
		return SF_CLIENT_MAXIMIZE_GAPS;
	}
	return SF_CLIENT_MAXIMIZE_GAPS;
}

const char *sf_client_maximize_name(enum sf_client_maximize mode) {
	switch (mode) {
	case SF_CLIENT_MAXIMIZE_EDGES:
		return "edges";
	case SF_CLIENT_MAXIMIZE_GAPS:
	default:
		return "gaps";
	}
}

enum sf_unminimize_workspace sf_unminimize_workspace_parse(const char *s) {
	if (s == NULL || s[0] == '\0') {
		return SF_UNMINIMIZE_WS_ACTIVE;
	}
	if (strcmp(s, "home") == 0) {
		return SF_UNMINIMIZE_WS_HOME;
	}
	if (strcmp(s, "active") == 0) {
		return SF_UNMINIMIZE_WS_ACTIVE;
	}
	return SF_UNMINIMIZE_WS_ACTIVE;
}

const char *sf_unminimize_workspace_name(enum sf_unminimize_workspace mode) {
	switch (mode) {
	case SF_UNMINIMIZE_WS_HOME:
		return "home";
	case SF_UNMINIMIZE_WS_ACTIVE:
	default:
		return "active";
	}
}
