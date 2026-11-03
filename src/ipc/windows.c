#define _POSIX_C_SOURCE 200809L

#include "ipc/windows.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wlr/types/wlr_output.h>
#include <wlr/util/box.h>

#include "output/output.h"
#include "server/server.h"
#include "util/json.h"
#include "wm/wm.h"
#include "wm/workspace.h"
#include "xdg/xdg.h"

static const char *snap_dir_name(enum stonefly_snap snap) {
	switch (snap) {
	case STONEFLY_SNAP_LEFT:
		return "left";
	case STONEFLY_SNAP_RIGHT:
		return "right";
	case STONEFLY_SNAP_TOP:
		return "top";
	case STONEFLY_SNAP_BOTTOM:
		return "bottom";
	case STONEFLY_SNAP_NONE:
		break;
	}
	return "";
}

static void copy_str(char *dst, size_t cap, const char *src) {
	if (src == NULL) {
		src = "";
	}
	snprintf(dst, cap, "%s", src);
}

size_t stonefly_windows_collect(struct stonefly_server *server, struct stonefly_window_info *out,
				size_t cap) {
	if (server == NULL || out == NULL || cap == 0) {
		return 0;
	}

	struct stonefly_toplevel *focused = stonefly_focused_toplevel(server);
	size_t n = 0;

	/* toplevels link order is top-first; reverse → bottom-most first. */
	struct stonefly_toplevel *toplevel;
	wl_list_for_each_reverse(toplevel, &server->toplevels, link) {
		if (n >= cap) {
			break;
		}

		struct wlr_box geom;
		stonefly_toplevel_layout_geom(toplevel, &geom);

		struct stonefly_window_info *info = &out[n++];
		memset(info, 0, sizeof(*info));
		info->id = toplevel->id;
		copy_str(info->app_id, sizeof(info->app_id), toplevel->xdg_toplevel->app_id);
		copy_str(info->title, sizeof(info->title), toplevel->xdg_toplevel->title);
		copy_str(info->tag, sizeof(info->tag), toplevel->tag);
		copy_str(info->description, sizeof(info->description), toplevel->description);
		info->x = geom.x;
		info->y = geom.y;
		info->w = geom.width;
		info->h = geom.height;
		info->focused = toplevel == focused;
		info->maximized_to_edges = toplevel->maximized_edges;
		info->minimized = toplevel->minimized;
		info->snapped = toplevel->snapped != STONEFLY_SNAP_NONE;
		info->capture_blocked = toplevel->capture_blocked;
		info->floating =
			!info->maximized_to_edges && !toplevel->maximized_gaps && !info->snapped;
		copy_str(info->snap_dir, sizeof(info->snap_dir), snap_dir_name(toplevel->snapped));
		if (toplevel->workspace != NULL) {
			info->workspace = toplevel->workspace->index + 1;
			struct wlr_output *wo = toplevel->workspace->output->wlr_output;
			copy_str(info->output, sizeof(info->output), wo != NULL ? wo->name : "");
		} else {
			info->workspace = 0;
			struct stonefly_output *out = stonefly_output_for_toplevel(toplevel);
			copy_str(info->output, sizeof(info->output),
				 out != NULL && out->wlr_output != NULL ? out->wlr_output->name
									: "");
		}
	}

	return n;
}

