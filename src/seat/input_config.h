#ifndef SF_INPUT_CONFIG_H
#define SF_INPUT_CONFIG_H

#include "server/server.h"

struct wlr_input_device;

/* Apply matching sf.input rules to a newly added device (libinput + kb repeat). */
void sf_input_apply_device(struct sf_server *server, struct wlr_input_device *device);

#endif
