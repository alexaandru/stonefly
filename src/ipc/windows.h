#ifndef STONEFLY_IPC_WINDOWS_H
#define STONEFLY_IPC_WINDOWS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

struct stonefly_server;

/* Point-in-time window description (peeble WindowInfo subset). */
struct stonefly_window_info {
	uint64_t id;
	char app_id[128];
	char title[256];
	int32_t x;
	int32_t y;
	int32_t w;
	int32_t h;
	bool focused;
	bool floating;
	bool maximized_to_edges;
	bool minimized;
	bool snapped;
	char snap_dir[16]; /* empty when not snapped */
};

/* Bottom-most first. Returns how many entries were written (≤ cap). */
size_t stonefly_windows_collect(struct stonefly_server *server,
		struct stonefly_window_info *out, size_t cap);

/* Encode as a JSON array. Returns bytes written (excl. NUL), or -1. */
int stonefly_windows_to_json(const struct stonefly_window_info *infos,
		size_t n, char *buf, size_t cap);

/* Decode a JSON array produced by stonefly_windows_to_json. */
bool stonefly_windows_from_json(const char *json,
		struct stonefly_window_info *out, size_t cap, size_t *n_out);

void stonefly_windows_print(const struct stonefly_window_info *infos,
		size_t n, FILE *fp);

#endif
