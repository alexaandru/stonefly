#include "ipc/windows.h"

#include <stdio.h>
#include <string.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/box.h>

#include "output/output.h"
#include "server/server.h"
#include "toplevel/toplevel.h"
#include "wm/wm.h"
#include "wm/workspace.h"

static void copy_str(char *dst, size_t cap, const char *src) {
	if (src == NULL) {
		src = "";
	}
	snprintf(dst, cap, "%s", src);
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

		struct wlr_box geom;
		sf_toplevel_layout_geom(toplevel, &geom);

		struct sf_window_info *info = &out[n++];
		memset(info, 0, sizeof(*info));
		info->id = toplevel->id;
		copy_str(info->app_id, sizeof(info->app_id), toplevel->xdg_toplevel->app_id);
		copy_str(info->title, sizeof(info->title), toplevel->xdg_toplevel->title);
		copy_str(info->tag, sizeof(info->tag), toplevel->tag);
		copy_str(info->description, sizeof(info->description), toplevel->description);
		copy_str(info->icon_name, sizeof(info->icon_name), toplevel->icon_name);
		info->x = geom.x;
		info->y = geom.y;
		info->w = geom.width;
		info->h = geom.height;
		info->focused = toplevel == focused;
		info->maximized_to_edges = toplevel->maximized_edges;
		info->minimized = toplevel->minimized;
		info->snapped = toplevel->snapped != SF_SNAP_NONE;
		info->urgent = toplevel->urgent;
		info->capture_blocked = toplevel->capture_blocked;
		info->floating =
			!info->maximized_to_edges && !toplevel->maximized_gaps && !info->snapped;
		copy_str(info->snap_dir, sizeof(info->snap_dir), sf_snap_name(toplevel->snapped));
		if (toplevel->workspace != NULL) {
			info->workspace = toplevel->workspace->index + 1;
			struct wlr_output *wo = toplevel->workspace->output->wlr_output;
			copy_str(info->output, sizeof(info->output), wo != NULL ? wo->name : "");
		} else {
			info->workspace = 0;
			struct sf_output *out = sf_output_for_toplevel(toplevel);
			copy_str(info->output, sizeof(info->output),
				 out != NULL && out->wlr_output != NULL ? out->wlr_output->name
									: "");
		}
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
		if (w->tag[0] != '\0') {
			fprintf(fp, "\ttag=%s", w->tag);
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
		fputc('\n', fp);
	}
}
