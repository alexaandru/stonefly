#ifndef SF_IPC_OUTPUTS_H
#define SF_IPC_OUTPUTS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

struct sf_server;

/* Point-in-time output description (peeble OutputInfo subset). */
struct sf_output_info {
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

size_t sf_outputs_collect(struct sf_server *server, struct sf_output_info *out, size_t cap);

int sf_outputs_to_json(const struct sf_output_info *infos, size_t n, char *buf, size_t cap);

bool sf_outputs_from_json(const char *json, struct sf_output_info *out, size_t cap, size_t *n_out);

void sf_outputs_print(const struct sf_output_info *infos, size_t n, FILE *fp);

#endif
