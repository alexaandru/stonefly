#include "ipc/windows.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "util/json.h"

static void emit_bool(struct json_buf *b, const char *key, bool v) {
	json_byte(b, ',');
	json_key(b, key);
	json_bool(b, v);
}

static void emit_i64(struct json_buf *b, const char *key, int64_t v) {
	json_byte(b, ',');
	json_key(b, key);
	json_i64(b, v);
}

static void emit_str(struct json_buf *b, const char *key, const char *v) {
	json_byte(b, ',');
	json_key(b, key);
	json_str(b, v != NULL ? v : "");
}

void sf_windows_info_to_json_object(struct json_buf *b, const struct sf_window_info *w) {
	json_byte(b, '{');

	json_key(b, "id");
	char idbuf[32];
	snprintf(idbuf, sizeof(idbuf), "%llu", (unsigned long long)w->id);
	json_str(b, idbuf);

	emit_str(b, "app_id", w->app_id);
	emit_str(b, "title", w->title);
	emit_str(b, "tag", w->tag);
	emit_str(b, "description", w->description);
	emit_str(b, "icon_name", w->icon_name);
	emit_i64(b, "x", w->x);
	emit_i64(b, "y", w->y);
	emit_i64(b, "w", w->w);
	emit_i64(b, "h", w->h);
	emit_bool(b, "focused", w->focused);
	emit_bool(b, "floating", w->floating);
	emit_bool(b, "maximized_to_edges", w->maximized_to_edges);
	emit_bool(b, "maximized_gaps", w->maximized_gaps);
	emit_bool(b, "fullscreen", w->fullscreen);
	emit_bool(b, "minimized", w->minimized);
	emit_bool(b, "snapped", w->snapped);
	emit_bool(b, "urgent", w->urgent);
	emit_bool(b, "capture_blocked", w->capture_blocked);
	emit_bool(b, "always_on_top", w->always_on_top);
	emit_bool(b, "scene_enabled", w->scene_enabled);
	emit_bool(b, "arrange_floating", w->arrange_floating);
	emit_bool(b, "arrange_managed", w->arrange_managed);
	emit_bool(b, "soft", w->soft);
	emit_bool(b, "appear_hold", w->appear_hold);
	emit_bool(b, "modal", w->modal);
	emit_bool(b, "borderless", w->borderless);
	emit_bool(b, "xwayland", w->xwayland);
	emit_bool(b, "client_blur", w->client_blur);
	emit_str(b, "layout", w->layout);
	if (w->snap_dir[0] != '\0') {
		emit_str(b, "snap_dir", w->snap_dir);
	}
	emit_str(b, "scratchpad", w->scratchpad);
	emit_str(b, "output", w->output);
	emit_i64(b, "workspace", w->workspace);
	emit_i64(b, "column", w->column);
	emit_i64(b, "column_row", w->column_row);

	json_byte(b, '}');
}

int sf_windows_to_json(const struct sf_window_info *infos, size_t n, char *buf, size_t cap) {
	struct json_buf b;
	json_buf_init(&b, buf, cap);

	json_byte(&b, '[');
	for (size_t i = 0; i < n; i++) {
		if (i > 0) {
			json_byte(&b, ',');
		}
		sf_windows_info_to_json_object(&b, &infos[i]);
	}
	json_byte(&b, ']');
	json_finish(&b);

	if (!json_ok(&b)) {
		return -1;
	}
	return (int)b.len;
}

static bool parse_object(struct json_scan *s, struct sf_window_info *out) {
	memset(out, 0, sizeof(*out));
	out->column = -1;
	out->column_row = -1;
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
		} else if (strcmp(key, "icon_name") == 0) {
			if (!json_scan_string(s, out->icon_name, sizeof(out->icon_name))) {
				return false;
			}
		} else if (strcmp(key, "layout") == 0) {
			if (!json_scan_string(s, out->layout, sizeof(out->layout))) {
				return false;
			}
		} else if (strcmp(key, "snap_dir") == 0) {
			if (!json_scan_string(s, out->snap_dir, sizeof(out->snap_dir))) {
				return false;
			}
		} else if (strcmp(key, "scratchpad") == 0) {
			if (!json_scan_string(s, out->scratchpad, sizeof(out->scratchpad))) {
				return false;
			}
		} else if (strcmp(key, "output") == 0) {
			if (!json_scan_string(s, out->output, sizeof(out->output))) {
				return false;
			}
		} else if (strcmp(key, "x") == 0 || strcmp(key, "y") == 0 ||
			   strcmp(key, "w") == 0 || strcmp(key, "h") == 0 ||
			   strcmp(key, "workspace") == 0 || strcmp(key, "column") == 0 ||
			   strcmp(key, "column_row") == 0) {
			int64_t v = 0;
			if (!json_scan_i64(s, &v)) {
				return false;
			}
			if (strcmp(key, "workspace") == 0) {
				out->workspace = (int)v;
			} else if (strcmp(key, "column") == 0) {
				out->column = (int)v;
			} else if (strcmp(key, "column_row") == 0) {
				out->column_row = (int)v;
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
			   strcmp(key, "maximized_gaps") == 0 || strcmp(key, "fullscreen") == 0 ||
			   strcmp(key, "minimized") == 0 || strcmp(key, "snapped") == 0 ||
			   strcmp(key, "urgent") == 0 || strcmp(key, "capture_blocked") == 0 ||
			   strcmp(key, "always_on_top") == 0 || strcmp(key, "scene_enabled") == 0 ||
			   strcmp(key, "arrange_floating") == 0 ||
			   strcmp(key, "arrange_managed") == 0 || strcmp(key, "soft") == 0 ||
			   strcmp(key, "appear_hold") == 0 || strcmp(key, "modal") == 0 ||
			   strcmp(key, "borderless") == 0 || strcmp(key, "xwayland") == 0 ||
			   strcmp(key, "client_blur") == 0) {
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
			} else if (strcmp(key, "maximized_gaps") == 0) {
				out->maximized_gaps = v;
			} else if (strcmp(key, "fullscreen") == 0) {
				out->fullscreen = v;
			} else if (strcmp(key, "minimized") == 0) {
				out->minimized = v;
			} else if (strcmp(key, "snapped") == 0) {
				out->snapped = v;
			} else if (strcmp(key, "urgent") == 0) {
				out->urgent = v;
			} else if (strcmp(key, "capture_blocked") == 0) {
				out->capture_blocked = v;
			} else if (strcmp(key, "always_on_top") == 0) {
				out->always_on_top = v;
			} else if (strcmp(key, "scene_enabled") == 0) {
				out->scene_enabled = v;
			} else if (strcmp(key, "arrange_floating") == 0) {
				out->arrange_floating = v;
			} else if (strcmp(key, "arrange_managed") == 0) {
				out->arrange_managed = v;
			} else if (strcmp(key, "soft") == 0) {
				out->soft = v;
			} else if (strcmp(key, "appear_hold") == 0) {
				out->appear_hold = v;
			} else if (strcmp(key, "modal") == 0) {
				out->modal = v;
			} else if (strcmp(key, "borderless") == 0) {
				out->borderless = v;
			} else if (strcmp(key, "xwayland") == 0) {
				out->xwayland = v;
			} else {
				out->client_blur = v;
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

bool sf_windows_from_json(const char *json, struct sf_window_info *out, size_t cap, size_t *n_out) {
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
		break;
	}

	if (n_out != NULL) {
		*n_out = n;
	}
	return json_scan_ok(&s);
}
