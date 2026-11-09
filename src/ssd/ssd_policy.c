#include <string.h>
#include <wlr/types/wlr_xdg_decoration_v1.h>

#include "ssd/ssd_policy.h"

enum sf_ssd_mode sf_ssd_mode_parse(const char *s) {
	if (s == NULL) {
		return SF_SSD_AUTO;
	}
	if (strcmp(s, "never") == 0) {
		return SF_SSD_NEVER;
	}
	if (strcmp(s, "prefer_ssd") == 0) {
		return SF_SSD_PREFER;
	}
	if (strcmp(s, "always") == 0) {
		return SF_SSD_ALWAYS;
	}
	if (strcmp(s, "auto") == 0) {
		return SF_SSD_AUTO;
	}
	return SF_SSD_AUTO;
}

const char *sf_ssd_mode_name(enum sf_ssd_mode mode) {
	switch (mode) {
	case SF_SSD_NEVER:
		return "never";
	case SF_SSD_PREFER:
		return "prefer_ssd";
	case SF_SSD_ALWAYS:
		return "always";
	case SF_SSD_AUTO:
	default:
		return "auto";
	}
}

bool sf_ssd_want(enum sf_ssd_mode mode, int requested_mode) {
	switch (mode) {
	case SF_SSD_NEVER:
		return false;
	case SF_SSD_ALWAYS:
		return true;
	case SF_SSD_PREFER:
		return requested_mode != WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE;
	case SF_SSD_AUTO:
	default:
		if (requested_mode == WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE) {
			return false;
		}
		/* SERVER_SIDE or NONE (no preference). */
		return true;
	}
}
