#include "ipc/layers.h"

#include <string.h>

#include "util/json.h"

int sf_layers_to_json(const struct sf_layer_info *infos, size_t n, char *buf, size_t cap) {
	struct json_buf b;
	json_buf_init(&b, buf, cap);

	json_byte(&b, '[');
	for (size_t i = 0; i < n; i++) {
		const struct sf_layer_info *L = &infos[i];
		if (i > 0) {
			json_byte(&b, ',');
		}
		json_byte(&b, '{');

		json_key(&b, "namespace");
		json_str(&b, L->namespace);

		json_byte(&b, ',');
		json_key(&b, "output");
		json_str(&b, L->output);

		json_byte(&b, ',');
		json_key(&b, "layer");
		json_str(&b, L->layer);

		json_byte(&b, ',');
		json_key(&b, "x");
		json_i64(&b, L->x);
		json_byte(&b, ',');
		json_key(&b, "y");
		json_i64(&b, L->y);
		json_byte(&b, ',');
		json_key(&b, "w");
		json_i64(&b, L->w);
		json_byte(&b, ',');
		json_key(&b, "h");
		json_i64(&b, L->h);

		json_byte(&b, ',');
		json_key(&b, "exclusive_zone");
		json_i64(&b, L->exclusive_zone);

		json_byte(&b, ',');
		json_key(&b, "anchor");
		json_i64(&b, (int64_t)L->anchor);

		json_byte(&b, ',');
		json_key(&b, "keyboard");
		json_str(&b, L->keyboard);

		json_byte(&b, ',');
		json_key(&b, "mapped");
		json_bool(&b, L->mapped);

		json_byte(&b, ',');
		json_key(&b, "focused");
		json_bool(&b, L->focused);

		json_byte(&b, '}');
	}
	json_byte(&b, ']');
	json_finish(&b);

	if (!json_ok(&b)) {
		return -1;
	}
	return (int)b.len;
}

static bool parse_object(struct json_scan *s, struct sf_layer_info *out) {
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

		if (strcmp(key, "namespace") == 0) {
			if (!json_scan_string(s, out->namespace, sizeof(out->namespace))) {
				return false;
			}
		} else if (strcmp(key, "output") == 0) {
			if (!json_scan_string(s, out->output, sizeof(out->output))) {
				return false;
			}
		} else if (strcmp(key, "layer") == 0) {
			if (!json_scan_string(s, out->layer, sizeof(out->layer))) {
				return false;
			}
		} else if (strcmp(key, "keyboard") == 0) {
			if (!json_scan_string(s, out->keyboard, sizeof(out->keyboard))) {
				return false;
			}
		} else if (strcmp(key, "x") == 0 || strcmp(key, "y") == 0 ||
			   strcmp(key, "w") == 0 || strcmp(key, "h") == 0 ||
			   strcmp(key, "exclusive_zone") == 0 || strcmp(key, "anchor") == 0) {
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
			} else if (strcmp(key, "exclusive_zone") == 0) {
				out->exclusive_zone = (int32_t)v;
			} else {
				out->anchor = (uint32_t)v;
			}
		} else if (strcmp(key, "mapped") == 0 || strcmp(key, "focused") == 0) {
			bool v = false;
			if (!json_scan_bool(s, &v)) {
				return false;
			}
			if (strcmp(key, "mapped") == 0) {
				out->mapped = v;
			} else {
				out->focused = v;
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

bool sf_layers_from_json(const char *json, struct sf_layer_info *out, size_t cap, size_t *n_out) {
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
