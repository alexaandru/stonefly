#ifndef SF_IPC_WINDOWS_H
#define SF_IPC_WINDOWS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

struct sf_server;

/* Point-in-time window description (peeble WindowInfo subset). */
struct sf_window_info {
	uint64_t id;
	char app_id[128];
	char title[256];
	char tag[128];	       /* xdg-toplevel-tag; empty when unset */
	char description[256]; /* translated tag description; empty when unset */
	char icon_name[128];   /* xdg-toplevel-icon name; empty when unset */
	int32_t x;
	int32_t y;
	int32_t w;
	int32_t h;
	bool focused;
	bool floating;
	bool maximized_to_edges;
	bool minimized;
	bool snapped;
	bool urgent;
	bool capture_blocked;
	bool always_on_top;
	char snap_dir[16]; /* empty when not snapped */
	char output[64];
	int workspace; /* 1-based; 0 when unset/scratchpad */
};

/* Bottom-most first. Returns how many entries were written (≤ cap). */
size_t sf_windows_collect(struct sf_server *server, struct sf_window_info *out, size_t cap);

/* Encode as a JSON array. Returns bytes written (excl. NUL), or -1. */
int sf_windows_to_json(const struct sf_window_info *infos, size_t n, char *buf, size_t cap);

/* Decode a JSON array produced by sf_windows_to_json. */
bool sf_windows_from_json(const char *json, struct sf_window_info *out, size_t cap, size_t *n_out);

void sf_windows_print(const struct sf_window_info *infos, size_t n, FILE *fp);

#endif
