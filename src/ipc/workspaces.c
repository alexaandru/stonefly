#include "ipc/workspaces.h"

#include <stdio.h>
#include <string.h>
#include <wlr/types/wlr_output.h>

#include "output/output.h"
#include "server/server.h"
#include "wm/scratchpad.h"
#include "wm/workspace.h"
#include "toplevel/toplevel.h"

static int workspace_member_count(struct sf_workspace *ws) {
	int n = 0;
	struct sf_toplevel *t;
	wl_list_for_each(t, &ws->output->server->toplevels, link) {
		if (t->workspace == ws && !sf_toplevel_is_scratchpad(t)) {
			n++;
		}
	}
	wl_list_for_each(t, &ws->output->server->minimized, minimize_link) {
		if (t->workspace == ws && !sf_toplevel_is_scratchpad(t)) {
			n++;
		}
	}
	return n;
}

size_t sf_workspaces_collect(struct sf_server *server, struct sf_workspace_info *out, size_t cap) {
	if (server == NULL || out == NULL || cap == 0) {
		return 0;
	}
	size_t n = 0;
	struct sf_output *output;
	wl_list_for_each(output, &server->outputs, link) {
		struct sf_workspace *ws;
		wl_list_for_each(ws, &output->workspaces, link) {
			if (n >= cap) {
				return n;
			}
			struct sf_workspace_info *info = &out[n++];
			memset(info, 0, sizeof(*info));
			snprintf(info->output, sizeof(info->output), "%s",
				 output->wlr_output->name != NULL ? output->wlr_output->name : "");
			info->index = ws->index + 1;
			snprintf(info->name, sizeof(info->name), "%s", ws->name);
			info->active = (output->active == ws);
			info->windows = workspace_member_count(ws);
			info->empty = info->windows == 0;
			snprintf(info->layout, sizeof(info->layout), "%s",
				 sf_arrange_name(sf_workspace_arrange(ws)));
		}
	}
	return n;
}

void sf_workspaces_print(const struct sf_workspace_info *infos, size_t n, FILE *fp) {
	if (fp == NULL) {
		fp = stdout;
	}
	for (size_t i = 0; i < n; i++) {
		const struct sf_workspace_info *w = &infos[i];
		fprintf(fp, "%s%s  %s  ws %d  %s  windows=%d  layout=%s\n", w->active ? "*" : " ",
			w->empty ? "." : " ", w->output, w->index, w->name, w->windows, w->layout);
	}
}
