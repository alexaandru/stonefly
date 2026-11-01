#ifndef SF_IPC_LAYERS_H
#define SF_IPC_LAYERS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

struct sf_server;

/* Point-in-time layer-shell surface description (debug aid). */
struct sf_layer_info {
	char namespace[128];
	char output[64];
	char layer[16];	   /* background|bottom|top|overlay */
	char keyboard[16]; /* none|exclusive|on_demand */
	int32_t x;
	int32_t y;
	int32_t w;
	int32_t h;
	int32_t exclusive_zone;
	uint32_t anchor;
	bool mapped;
	bool focused;
};

/* Bottom-most first (background → overlay, list order). Returns count ≤ cap. */
size_t sf_layers_collect(struct sf_server *server, struct sf_layer_info *out, size_t cap);

int sf_layers_to_json(const struct sf_layer_info *infos, size_t n, char *buf, size_t cap);

bool sf_layers_from_json(const char *json, struct sf_layer_info *out, size_t cap, size_t *n_out);

void sf_layers_print(const struct sf_layer_info *infos, size_t n, FILE *fp);

#endif
