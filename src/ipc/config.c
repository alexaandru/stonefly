#include "ipc/config.h"

#include <stdio.h>
#include <string.h>
#include <xkbcommon/xkbcommon.h>

#include "config/config.h"
#include "server/server.h"
#include "ssd/ssd_policy.h"
#include "util/json.h"
#include "wm/workspace.h"

static const char *action_name(enum stonefly_action a) {
	switch (a) {
	case STONEFLY_ACTION_NONE:
		return "none";
	case STONEFLY_ACTION_QUIT:
		return "quit";
	case STONEFLY_ACTION_CLOSE:
		return "close";
	case STONEFLY_ACTION_FOCUS_NEXT:
		return "focus_next";
	case STONEFLY_ACTION_FOCUS_PREV:
		return "focus_prev";
	case STONEFLY_ACTION_FOCUS_NEXT_ALL:
		return "focus_next_all";
	case STONEFLY_ACTION_FOCUS_PREV_ALL:
		return "focus_prev_all";
	case STONEFLY_ACTION_TOGGLE_MAX_EDGES:
		return "toggle_max_edges";
	case STONEFLY_ACTION_TOGGLE_MAX_GAPS:
		return "toggle_max_gaps";
	case STONEFLY_ACTION_MINIMIZE:
		return "minimize";
	case STONEFLY_ACTION_UNMINIMIZE:
		return "unminimize";
	case STONEFLY_ACTION_SNAP_LEFT:
		return "snap_left";
	case STONEFLY_ACTION_SNAP_RIGHT:
		return "snap_right";
	case STONEFLY_ACTION_SNAP_TOP:
		return "snap_top";
	case STONEFLY_ACTION_SNAP_BOTTOM:
		return "snap_bottom";
	case STONEFLY_ACTION_UNSNAP:
		return "unsnap";
	case STONEFLY_ACTION_SPAWN_FOOT:
		return "spawn_foot";
	case STONEFLY_ACTION_SPAWN_NAUTILUS:
		return "spawn_nautilus";
	case STONEFLY_ACTION_SPAWN_GHOSTTY:
		return "spawn_ghostty";
	case STONEFLY_ACTION_SPAWN_GHOSTTY_QT:
		return "spawn_ghostty_qt";
	case STONEFLY_ACTION_SPAWN_EDITOR:
		return "spawn_editor";
	case STONEFLY_ACTION_SPAWN_LAUNCHER:
		return "spawn_launcher";
	case STONEFLY_ACTION_SCRATCH_TOGGLE_TERM:
		return "scratch_toggle_term";
	case STONEFLY_ACTION_SCRATCH_CLAIM_TERM:
		return "scratch_claim_term";
	case STONEFLY_ACTION_EXEC:
		return "exec";
	case STONEFLY_ACTION_SCRATCH_TOGGLE:
		return "scratch_toggle";
	case STONEFLY_ACTION_SCRATCH_CLAIM:
		return "scratch_claim";
	case STONEFLY_ACTION_WORKSPACE_NEXT:
		return "workspace_next";
	case STONEFLY_ACTION_WORKSPACE_PREV:
		return "workspace_prev";
	case STONEFLY_ACTION_WORKSPACE_SWITCH:
		return "workspace_switch";
	case STONEFLY_ACTION_MOVE_TO_WORKSPACE_NEXT:
		return "move_to_workspace_next";
	case STONEFLY_ACTION_MOVE_TO_WORKSPACE_PREV:
		return "move_to_workspace_prev";
	case STONEFLY_ACTION_MOVE_TO_WORKSPACE:
		return "move_to_workspace";
	}
	return "unknown";
}

static void json_rgba(struct json_buf *b, const float c[4]) {
	json_byte(b, '[');
	for (int i = 0; i < 4; i++) {
		if (i > 0) {
			json_byte(b, ',');
		}
		json_f64(b, c[i]);
	}
	json_byte(b, ']');
}

static void json_mods(struct json_buf *b, uint32_t mods) {
	json_byte(b, '[');
	bool first = true;
	struct {
		uint32_t bit;
		const char *name;
	} tab[] = {
		{STONEFLY_SUPER, "super"},
		{STONEFLY_SHIFT, "shift"},
		{STONEFLY_CTRL, "ctrl"},
		{STONEFLY_ALT, "alt"},
	};
	for (size_t i = 0; i < sizeof tab / sizeof tab[0]; i++) {
		if ((mods & tab[i].bit) == 0) {
			continue;
		}
		if (!first) {
			json_byte(b, ',');
		}
		first = false;
		json_str(b, tab[i].name);
	}
	json_byte(b, ']');
}

