#include "ipc/dump.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wayland-util.h>
#include <wlr/types/wlr_scene.h>

#include "config/config.h"
#include "ipc/outputs.h"
#include "ipc/windows.h"
#include "ipc/workspaces.h"
#include "server/server.h"
#include "toplevel/toplevel.h"
#include "util/json.h"
#include "wm/arrange.h"
#include "wm/arrange_math.h"
#include "wm/rules.h"
#include "wm/wm.h"
#include "wm/workspace.h"

#define SF_DUMP_MAX_WINDOWS 256
#define SF_DUMP_MAX_OUTPUTS 32
#define SF_DUMP_MAX_WORKSPACES 128

static void emit_bool(struct json_buf *b, const char *key, bool v) {
	json_byte(b, ',');
	json_key(b, key);
	json_bool(b, v);
}

static void emit_i64(struct json_buf *b, const char *key, int64_t v) {
	json_byte(b, ',');
	json_key(b, key);
	json_i64(b, v);
}

static void emit_str(struct json_buf *b, const char *key, const char *v) {
	json_byte(b, ',');
	json_key(b, key);
	json_str(b, v != NULL ? v : "");
}

static size_t blur_plate_count(const struct sf_toplevel *toplevel) {
	return (size_t)wl_list_length(&toplevel->blur_plates);
}

static void emit_window_debug(struct json_buf *b, struct sf_toplevel *toplevel) {
	json_byte(b, ',');
	json_key(b, "debug");
	json_byte(b, '{');

	json_key(b, "resting_x");
	json_i64(b, toplevel->resting_x);
	emit_i64(b, "resting_y", toplevel->resting_y);
	emit_i64(b, "arrange_prop_w", toplevel->arrange_prop_w);
	emit_i64(b, "arrange_prop_h", toplevel->arrange_prop_h);
	emit_i64(b, "arrange_prop_waits", toplevel->arrange_prop_waits);
	emit_bool(b, "arrange_slot_saved", toplevel->arrange_slot_saved);
	emit_i64(b, "arrange_slot_col", toplevel->arrange_slot_col);
	emit_i64(b, "arrange_slot_row", toplevel->arrange_slot_row);
	emit_bool(b, "arrange_slot_full", toplevel->arrange_slot_full);
	emit_bool(b, "arrange_max_edges_rejoin", toplevel->arrange_max_edges_rejoin);
	emit_bool(b, "arrange_appear_pending", toplevel->arrange_appear_pending);
	emit_bool(b, "arrange_deferred", toplevel->arrange_deferred);
	emit_bool(b, "geom_regain_hold", toplevel->geom_regain_hold);
	emit_bool(b, "want_ssd", toplevel->want_ssd);
	emit_i64(b, "ssd_set_mode", toplevel->ssd_set_mode);
	emit_bool(b, "center_still_possible", sf_rules_center_still_possible(toplevel));
	emit_bool(b, "float_still_possible", sf_rules_float_still_possible(toplevel));
	emit_i64(b, "blur_plate_count", (int64_t)blur_plate_count(toplevel));

	json_byte(b, '}');
}

static void emit_scratchpads(struct json_buf *b, struct sf_server *server) {
	const struct sf_config *cfg = sf_cfg(server);

	json_key(b, "scratchpads");
	json_byte(b, '[');
	bool first = true;
	for (size_t i = 0; i < cfg->n_scratchpads; i++) {
		const char *name = cfg->scratchpads[i].name;
		if (name == NULL || name[0] == '\0') {
			continue;
		}
		struct sf_toplevel *found = NULL;
		struct sf_toplevel *toplevel;
		wl_list_for_each(toplevel, &server->toplevels, link) {
			if (strcmp(toplevel->scratchpad, name) == 0) {
				found = toplevel;
				break;
			}
		}
		if (!first) {
			json_byte(b, ',');
		}
		first = false;
		json_byte(b, '{');
		json_key(b, "name");
		json_str(b, name);
		emit_bool(b, "auto_hide", cfg->scratchpads[i].auto_hide);
		if (found != NULL) {
			char idbuf[32];
			snprintf(idbuf, sizeof(idbuf), "%llu", (unsigned long long)found->id);
			emit_str(b, "window_id", idbuf);
			bool vis = found->scene_tree != NULL && found->scene_tree->node.enabled;
			emit_bool(b, "visible", vis);
		} else {
			emit_bool(b, "visible", false);
		}
		json_byte(b, '}');
	}
	json_byte(b, ']');
}

