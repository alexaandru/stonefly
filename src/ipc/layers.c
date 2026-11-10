#include "ipc/layers.h"

#include <stdio.h>
#include <string.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_output.h>
#include <scenefx/types/wlr_scene.h>

#include "layer/layer.h"
#include "output/output.h"
#include "server/server.h"

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

static const char *keyboard_name(enum zwlr_layer_surface_v1_keyboard_interactivity kbd) {
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

size_t sf_layers_collect(struct sf_server *server, struct sf_layer_info *out, size_t cap) {
	if (server == NULL || out == NULL || cap == 0) {
		return 0;
	}

	size_t n = 0;
	struct sf_output *output;
	wl_list_for_each(output, &server->outputs, link) {
		for (int layer_idx = ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND;
		     layer_idx <= ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY; layer_idx++) {
			struct sf_layer_surface *layer;
			wl_list_for_each(layer, &output->layers[layer_idx], link) {
				if (n >= cap) {
					return n;
				}

				struct wlr_layer_surface_v1 *ls = layer->layer_surface;
				struct wlr_layer_surface_v1_state *state = &ls->current;

				struct sf_layer_info *info = &out[n++];
				memset(info, 0, sizeof(*info));
				copy_str(info->namespace, sizeof(info->namespace), ls->namespace);
				copy_str(info->output, sizeof(info->output),
					 output->wlr_output != NULL ? output->wlr_output->name
								    : "");
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

void sf_layers_print(const struct sf_layer_info *infos, size_t n, FILE *fp) {
	if (fp == NULL) {
		fp = stdout;
	}
	for (size_t i = 0; i < n; i++) {
		const struct sf_layer_info *L = &infos[i];
		fprintf(fp, "%s %-28s %-10s %-8s %d,%d %dx%d excl=%d kbd=%s%s\n",
			L->focused ? "*" : " ", L->namespace[0] != '\0' ? L->namespace : "-",
			L->layer, L->output[0] != '\0' ? L->output : "-", L->x, L->y, L->w, L->h,
			L->exclusive_zone, L->keyboard, L->mapped ? "" : " (unmapped)");
	}
}
