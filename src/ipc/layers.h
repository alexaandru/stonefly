#ifndef STONEFLY_IPC_LAYERS_H
#define STONEFLY_IPC_LAYERS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

struct stonefly_server;

/* Point-in-time layer-shell surface description (debug aid). */
struct stonefly_layer_info {
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
size_t stonefly_layers_collect(struct stonefly_server *server, struct stonefly_layer_info *out,
			       size_t cap);

int stonefly_layers_to_json(const struct stonefly_layer_info *infos, size_t n, char *buf,
			    size_t cap);

bool stonefly_layers_from_json(const char *json, struct stonefly_layer_info *out, size_t cap,
			       size_t *n_out);

void stonefly_layers_print(const struct stonefly_layer_info *infos, size_t n, FILE *fp);

#endif
