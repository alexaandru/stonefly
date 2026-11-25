/* arrange-private: shared by arrange.c and arrange_drag.c. */
#ifndef SF_WM_ARRANGE_INTERNAL_H
#define SF_WM_ARRANGE_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>
#include <wlr/util/box.h>

struct sf_server;
struct sf_toplevel;

#define SF_ARRANGE_MAX 64

/* arrange.c — pack apply used by live cell-drag reflow. */
void arrange_apply_boxes(struct sf_toplevel **wins, const struct wlr_box *boxes, size_t n,
			 bool morph);

/* arrange_drag.c — clear drop targets; membership unlink invalidates mid-drag. */
void arrange_columns_drop_clear(struct sf_server *server);

#endif