int stonefly_windows_to_json(const struct stonefly_window_info *infos, size_t n, char *buf,
			     size_t cap) {
	struct json_buf b;
	json_buf_init(&b, buf, cap);

	json_byte(&b, '[');
	for (size_t i = 0; i < n; i++) {
		const struct stonefly_window_info *w = &infos[i];
		if (i > 0) {
			json_byte(&b, ',');
		}
		json_byte(&b, '{');

		json_key(&b, "id");
		char idbuf[32];
		snprintf(idbuf, sizeof(idbuf), "%llu", (unsigned long long)w->id);
		json_str(&b, idbuf);

		json_byte(&b, ',');
		json_key(&b, "app_id");
		json_str(&b, w->app_id);

		json_byte(&b, ',');
		json_key(&b, "title");
		json_str(&b, w->title);

		json_byte(&b, ',');
		json_key(&b, "tag");
		json_str(&b, w->tag);

		json_byte(&b, ',');
		json_key(&b, "description");
		json_str(&b, w->description);

		json_byte(&b, ',');
		json_key(&b, "x");
		json_i64(&b, w->x);
		json_byte(&b, ',');
		json_key(&b, "y");
		json_i64(&b, w->y);
		json_byte(&b, ',');
		json_key(&b, "w");
		json_i64(&b, w->w);
		json_byte(&b, ',');
		json_key(&b, "h");
		json_i64(&b, w->h);

		json_byte(&b, ',');
		json_key(&b, "focused");
		json_bool(&b, w->focused);

		json_byte(&b, ',');
		json_key(&b, "floating");
		json_bool(&b, w->floating);

		json_byte(&b, ',');
		json_key(&b, "maximized_to_edges");
		json_bool(&b, w->maximized_to_edges);

		json_byte(&b, ',');
		json_key(&b, "minimized");
		json_bool(&b, w->minimized);

		json_byte(&b, ',');
		json_key(&b, "snapped");
		json_bool(&b, w->snapped);

		json_byte(&b, ',');
		json_key(&b, "capture_blocked");
		json_bool(&b, w->capture_blocked);

		if (w->snap_dir[0] != '\0') {
			json_byte(&b, ',');
			json_key(&b, "snap_dir");
			json_str(&b, w->snap_dir);
		}

		json_byte(&b, ',');
		json_key(&b, "output");
		json_str(&b, w->output);
		json_byte(&b, ',');
		json_key(&b, "workspace");
		json_i64(&b, w->workspace);

		json_byte(&b, '}');
	}
	json_byte(&b, ']');
	json_finish(&b);

	if (!json_ok(&b)) {
		return -1;
	}
	return (int)b.len;
}

static bool parse_object(struct json_scan *s, struct stonefly_window_info *out) {
	memset(out, 0, sizeof(*out));
	if (!json_scan_expect(s, '{')) {
		return false;
	}

	json_scan_skip_ws(s);
	if (json_scan_ok(s) && s->p < s->end && *s->p == '}') {
		s->p++;
		return true;
	}

	for (;;) {
		char key[64];
		if (!json_scan_string(s, key, sizeof(key)) || !json_scan_expect(s, ':')) {
			return false;
		}

		if (strcmp(key, "id") == 0) {
			char idbuf[32];
			if (!json_scan_string(s, idbuf, sizeof(idbuf))) {
				return false;
			}
			out->id = (uint64_t)strtoull(idbuf, NULL, 10);
		} else if (strcmp(key, "app_id") == 0) {
			if (!json_scan_string(s, out->app_id, sizeof(out->app_id))) {
				return false;
			}
		} else if (strcmp(key, "title") == 0) {
			if (!json_scan_string(s, out->title, sizeof(out->title))) {
				return false;
			}
		} else if (strcmp(key, "tag") == 0) {
			if (!json_scan_string(s, out->tag, sizeof(out->tag))) {
				return false;
			}
		} else if (strcmp(key, "description") == 0) {
			if (!json_scan_string(s, out->description, sizeof(out->description))) {
				return false;
			}
		} else if (strcmp(key, "snap_dir") == 0) {
			if (!json_scan_string(s, out->snap_dir, sizeof(out->snap_dir))) {
				return false;
			}
		} else if (strcmp(key, "output") == 0) {
			if (!json_scan_string(s, out->output, sizeof(out->output))) {
				return false;
			}
		} else if (strcmp(key, "x") == 0 || strcmp(key, "y") == 0 ||
			   strcmp(key, "w") == 0 || strcmp(key, "h") == 0 ||
			   strcmp(key, "workspace") == 0) {
			int64_t v = 0;
			if (!json_scan_i64(s, &v)) {
				return false;
			}
			if (strcmp(key, "workspace") == 0) {
				out->workspace = (int)v;
			} else if (key[0] == 'x') {
				out->x = (int32_t)v;
			} else if (key[0] == 'y') {
				out->y = (int32_t)v;
			} else if (key[0] == 'w') {
				out->w = (int32_t)v;
			} else {
				out->h = (int32_t)v;
			}
		} else if (strcmp(key, "focused") == 0 || strcmp(key, "floating") == 0 ||
			   strcmp(key, "maximized_to_edges") == 0 ||
			   strcmp(key, "minimized") == 0 || strcmp(key, "snapped") == 0 ||
			   strcmp(key, "capture_blocked") == 0) {
			bool v = false;
			if (!json_scan_bool(s, &v)) {
				return false;
			}
			if (strcmp(key, "focused") == 0) {
				out->focused = v;
			} else if (strcmp(key, "floating") == 0) {
				out->floating = v;
			} else if (strcmp(key, "maximized_to_edges") == 0) {
				out->maximized_to_edges = v;
			} else if (strcmp(key, "minimized") == 0) {
				out->minimized = v;
			} else if (strcmp(key, "snapped") == 0) {
				out->snapped = v;
			} else {
				out->capture_blocked = v;
			}
		} else {
			/* Skip unknown value: string, bool, or integer. */
			json_scan_skip_ws(s);
			if (s->p < s->end && *s->p == '"') {
				char discard[256];
				if (!json_scan_string(s, discard, sizeof(discard))) {
					return false;
				}
			} else if (s->p < s->end && (*s->p == 't' || *s->p == 'f')) {
				bool discard = false;
				if (!json_scan_bool(s, &discard)) {
					return false;
				}
			} else {
				int64_t discard = 0;
				if (!json_scan_i64(s, &discard)) {
					return false;
				}
			}
		}

		json_scan_skip_ws(s);
		if (s->p < s->end && *s->p == ',') {
			s->p++;
			continue;
		}
		return json_scan_expect(s, '}');
	}
}

