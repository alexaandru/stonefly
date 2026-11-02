#define _POSIX_C_SOURCE 200809L

#include "ipc/layers.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_output.h>
#include <scenefx/types/wlr_scene.h>

#include "layer/layer.h"
#include "output/output.h"
#include "server/server.h"
#include "util/json.h"

static void copy_str(char *dst, size_t cap, const char *src) {
	if (src == NULL) {
		src = "";
	}
	snprintf(dst, cap, "%s", src);
}

static const char *layer_name(enum zwlr_layer_shell_v1_layer layer) {
	switch (layer) {
	case ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND:
		return "background";
	case ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM:
		return "bottom";
	case ZWLR_LAYER_SHELL_V1_LAYER_TOP:
		return "top";
	case ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY:
		return "overlay";
	}
	return "unknown";
}

static const char *keyboard_name(
		enum zwlr_layer_surface_v1_keyboard_interactivity kbd) {
	switch (kbd) {
	case ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE:
		return "none";
	case ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE:
		return "exclusive";
	case ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_ON_DEMAND:
		return "on_demand";
	}
	return "unknown";
}

size_t stonefly_layers_collect(struct stonefly_server *server,
		struct stonefly_layer_info *out, size_t cap) {
	if (server == NULL || out == NULL || cap == 0) {
		return 0;
	}

	size_t n = 0;
	struct stonefly_output *output;
	wl_list_for_each(output, &server->outputs, link) {
		for (int layer_idx = ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND;
				layer_idx <= ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY;
				layer_idx++) {
			struct stonefly_layer_surface *layer;
			wl_list_for_each(layer, &output->layers[layer_idx], link) {
				if (n >= cap) {
					return n;
				}

				struct wlr_layer_surface_v1 *ls = layer->layer_surface;
				struct wlr_layer_surface_v1_state *state = &ls->current;

				struct stonefly_layer_info *info = &out[n++];
				memset(info, 0, sizeof(*info));
				copy_str(info->namespace, sizeof(info->namespace), ls->namespace);
				copy_str(info->output, sizeof(info->output),
					output->wlr_output != NULL ? output->wlr_output->name : "");
				copy_str(info->layer, sizeof(info->layer),
					layer_name(state->layer));
				copy_str(info->keyboard, sizeof(info->keyboard),
					keyboard_name(state->keyboard_interactive));
				info->x = layer->scene != NULL ? layer->scene->node.x : 0;
				info->y = layer->scene != NULL ? layer->scene->node.y : 0;
				info->w = (int32_t)state->actual_width;
				info->h = (int32_t)state->actual_height;
				if (info->w <= 0 || info->h <= 0) {
					info->w = ls->surface->current.width;
					info->h = ls->surface->current.height;
				}
				info->exclusive_zone = state->exclusive_zone;
				info->anchor = state->anchor;
				info->mapped = ls->surface->mapped;
				info->focused = server->exclusive_layer == layer;
			}
		}
	}

	return n;
}

int stonefly_layers_to_json(const struct stonefly_layer_info *infos,
		size_t n, char *buf, size_t cap) {
	struct json_buf b;
	json_buf_init(&b, buf, cap);

	json_byte(&b, '[');
	for (size_t i = 0; i < n; i++) {
		const struct stonefly_layer_info *L = &infos[i];
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

static bool parse_object(struct json_scan *s, struct stonefly_layer_info *out) {
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
				strcmp(key, "exclusive_zone") == 0 ||
				strcmp(key, "anchor") == 0) {
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

bool stonefly_layers_from_json(const char *json,
		struct stonefly_layer_info *out, size_t cap, size_t *n_out) {
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

void stonefly_layers_print(const struct stonefly_layer_info *infos,
		size_t n, FILE *fp) {
	if (fp == NULL) {
		fp = stdout;
	}
	for (size_t i = 0; i < n; i++) {
		const struct stonefly_layer_info *L = &infos[i];
		fprintf(fp, "%s %-28s %-10s %-8s %d,%d %dx%d excl=%d kbd=%s%s\n",
			L->focused ? "*" : " ",
			L->namespace[0] != '\0' ? L->namespace : "-",
			L->layer,
			L->output[0] != '\0' ? L->output : "-",
			L->x, L->y, L->w, L->h,
			L->exclusive_zone,
			L->keyboard,
			L->mapped ? "" : " (unmapped)");
	}
}
