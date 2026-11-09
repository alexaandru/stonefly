#ifndef SF_SSD_POLICY_H
#define SF_SSD_POLICY_H

#include <stdbool.h>

enum sf_ssd_mode {
	SF_SSD_NEVER = 0,
	SF_SSD_AUTO,
	SF_SSD_PREFER,
	SF_SSD_ALWAYS,
};

enum sf_ssd_mode sf_ssd_mode_parse(const char *s);
const char *sf_ssd_mode_name(enum sf_ssd_mode mode);

/* requested_mode is WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_*. */
bool sf_ssd_want(enum sf_ssd_mode mode, int requested_mode);

#endif /* SF_SSD_POLICY_H */