bool stonefly_windows_from_json(const char *json, struct stonefly_window_info *out, size_t cap,
				size_t *n_out) {
	if (n_out != NULL) {
		*n_out = 0;
	}
	if (json == NULL || out == NULL) {
		return false;
	}

	struct json_scan s;
	json_scan_init(&s, json);
	if (!json_scan_expect(&s, '[')) {
		return false;
	}

	json_scan_skip_ws(&s);
	if (s.p < s.end && *s.p == ']') {
		s.p++;
		return true;
	}

	size_t n = 0;
	for (;;) {
		if (n >= cap) {
			return false;
		}
		if (!parse_object(&s, &out[n])) {
			return false;
		}
		n++;

		json_scan_skip_ws(&s);
		if (s.p < s.end && *s.p == ',') {
			s.p++;
			continue;
		}
		if (!json_scan_expect(&s, ']')) {
			return false;
		}
		if (n_out != NULL) {
			*n_out = n;
		}
		return true;
	}
}

void stonefly_windows_print(const struct stonefly_window_info *infos, size_t n, FILE *fp) {
	if (fp == NULL) {
		fp = stdout;
	}
	for (size_t i = 0; i < n; i++) {
		const struct stonefly_window_info *w = &infos[i];
		if (w->tag[0] != '\0') {
			fprintf(fp, "%s %llu\t%s\t%s\ttag=%s%s\n", w->focused ? "*" : " ",
				(unsigned long long)w->id, w->app_id, w->title, w->tag,
				w->capture_blocked ? "\tcapture=blocked" : "");
		} else if (w->capture_blocked) {
			fprintf(fp, "%s %llu\t%s\t%s\tcapture=blocked\n", w->focused ? "*" : " ",
				(unsigned long long)w->id, w->app_id, w->title);
		} else {
			fprintf(fp, "%s %llu\t%s\t%s\n", w->focused ? "*" : " ",
				(unsigned long long)w->id, w->app_id, w->title);
		}
	}
}
