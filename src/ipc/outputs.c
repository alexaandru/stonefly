#define _POSIX_C_SOURCE 200809L

#include "ipc/outputs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>

#include "output/output.h"
#include "server/server.h"
#include "util/json.h"

size_t stonefly_outputs_collect(struct stonefly_server *server,
		struct stonefly_output_info *out, size_t cap) {
	if (server == NULL || out == NULL || cap == 0) {
		return 0;
	}

	struct wlr_output *focused_wlr = NULL;
	if (server->cursor != NULL && server->output_layout != NULL) {
		focused_wlr = wlr_output_layout_output_at(server->output_layout,
			server->cursor->x, server->cursor->y);
	}

	size_t n = 0;
	struct stonefly_output *output;
	wl_list_for_each(output, &server->outputs, link) {
		if (n >= cap) {
			break;
		}
		struct wlr_output *wlr = output->wlr_output;
		if (wlr == NULL) {
			continue;
		}

		struct stonefly_output_info *info = &out[n++];
		memset(info, 0, sizeof(*info));
		snprintf(info->name, sizeof(info->name), "%s",
			wlr->name != NULL ? wlr->name : "");
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

int stonefly_outputs_to_json(const struct stonefly_output_info *infos,
		size_t n, char *buf, size_t cap) {
	struct json_buf b;
	json_buf_init(&b, buf, cap);

	json_byte(&b, '[');
	for (size_t i = 0; i < n; i++) {
		const struct stonefly_output_info *o = &infos[i];
		if (i > 0) {
			json_byte(&b, ',');
		}
		json_byte(&b, '{');

		json_key(&b, "name");
		json_str(&b, o->name);

		json_byte(&b, ',');
		json_key(&b, "enabled");
		json_bool(&b, o->enabled);

		json_byte(&b, ',');
		json_key(&b, "x");
		json_i64(&b, o->x);
		json_byte(&b, ',');
		json_key(&b, "y");
		json_i64(&b, o->y);
		json_byte(&b, ',');
		json_key(&b, "w");
		json_i64(&b, o->w);
		json_byte(&b, ',');
		json_key(&b, "h");
		json_i64(&b, o->h);

		json_byte(&b, ',');
		json_key(&b, "logical_w");
		json_i64(&b, o->logical_w);
		json_byte(&b, ',');
		json_key(&b, "logical_h");
		json_i64(&b, o->logical_h);

		json_byte(&b, ',');
		json_key(&b, "scale");
		char scalebuf[32];
		snprintf(scalebuf, sizeof(scalebuf), "%.3f", o->scale);
		json_raw(&b, scalebuf);

		json_byte(&b, ',');
		json_key(&b, "usable");
		json_byte(&b, '{');
		json_key(&b, "x");
		json_i64(&b, o->usable_x);
		json_byte(&b, ',');
		json_key(&b, "y");
		json_i64(&b, o->usable_y);
		json_byte(&b, ',');
		json_key(&b, "w");
		json_i64(&b, o->usable_w);
		json_byte(&b, ',');
		json_key(&b, "h");
		json_i64(&b, o->usable_h);
		json_byte(&b, '}');

		json_byte(&b, ',');
		json_key(&b, "focused");
		json_bool(&b, o->focused);

		json_byte(&b, '}');
	}
	json_byte(&b, ']');
	json_finish(&b);
	if (!json_ok(&b)) {
		return -1;
	}
	return (int)b.len;
}

static bool scan_number_token(struct json_scan *s, char *out, size_t cap) {
	json_scan_skip_ws(s);
	if (!json_scan_ok(s) || s->p >= s->end || cap == 0) {
		return false;
	}
	const char *start = s->p;
	if (*s->p == '-' || *s->p == '+') {
		s->p++;
	}
	bool digits = false;
	while (s->p < s->end && *s->p >= '0' && *s->p <= '9') {
		s->p++;
		digits = true;
	}
	if (s->p < s->end && *s->p == '.') {
		s->p++;
		while (s->p < s->end && *s->p >= '0' && *s->p <= '9') {
			s->p++;
			digits = true;
		}
	}
	if (!digits) {
		return false;
	}
	size_t n = (size_t)(s->p - start);
	if (n + 1 > cap) {
		return false;
	}
	memcpy(out, start, n);
	out[n] = '\0';
	return true;
}

static bool parse_usable(struct json_scan *s, struct stonefly_output_info *out) {
	if (!json_scan_expect(s, '{')) {
		return false;
	}
	json_scan_skip_ws(s);
	if (json_scan_ok(s) && s->p < s->end && *s->p == '}') {
		s->p++;
		return true;
	}
	for (;;) {
		char key[32];
		if (!json_scan_string(s, key, sizeof(key)) ||
				!json_scan_expect(s, ':')) {
			return false;
		}
		int64_t v = 0;
		if (!json_scan_i64(s, &v)) {
			return false;
		}
		if (strcmp(key, "x") == 0) {
			out->usable_x = (int32_t)v;
		} else if (strcmp(key, "y") == 0) {
			out->usable_y = (int32_t)v;
		} else if (strcmp(key, "w") == 0) {
			out->usable_w = (int32_t)v;
		} else if (strcmp(key, "h") == 0) {
			out->usable_h = (int32_t)v;
		}
		json_scan_skip_ws(s);
		if (s->p < s->end && *s->p == ',') {
			s->p++;
			continue;
		}
		return json_scan_expect(s, '}');
	}
}

static bool parse_object(struct json_scan *s, struct stonefly_output_info *out) {
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
		if (!json_scan_string(s, key, sizeof(key)) ||
				!json_scan_expect(s, ':')) {
			return false;
		}

		if (strcmp(key, "name") == 0) {
			if (!json_scan_string(s, out->name, sizeof(out->name))) {
				return false;
			}
		} else if (strcmp(key, "enabled") == 0 ||
				strcmp(key, "focused") == 0) {
			bool v = false;
			if (!json_scan_bool(s, &v)) {
				return false;
			}
			if (strcmp(key, "enabled") == 0) {
				out->enabled = v;
			} else {
				out->focused = v;
			}
		} else if (strcmp(key, "x") == 0 || strcmp(key, "y") == 0 ||
				strcmp(key, "w") == 0 || strcmp(key, "h") == 0 ||
				strcmp(key, "logical_w") == 0 ||
				strcmp(key, "logical_h") == 0) {
			int64_t v = 0;
			if (!json_scan_i64(s, &v)) {
				return false;
			}
			if (strcmp(key, "x") == 0) {
				out->x = (int32_t)v;
			} else if (strcmp(key, "y") == 0) {
				out->y = (int32_t)v;
			} else if (strcmp(key, "w") == 0) {
				out->w = (int32_t)v;
			} else if (strcmp(key, "h") == 0) {
				out->h = (int32_t)v;
			} else if (strcmp(key, "logical_w") == 0) {
				out->logical_w = (int32_t)v;
			} else {
				out->logical_h = (int32_t)v;
			}
		} else if (strcmp(key, "scale") == 0) {
			char num[32];
			if (!scan_number_token(s, num, sizeof(num))) {
				return false;
			}
			out->scale = strtod(num, NULL);
		} else if (strcmp(key, "usable") == 0) {
			if (!parse_usable(s, out)) {
				return false;
			}
		} else {
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
			} else if (s->p < s->end && *s->p == '{') {
				/* Skip unknown object by consuming braces roughly via usable. */
				struct stonefly_output_info discard_info;
				if (!parse_usable(s, &discard_info)) {
					return false;
				}
			} else {
				char discard[64];
				if (!scan_number_token(s, discard, sizeof(discard))) {
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

bool stonefly_outputs_from_json(const char *json,
		struct stonefly_output_info *out, size_t cap, size_t *n_out) {
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

void stonefly_outputs_print(const struct stonefly_output_info *infos,
		size_t n, FILE *fp) {
	for (size_t i = 0; i < n; i++) {
		const struct stonefly_output_info *o = &infos[i];
		fprintf(fp, "%s%s  %dx%d@%.2f  layout %d,%d %dx%d  usable %d,%d %dx%d\n",
			o->name,
			o->focused ? "*" : " ",
			o->w, o->h, o->scale,
			o->x, o->y, o->logical_w, o->logical_h,
			o->usable_x, o->usable_y, o->usable_w, o->usable_h);
	}
}