int sf_dump_to_json(struct sf_server *server, char *buf, size_t cap) {
	if (server == NULL || buf == NULL || cap == 0) {
		return -1;
	}

	struct sf_window_info wins[SF_DUMP_MAX_WINDOWS];
	struct sf_output_info outs[SF_DUMP_MAX_OUTPUTS];
	struct sf_workspace_info wss[SF_DUMP_MAX_WORKSPACES];
	struct sf_toplevel *focused = sf_focused_toplevel(server);

	size_t n_wins = 0;
	struct sf_toplevel *toplevel;
	wl_list_for_each_reverse(toplevel, &server->toplevels, link) {
		if (n_wins >= SF_DUMP_MAX_WINDOWS) {
			break;
		}
		sf_windows_fill_info(toplevel, focused, &wins[n_wins]);
		n_wins++;
	}
	size_t n_outs = sf_outputs_collect(server, outs, SF_DUMP_MAX_OUTPUTS);
	size_t n_wss = sf_workspaces_collect(server, wss, SF_DUMP_MAX_WORKSPACES);

	enum sf_arrange arr = sf_cfg(server)->arrange;
	if (focused != NULL && focused->workspace != NULL) {
		arr = sf_workspace_arrange(focused->workspace);
	}

	struct json_buf b;
	json_buf_init(&b, buf, cap);
	json_byte(&b, '{');

	json_key(&b, "version");
	json_i64(&b, 1);

	emit_str(&b, "arrange", sf_arrange_name(arr));

	json_byte(&b, ',');
	json_key(&b, "focused_id");
	if (focused != NULL) {
		char idbuf[32];
		snprintf(idbuf, sizeof(idbuf), "%llu", (unsigned long long)focused->id);
		json_str(&b, idbuf);
	} else {
		json_str(&b, "");
	}

	json_byte(&b, ',');
	json_key(&b, "windows");
	json_byte(&b, '[');
	size_t wi = 0;
	wl_list_for_each_reverse(toplevel, &server->toplevels, link) {
		if (wi >= n_wins) {
			break;
		}
		if (wi > 0) {
			json_byte(&b, ',');
		}
		/* Open object via shared encoder, then splice debug before close.
		 * Re-encode: write object without closing — use fill buffer trick:
		 * encode to temp is heavy; instead emit fields manually after
		 * truncating the closing brace from sf_windows_info_to_json_object.
		 * Simpler: encode object, then we can't add debug. So emit base
		 * object and replace final '}' with debug + '}'. */
		size_t mark = b.len;
		sf_windows_info_to_json_object(&b, &wins[wi]);
		if (json_ok(&b) && b.len > mark && b.data[b.len - 1] == '}') {
			b.len--;
			emit_window_debug(&b, toplevel);
			json_byte(&b, '}');
		}
		wi++;
	}
	json_byte(&b, ']');

	json_byte(&b, ',');
	json_key(&b, "workspaces");
	/* Reuse workspace array encoder into a side buffer is awkward; inline
	 * by calling to_json into a stack chunk only if small — prefer nested
	 * write of collected infos via the public array helper into a temp. */
	{
		char *tmp = malloc(cap);
		if (tmp == NULL) {
			b.err = true;
		} else if (sf_workspaces_to_json(wss, n_wss, tmp, cap) < 0) {
			b.err = true;
			free(tmp);
		} else {
			json_raw(&b, tmp);
			free(tmp);
		}
	}

	json_byte(&b, ',');
	json_key(&b, "outputs");
	{
		char *tmp = malloc(cap);
		if (tmp == NULL) {
			b.err = true;
		} else if (sf_outputs_to_json(outs, n_outs, tmp, cap) < 0) {
			b.err = true;
			free(tmp);
		} else {
			json_raw(&b, tmp);
			free(tmp);
		}
	}

	json_byte(&b, ',');
	emit_scratchpads(&b, server);

	json_byte(&b, '}');
	json_finish(&b);

	if (!json_ok(&b)) {
		return -1;
	}
	return (int)b.len;
}
