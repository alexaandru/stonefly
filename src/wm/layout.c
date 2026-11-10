#include "wm/workspace.h"

#include <string.h>

enum sf_layout sf_layout_parse(const char *s) {
	if (s == NULL || s[0] == '\0' || strcmp(s, "stacking") == 0) {
		return SF_LAYOUT_STACKING;
	}
	if (strcmp(s, "columns") == 0) {
		return SF_LAYOUT_COLUMNS;
	}
	if (strcmp(s, "tile") == 0) {
		return SF_LAYOUT_TILE;
	}
	if (strcmp(s, "unset") == 0 || strcmp(s, "inherit") == 0) {
		return SF_LAYOUT_UNSET;
	}
	return SF_LAYOUT_STACKING;
}

const char *sf_layout_name(enum sf_layout layout) {
	switch (layout) {
	case SF_LAYOUT_UNSET:
		return "inherit";
	case SF_LAYOUT_STACKING:
		return "stacking";
	case SF_LAYOUT_COLUMNS:
		return "columns";
	case SF_LAYOUT_TILE:
		return "tile";
	}
	return "stacking";
}
