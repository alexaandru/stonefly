#ifndef STONEFLY_IPC_OUTPUTS_H
#define STONEFLY_IPC_OUTPUTS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

struct stonefly_server;

/* Point-in-time output description (peeble OutputInfo subset). */
struct stonefly_output_info {
	char name[64];
	bool enabled;
	int32_t x;
	int32_t y;
	int32_t w; /* mode width (px) */
	int32_t h;
	int32_t logical_w;
	int32_t logical_h;
	double scale;
	int32_t usable_x;
	int32_t usable_y;
	int32_t usable_w;
	int32_t usable_h;
	bool focused;
};

size_t stonefly_outputs_collect(struct stonefly_server *server, struct stonefly_output_info *out,
				size_t cap);

int stonefly_outputs_to_json(const struct stonefly_output_info *infos, size_t n, char *buf,
			     size_t cap);

bool stonefly_outputs_from_json(const char *json, struct stonefly_output_info *out, size_t cap,
				size_t *n_out);

void stonefly_outputs_print(const struct stonefly_output_info *infos, size_t n, FILE *fp);

#endif
