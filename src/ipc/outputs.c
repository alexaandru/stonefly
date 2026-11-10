#include "ipc/outputs.h"

#include <stdio.h>
#include <string.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>

#include "output/output.h"
#include "server/server.h"

size_t sf_outputs_collect(struct sf_server *server, struct sf_output_info *out, size_t cap) {
	if (server == NULL || out == NULL || cap == 0) {
		return 0;
	}

	struct wlr_output *focused_wlr = NULL;
	if (server->cursor != NULL && server->output_layout != NULL) {
		focused_wlr = wlr_output_layout_output_at(server->output_layout, server->cursor->x,
							  server->cursor->y);
	}

	size_t n = 0;
	struct sf_output *output;
	wl_list_for_each(output, &server->outputs, link) {
		if (n >= cap) {
			break;
		}
		struct wlr_output *wlr = output->wlr_output;
		if (wlr == NULL) {
			continue;
		}

		struct sf_output_info *info = &out[n++];
		memset(info, 0, sizeof(*info));
		snprintf(info->name, sizeof(info->name), "%s", wlr->name != NULL ? wlr->name : "");
		info->enabled = wlr->enabled;
		info->x = output->full.x;
		info->y = output->full.y;
		info->logical_w = output->full.width;
		info->logical_h = output->full.height;
		info->w = wlr->width;
		info->h = wlr->height;
		info->scale = wlr->scale;
		info->usable_x = output->usable.x;
		info->usable_y = output->usable.y;
		info->usable_w = output->usable.width;
		info->usable_h = output->usable.height;
		info->focused = focused_wlr == wlr;
	}
	return n;
}

void sf_outputs_print(const struct sf_output_info *infos, size_t n, FILE *fp) {
	for (size_t i = 0; i < n; i++) {
		const struct sf_output_info *o = &infos[i];
		fprintf(fp, "%s%s  %dx%d@%.2f  layout %d,%d %dx%d  usable %d,%d %dx%d\n", o->name,
			o->focused ? "*" : " ", o->w, o->h, o->scale, o->x, o->y, o->logical_w,
			o->logical_h, o->usable_x, o->usable_y, o->usable_w, o->usable_h);
	}
}
