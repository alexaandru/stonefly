#define _POSIX_C_SOURCE 200809L

#include <string.h>
#include <wlr/types/wlr_xdg_decoration_v1.h>

#include "ssd/ssd_policy.h"

enum stonefly_ssd_mode stonefly_ssd_mode_parse(const char *s) {
	if (s == NULL) {
		return STONEFLY_SSD_AUTO;
	}
	if (strcmp(s, "never") == 0) {
		return STONEFLY_SSD_NEVER;
	}
	if (strcmp(s, "prefer_ssd") == 0) {
		return STONEFLY_SSD_PREFER;
	}
	if (strcmp(s, "always") == 0) {
		return STONEFLY_SSD_ALWAYS;
	}
	if (strcmp(s, "auto") == 0) {
		return STONEFLY_SSD_AUTO;
	}
	return STONEFLY_SSD_AUTO;
}

const char *stonefly_ssd_mode_name(enum stonefly_ssd_mode mode) {
	switch (mode) {
	case STONEFLY_SSD_NEVER:
		return "never";
	case STONEFLY_SSD_PREFER:
		return "prefer_ssd";
	case STONEFLY_SSD_ALWAYS:
		return "always";
	case STONEFLY_SSD_AUTO:
	default:
		return "auto";
	}
}

bool stonefly_ssd_want(enum stonefly_ssd_mode mode, int requested_mode) {
	switch (mode) {
	case STONEFLY_SSD_NEVER:
		return false;
	case STONEFLY_SSD_ALWAYS:
		return true;
	case STONEFLY_SSD_PREFER:
		return requested_mode != WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE;
	case STONEFLY_SSD_AUTO:
	default:
		if (requested_mode == WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE) {
			return false;
		}
		/* SERVER_SIDE or NONE (no preference). */
		return true;
	}
}