static void json_keysym(struct json_buf *b, xkb_keysym_t sym) {
	char name[64];
	if (sym == 0 || xkb_keysym_get_name(sym, name, sizeof name) < 0) {
		json_str(b, "");
		return;
	}
	json_str(b, name);
}

static void emit_options(struct json_buf *b, const struct stonefly_config *cfg) {
	json_byte(b, '{');

	json_key(b, "scale");
	json_f64(b, cfg->output_scale);
	json_byte(b, ',');
	json_key(b, "cursor_size");
	json_i64(b, cfg->cursor_size_default);
	json_byte(b, ',');
	json_key(b, "gap");
	json_i64(b, cfg->gap_size);
	json_byte(b, ',');
	json_key(b, "cascade_step");
	json_i64(b, cfg->cascade_step);
	json_byte(b, ',');
	json_key(b, "cascade_wrap");
	json_i64(b, cfg->cascade_wrap);
	json_byte(b, ',');
	json_key(b, "slide_duration_ms");
	json_i64(b, cfg->slide_duration_ms);
	json_byte(b, ',');
	json_key(b, "slide_dir_closest");
	json_bool(b, cfg->slide_dir_closest != 0);
	json_byte(b, ',');
	json_key(b, "morph_duration_ms");
	json_i64(b, cfg->morph_duration_ms);
	json_byte(b, ',');
	json_key(b, "client_opacity");
	json_f64(b, cfg->client_opacity);
	json_byte(b, ',');
	json_key(b, "inactive_opacity");
	json_f64(b, cfg->inactive_opacity);
	json_byte(b, ',');
	json_key(b, "corner_radius");
	json_i64(b, cfg->corner_radius);
	json_byte(b, ',');
	json_key(b, "border_thickness");
	json_i64(b, cfg->border_thickness);
	json_byte(b, ',');
	json_key(b, "shadow_blur_sigma");
	json_f64(b, cfg->shadow_blur_sigma);
	json_byte(b, ',');
	json_key(b, "border_active");
	json_rgba(b, cfg->border_active);
	json_byte(b, ',');
	json_key(b, "border_inactive");
	json_rgba(b, cfg->border_inactive);
	json_byte(b, ',');
	json_key(b, "border_urgent");
	json_rgba(b, cfg->border_urgent);
	json_byte(b, ',');
	json_key(b, "shadow_color");
	json_rgba(b, cfg->shadow_color);
	json_byte(b, ',');
	json_key(b, "window_blur");
	json_bool(b, cfg->window_blur != 0);
	json_byte(b, ',');
	json_key(b, "blur_xray");
	json_bool(b, cfg->blur_xray != 0);
	json_byte(b, ',');
	json_key(b, "blur_num_passes");
	json_i64(b, cfg->blur_num_passes);
	json_byte(b, ',');
	json_key(b, "blur_radius");
	json_i64(b, cfg->blur_radius);
	json_byte(b, ',');
	json_key(b, "blur_noise");
	json_f64(b, cfg->blur_noise);
	json_byte(b, ',');
	json_key(b, "blur_brightness");
	json_f64(b, cfg->blur_brightness);
	json_byte(b, ',');
	json_key(b, "blur_contrast");
	json_f64(b, cfg->blur_contrast);
	json_byte(b, ',');
	json_key(b, "blur_saturation");
	json_f64(b, cfg->blur_saturation);
	json_byte(b, ',');
	json_key(b, "bell_flash_ms");
	json_i64(b, cfg->bell_flash_ms);
	json_byte(b, ',');
	json_key(b, "snap_threshold");
	json_i64(b, cfg->snap_threshold);
	json_byte(b, ',');
	json_key(b, "snap_preview_border");
	json_i64(b, cfg->snap_preview_border);
	json_byte(b, ',');
	json_key(b, "snap_preview_opacity");
	json_f64(b, cfg->snap_preview_opacity);
	json_byte(b, ',');
	json_key(b, "tap_max_hold_ms");
	json_i64(b, cfg->tap_max_hold_ms);
	json_byte(b, ',');
	json_key(b, "double_tap_gap_ms");
	json_i64(b, cfg->double_tap_gap_ms);
	json_byte(b, ',');
	json_key(b, "layout");
	json_str(b, stonefly_layout_name((enum stonefly_layout)cfg->layout));
	json_byte(b, ',');
	json_key(b, "swipe_workspace_threshold");
	json_i64(b, cfg->swipe_workspace_threshold);
	json_byte(b, ',');
	json_key(b, "swipe_workspace_natural");
	json_bool(b, cfg->swipe_workspace_natural != 0);
	json_byte(b, ',');
	json_key(b, "ssd_mode");
	json_str(b, stonefly_ssd_mode_name((enum stonefly_ssd_mode)cfg->ssd_mode));
	json_byte(b, ',');
	json_key(b, "resize_border_in");
	json_i64(b, cfg->resize_border_in);
	json_byte(b, ',');
	json_key(b, "resize_border_out");
	json_i64(b, cfg->resize_border_out);
	json_byte(b, ',');
	json_key(b, "ssd_height");
	json_i64(b, cfg->ssd_height);
	json_byte(b, ',');
	json_key(b, "ssd_font_size");
	json_i64(b, cfg->ssd_font_size);
	json_byte(b, ',');
	json_key(b, "capture_block_app_ids");
	json_str(b, cfg->capture_block_app_ids ? cfg->capture_block_app_ids : "");

	json_byte(b, '}');
}

