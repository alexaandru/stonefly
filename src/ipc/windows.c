#include "ipc/windows.h"

#include <stdio.h>
#include <string.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/box.h>

#include "output/output.h"
#include "server/server.h"
#include "toplevel/toplevel.h"
#include "wm/column.h"
#include "wm/layout_sm.h"
#include "wm/place_sm.h"
#include "wm/wm.h"
#include "wm/workspace.h"

static void copy_str(char *dst, size_t cap, const char *src) {
	if (src == NULL) {
		src = "";
	}
	snprintf(dst, cap, "%s", src);
}

static const char *layout_mode_name(enum sf_layout_mode mode) {
	switch (mode) {
	case SF_LAYOUT_MODE_FLOAT:
		return "float";
	case SF_LAYOUT_MODE_MAX_EDGES:
		return "max_edges";
	case SF_LAYOUT_MODE_MAX_GAPS:
		return "max_gaps";
	case SF_LAYOUT_MODE_SNAP:
		return "snap";
	case SF_LAYOUT_MODE_FULLSCREEN:
		return "fullscreen";
	}
	return "float";
}

void sf_windows_fill_info(struct sf_toplevel *toplevel, struct sf_toplevel *focused,
			  struct sf_window_info *out) {
	if (toplevel == NULL || out == NULL) {
		return;
	}

	struct wlr_box geom;
	sf_toplevel_layout_geom(toplevel, &geom);

	memset(out, 0, sizeof(*out));
	out->id = toplevel->id;
	copy_str(out->app_id, sizeof(out->app_id), sf_toplevel_app_id(toplevel));
	copy_str(out->title, sizeof(out->title), sf_toplevel_title(toplevel));
	copy_str(out->tag, sizeof(out->tag), toplevel->tag);
	copy_str(out->description, sizeof(out->description), toplevel->description);
	copy_str(out->icon_name, sizeof(out->icon_name), toplevel->icon_name);
	out->x = geom.x;
	out->y = geom.y;
	out->w = geom.width;
	out->h = geom.height;
	out->focused = toplevel == focused;
	out->floating = sf_layout_sm_is_float(&toplevel->layout);
	out->maximized_to_edges = sf_layout_sm_is_max_edges(&toplevel->layout);
	out->maximized_gaps = sf_layout_sm_is_max_gaps(&toplevel->layout);
	out->fullscreen = sf_layout_sm_is_fullscreen(&toplevel->layout);
	out->minimized = toplevel->minimized;
	out->snapped = sf_layout_sm_snap(&toplevel->layout) != SF_SNAP_NONE;
	out->urgent = toplevel->urgent;
	out->capture_blocked = toplevel->capture_blocked;
	out->always_on_top = toplevel->always_on_top;
	out->scene_enabled = toplevel->scene_tree != NULL && toplevel->scene_tree->node.enabled;
	out->arrange_floating = toplevel->arrange_floating;
	out->arrange_managed = toplevel->arrange_managed;
	out->soft = sf_place_sm_is_soft(&toplevel->place);
	out->appear_hold = sf_place_sm_is_holding(&toplevel->place);
	out->modal = toplevel->modal;
	out->borderless = toplevel->borderless;
	out->xwayland = toplevel->xwayland_surface != NULL;
	out->client_blur = toplevel->client_blur;
	copy_str(out->layout, sizeof(out->layout), layout_mode_name(toplevel->layout.mode));
	copy_str(out->snap_dir, sizeof(out->snap_dir),
		 sf_snap_name(sf_layout_sm_snap(&toplevel->layout)));
	copy_str(out->scratchpad, sizeof(out->scratchpad), toplevel->scratchpad);
	if (toplevel->column != NULL) {
		out->column = sf_column_index(toplevel->column);
		out->column_row = sf_column_row_of(toplevel);
	} else {
		out->column = -1;
		out->column_row = -1;
	}
	if (toplevel->workspace != NULL) {
		out->workspace = toplevel->workspace->index + 1;
		struct wlr_output *wo = toplevel->workspace->output->wlr_output;
		copy_str(out->output, sizeof(out->output), wo != NULL ? wo->name : "");
	} else {
		out->workspace = 0;
		struct sf_output *o = sf_output_for_toplevel(toplevel);
		copy_str(out->output, sizeof(out->output),
			 o != NULL && o->wlr_output != NULL ? o->wlr_output->name : "");
	}
}

size_t sf_windows_collect(struct sf_server *server, struct sf_window_info *out, size_t cap) {
	if (server == NULL || out == NULL || cap == 0) {
		return 0;
	}

	struct sf_toplevel *focused = sf_focused_toplevel(server);
	size_t n = 0;

	/* toplevels link order is top-first; reverse → bottom-most first. */
	struct sf_toplevel *toplevel;
	wl_list_for_each_reverse(toplevel, &server->toplevels, link) {
		if (n >= cap) {
			break;
		}
		sf_windows_fill_info(toplevel, focused, &out[n++]);
	}

	return n;
}

void sf_windows_print(const struct sf_window_info *infos, size_t n, FILE *fp) {
	if (fp == NULL) {
		fp = stdout;
	}
	for (size_t i = 0; i < n; i++) {
		const struct sf_window_info *w = &infos[i];
		fprintf(fp, "%s %llu\t%s\t%s", w->focused ? "*" : " ", (unsigned long long)w->id,
			w->app_id, w->title);
		if (w->layout[0] != '\0') {
			fprintf(fp, "\t%s", w->layout);
		}
		if (w->tag[0] != '\0') {
			fprintf(fp, "\ttag=%s", w->tag);
		}
		if (w->scratchpad[0] != '\0') {
			fprintf(fp, "\tscratch=%s", w->scratchpad);
		}
		if (w->column >= 0) {
			fprintf(fp, "\tcol=%d:%d", w->column, w->column_row);
		}
		if (w->arrange_floating) {
			fprintf(fp, "\tarrange_float");
		}
		if (w->soft) {
			fprintf(fp, "\tsoft");
		}
		if (w->appear_hold) {
			fprintf(fp, "\thold");
		}
		if (w->icon_name[0] != '\0') {
			fprintf(fp, "\ticon=%s", w->icon_name);
		}
		if (w->urgent) {
			fprintf(fp, "\turgent");
		}
		if (w->capture_blocked) {
			fprintf(fp, "\tcapture=blocked");
		}
		if (w->always_on_top) {
			fprintf(fp, "\talways_on_top");
		}
		if (w->xwayland) {
			fprintf(fp, "\txwayland");
		}
		fputc('\n', fp);
	}
}
