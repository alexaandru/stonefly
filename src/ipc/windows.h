#ifndef SF_IPC_WINDOWS_H
#define SF_IPC_WINDOWS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

struct sf_server;
struct sf_toplevel;

/* Point-in-time window description (IPC `windows` + dump base). */
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
	/* layout_sm float mode (not sticky-float membership). */
	bool floating;
	bool maximized_to_edges;
	bool maximized_gaps;
	bool fullscreen;
	bool minimized;
	bool snapped;
	bool urgent;
	bool capture_blocked;
	bool always_on_top;
	bool scene_enabled; /* false while SoftPlace/appear parks the node */
	bool arrange_floating;
	bool arrange_managed;
	bool soft;	  /* SoftPlace still provisional */
	bool appear_hold; /* scene parked for SoftPlace */
	bool modal;
	bool borderless;
	bool xwayland;
	bool client_blur;
	char layout[16];     /* float|max_edges|max_gaps|snap|fullscreen */
	char snap_dir[16];   /* empty when not snapped */
	char scratchpad[32]; /* empty when not on a pad */
	char output[64];
	int workspace;	/* 1-based; 0 when unset/scratchpad */
	int column;	/* 0-based strip index; -1 when none */
	int column_row; /* 0-based row in column; -1 when none */
};

/* Fill one info from a live toplevel. focused may be NULL. */
void sf_windows_fill_info(struct sf_toplevel *toplevel, struct sf_toplevel *focused,
			  struct sf_window_info *out);

/* Bottom-most first. Returns how many entries were written (≤ cap). */
size_t sf_windows_collect(struct sf_server *server, struct sf_window_info *out, size_t cap);

struct json_buf;

/* Encode one window object into an open json_buf (no surrounding array). */
void sf_windows_info_to_json_object(struct json_buf *b, const struct sf_window_info *w);

/* Encode as a JSON array. Returns bytes written (excl. NUL), or -1. */
int sf_windows_to_json(const struct sf_window_info *infos, size_t n, char *buf, size_t cap);

/* Decode a JSON array produced by sf_windows_to_json. */
bool sf_windows_from_json(const char *json, struct sf_window_info *out, size_t cap, size_t *n_out);

void sf_windows_print(const struct sf_window_info *infos, size_t n, FILE *fp);

#endif