static void emit_cmds(struct json_buf *b, const struct stonefly_config *cfg) {
	json_byte(b, '{');
	json_key(b, "foot");
	json_str(b, cfg->cmd_foot ? cfg->cmd_foot : "");
	json_byte(b, ',');
	json_key(b, "nautilus");
	json_str(b, cfg->cmd_nautilus ? cfg->cmd_nautilus : "");
	json_byte(b, ',');
	json_key(b, "ghostty");
	json_str(b, cfg->cmd_ghostty ? cfg->cmd_ghostty : "");
	json_byte(b, ',');
	json_key(b, "ghostty_qt");
	json_str(b, cfg->cmd_ghostty_qt ? cfg->cmd_ghostty_qt : "");
	json_byte(b, ',');
	json_key(b, "editor");
	json_str(b, cfg->cmd_editor ? cfg->cmd_editor : "");
	json_byte(b, ',');
	json_key(b, "launcher");
	json_str(b, cfg->cmd_launcher ? cfg->cmd_launcher : "");
	json_byte(b, '}');
}

static void emit_binds(struct json_buf *b, const struct stonefly_config *cfg) {
	json_byte(b, '[');
	for (size_t i = 0; i < cfg->n_keybinds; i++) {
		const struct stonefly_keybind *k = &cfg->keybinds[i];
		if (i > 0) {
			json_byte(b, ',');
		}
		json_byte(b, '{');
		json_key(b, "mods");
		json_mods(b, k->mods);
		json_byte(b, ',');
		json_key(b, "key");
		json_keysym(b, k->sym);
		if (k->sym_alt != 0) {
			json_byte(b, ',');
			json_key(b, "key_alt");
			json_keysym(b, k->sym_alt);
		}
		json_byte(b, ',');
		json_key(b, "action");
		json_str(b, action_name(k->action));
		if (k->cmd != NULL && k->cmd[0] != '\0') {
			json_byte(b, ',');
			json_key(b, "cmd");
			json_str(b, k->cmd);
		}
		json_byte(b, '}');
	}
	json_byte(b, ']');
}

static void emit_taps(struct json_buf *b, const struct stonefly_config *cfg) {
	json_byte(b, '[');
	for (size_t i = 0; i < cfg->n_tapbinds; i++) {
		const struct stonefly_tapbind *t = &cfg->tapbinds[i];
		if (i > 0) {
			json_byte(b, ',');
		}
		json_byte(b, '{');
		json_key(b, "key");
		json_keysym(b, t->sym);
		if (t->sym_alt != 0) {
			json_byte(b, ',');
			json_key(b, "key_alt");
			json_keysym(b, t->sym_alt);
		}
		json_byte(b, ',');
		json_key(b, "single");
		json_str(b, action_name(t->single));
		if (t->single_cmd != NULL && t->single_cmd[0] != '\0') {
			json_byte(b, ',');
			json_key(b, "single_cmd");
			json_str(b, t->single_cmd);
		}
		if (t->double_tap != STONEFLY_ACTION_NONE) {
			json_byte(b, ',');
			json_key(b, "double");
			json_str(b, action_name(t->double_tap));
			if (t->double_cmd != NULL && t->double_cmd[0] != '\0') {
				json_byte(b, ',');
				json_key(b, "double_cmd");
				json_str(b, t->double_cmd);
			}
		}
		json_byte(b, '}');
	}
	json_byte(b, ']');
}

