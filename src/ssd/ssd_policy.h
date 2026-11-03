#ifndef STONEFLY_SSD_POLICY_H
#define STONEFLY_SSD_POLICY_H

#include <stdbool.h>

enum stonefly_ssd_mode {
	STONEFLY_SSD_NEVER = 0,
	STONEFLY_SSD_AUTO,
	STONEFLY_SSD_PREFER,
	STONEFLY_SSD_ALWAYS,
};

enum stonefly_ssd_mode stonefly_ssd_mode_parse(const char *s);
const char *stonefly_ssd_mode_name(enum stonefly_ssd_mode mode);

/* requested_mode is WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_*. */
bool stonefly_ssd_want(enum stonefly_ssd_mode mode, int requested_mode);

#endif /* STONEFLY_SSD_POLICY_H */
