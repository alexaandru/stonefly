#include "ipc/workspaces.h"

#include <string.h>

#include "util/json.h"

int sf_workspaces_to_json(const struct sf_workspace_info *infos, size_t n, char *buf, size_t cap) {
	struct json_buf b;
	json_buf_init(&b, buf, cap);
	json_byte(&b, '[');
	for (size_t i = 0; i < n; i++) {
		const struct sf_workspace_info *w = &infos[i];
		if (i > 0) {
			json_byte(&b, ',');
		}
		json_byte(&b, '{');
		json_key(&b, "output");
		json_str(&b, w->output);
		json_byte(&b, ',');
		json_key(&b, "index");
		json_i64(&b, w->index);
		json_byte(&b, ',');
		json_key(&b, "name");
		json_str(&b, w->name);
		json_byte(&b, ',');
		json_key(&b, "active");
		json_bool(&b, w->active);
		json_byte(&b, ',');
		json_key(&b, "empty");
		json_bool(&b, w->empty);
		json_byte(&b, ',');
		json_key(&b, "windows");
		json_i64(&b, w->windows);
		json_byte(&b, ',');
		json_key(&b, "layout");
		json_str(&b, w->layout);
		json_byte(&b, '}');
	}
	json_byte(&b, ']');
	json_finish(&b);
	return json_ok(&b) ? (int)b.len : -1;
}

static bool parse_object(struct json_scan *s, struct sf_workspace_info *out) {
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

		if (strcmp(key, "output") == 0) {
			if (!json_scan_string(s, out->output, sizeof(out->output))) {
				return false;
			}
		} else if (strcmp(key, "name") == 0) {
			if (!json_scan_string(s, out->name, sizeof(out->name))) {
				return false;
			}
		} else if (strcmp(key, "layout") == 0) {
			if (!json_scan_string(s, out->layout, sizeof(out->layout))) {
				return false;
			}
		} else if (strcmp(key, "index") == 0 || strcmp(key, "windows") == 0) {
			int64_t v = 0;
			if (!json_scan_i64(s, &v)) {
				return false;
			}
			if (strcmp(key, "index") == 0) {
				out->index = (int)v;
			} else {
				out->windows = (int)v;
			}
		} else if (strcmp(key, "active") == 0 || strcmp(key, "empty") == 0) {
			bool v = false;
			if (!json_scan_bool(s, &v)) {
				return false;
			}
			if (strcmp(key, "active") == 0) {
				out->active = v;
			} else {
				out->empty = v;
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

bool sf_workspaces_from_json(const char *json, struct sf_workspace_info *out, size_t cap,
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