static void emit_scratchpads(struct json_buf *b, const struct stonefly_config *cfg) {
	json_byte(b, '[');
	for (size_t i = 0; i < cfg->n_scratchpads; i++) {
		const struct stonefly_scratchpad_spec *s = &cfg->scratchpads[i];
		if (i > 0) {
			json_byte(b, ',');
		}
		json_byte(b, '{');
		json_key(b, "name");
		json_str(b, s->name ? s->name : "");
		json_byte(b, ',');
		json_key(b, "cmd");
		json_str(b, s->cmd ? s->cmd : "");
		json_byte(b, ',');
		json_key(b, "app_id");
		json_str(b, s->app_id ? s->app_id : "");
		json_byte(b, ',');
		json_key(b, "auto_hide");
		json_bool(b, s->auto_hide);
		json_byte(b, '}');
	}
	json_byte(b, ']');
}

static bool section_ok(const char *section) {
	return section == NULL || section[0] == '\0' || strcmp(section, "options") == 0 ||
	       strcmp(section, "binds") == 0 || strcmp(section, "taps") == 0 ||
	       strcmp(section, "scratchpads") == 0 || strcmp(section, "cmds") == 0 ||
	       strcmp(section, "inputs") == 0 || strcmp(section, "outputs") == 0;
}

int stonefly_config_to_json(struct stonefly_server *server, const char *section, char *buf,
			    size_t cap) {
	if (server == NULL || buf == NULL || cap == 0 || !section_ok(section)) {
		return -1;
	}
	const struct stonefly_config *cfg = stonefly_cfg(server);
	struct json_buf b;
	json_buf_init(&b, buf, cap);

	bool all = section == NULL || section[0] == '\0';

	if (all) {
		json_byte(&b, '{');
	}

	bool need_comma = false;
	if (all || strcmp(section, "options") == 0) {
		if (all) {
			json_key(&b, "options");
		}
		emit_options(&b, cfg);
		need_comma = true;
	}
	if (all || strcmp(section, "binds") == 0) {
		if (all) {
			if (need_comma) {
				json_byte(&b, ',');
			}
			json_key(&b, "binds");
		}
		emit_binds(&b, cfg);
		need_comma = true;
	}
	if (all || strcmp(section, "taps") == 0) {
		if (all) {
			if (need_comma) {
				json_byte(&b, ',');
			}
			json_key(&b, "taps");
		}
		emit_taps(&b, cfg);
		need_comma = true;
	}
	if (all || strcmp(section, "scratchpads") == 0) {
		if (all) {
			if (need_comma) {
				json_byte(&b, ',');
			}
			json_key(&b, "scratchpads");
		}
		emit_scratchpads(&b, cfg);
		need_comma = true;
	}
	if (all || strcmp(section, "cmds") == 0) {
		if (all) {
			if (need_comma) {
				json_byte(&b, ',');
			}
			json_key(&b, "cmds");
		}
		emit_cmds(&b, cfg);
		need_comma = true;
	}
	if (all || strcmp(section, "inputs") == 0) {
		if (all) {
			if (need_comma) {
				json_byte(&b, ',');
			}
			json_key(&b, "inputs");
		}
		json_raw(&b, "{}");
		need_comma = true;
	}
	if (all || strcmp(section, "outputs") == 0) {
		if (all) {
			if (need_comma) {
				json_byte(&b, ',');
			}
			json_key(&b, "outputs");
		}
		json_raw(&b, "{}");
	}

	if (all) {
		json_byte(&b, '}');
	}
	json_finish(&b);
	if (!json_ok(&b)) {
		return -1;
	}
	return (int)b.len;
}

/* Indenting pretty-printer for the compact JSON we emit (no nested strings with {). */
void stonefly_config_print_json(const char *json, FILE *fp) {
	if (json == NULL || fp == NULL) {
		return;
	}
	int indent = 0;
	bool in_str = false;
	bool esc = false;
	for (const char *p = json; *p != '\0'; p++) {
		char c = *p;
		if (in_str) {
			fputc(c, fp);
			if (esc) {
				esc = false;
			} else if (c == '\\') {
				esc = true;
			} else if (c == '"') {
				in_str = false;
			}
			continue;
		}
		switch (c) {
		case '"':
			in_str = true;
			fputc(c, fp);
			break;
		case '{':
		case '[':
			fputc(c, fp);
			fputc('\n', fp);
			indent++;
			for (int i = 0; i < indent; i++) {
				fputs("  ", fp);
			}
			break;
		case '}':
		case ']':
			fputc('\n', fp);
			indent--;
			if (indent < 0) {
				indent = 0;
			}
			for (int i = 0; i < indent; i++) {
				fputs("  ", fp);
			}
			fputc(c, fp);
			break;
		case ',':
			fputc(c, fp);
			fputc('\n', fp);
			for (int i = 0; i < indent; i++) {
				fputs("  ", fp);
			}
			break;
		case ':':
			fputs(": ", fp);
			break;
		default:
			fputc(c, fp);
			break;
		}
	}
	fputc('\n', fp);
}
