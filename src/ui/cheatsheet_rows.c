#include "ui/cheatsheet_rows.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <xkbcommon/xkbcommon.h>

#include "config/action.h"

const char *sf_cheatsheet_group_title(enum sf_cheatsheet_group group) {
	switch (group) {
	case SF_CS_GROUP_APPS:
		return "Apps";
	case SF_CS_GROUP_FOCUS:
		return "Focus";
	case SF_CS_GROUP_MOVE:
		return "Move & size";
	case SF_CS_GROUP_WINDOWS:
		return "Windows";
	case SF_CS_GROUP_SCRATCH:
		return "Scratchpad";
	case SF_CS_GROUP_WORKSPACES:
		return "Workspaces";
	case SF_CS_GROUP_OVERVIEW:
		return "Overview";
	case SF_CS_GROUP_SYSTEM:
		return "System";
	default:
		return "Other";
	}
}

enum sf_cheatsheet_group sf_cheatsheet_group_for_action(enum sf_action action) {
	switch (action) {
	case SF_ACTION_EXEC:
		return SF_CS_GROUP_APPS;
	case SF_ACTION_FOCUS_NEXT:
	case SF_ACTION_FOCUS_PREV:
	case SF_ACTION_FOCUS_NEXT_ALL:
	case SF_ACTION_FOCUS_PREV_ALL:
	case SF_ACTION_FOCUS_DIR:
	case SF_ACTION_FOCUS_MONITOR:
	case SF_ACTION_POINTER_CENTER:
		return SF_CS_GROUP_FOCUS;
	case SF_ACTION_NUDGE:
	case SF_ACTION_RESIZE_EDGE:
	case SF_ACTION_CONSUME:
	case SF_ACTION_CONSUME_FROM:
	case SF_ACTION_SEND_TO_MONITOR:
	case SF_ACTION_MOVE_WINDOW:
	case SF_ACTION_RESIZE_WINDOW:
	case SF_ACTION_RESIZE_CENTER:
	case SF_ACTION_SNAP_LEFT:
	case SF_ACTION_SNAP_RIGHT:
	case SF_ACTION_SNAP_TOP:
	case SF_ACTION_SNAP_BOTTOM:
	case SF_ACTION_UNSNAP:
		return SF_CS_GROUP_MOVE;
	case SF_ACTION_CLOSE:
	case SF_ACTION_MAXIMIZE:
	case SF_ACTION_MAXIMIZE_TO_EDGES:
	case SF_ACTION_UNMAXIMIZE:
	case SF_ACTION_TOGGLE_MAXIMIZE:
	case SF_ACTION_TOGGLE_MAXIMIZE_TO_EDGES:
	case SF_ACTION_MINIMIZE:
	case SF_ACTION_UNMINIMIZE:
	case SF_ACTION_TOGGLE_FLOAT:
	case SF_ACTION_FLOAT:
	case SF_ACTION_UNFLOAT:
	case SF_ACTION_TOGGLE_ALWAYS_ON_TOP:
	case SF_ACTION_ALWAYS_ON_TOP:
	case SF_ACTION_SET_FULLSCREEN:
	case SF_ACTION_UNSET_FULLSCREEN:
	case SF_ACTION_TOGGLE_FULLSCREEN:
	case SF_ACTION_ARRANGE_CYCLE:
	case SF_ACTION_ARRANGE_SET:
	case SF_ACTION_ARRANGE_ROTATE:
		return SF_CS_GROUP_WINDOWS;
	case SF_ACTION_SCRATCH_TOGGLE:
	case SF_ACTION_SCRATCH_CLAIM:
		return SF_CS_GROUP_SCRATCH;
	case SF_ACTION_WORKSPACE_NEXT:
	case SF_ACTION_WORKSPACE_PREV:
	case SF_ACTION_WORKSPACE_SWITCH:
	case SF_ACTION_MOVE_TO_WORKSPACE_NEXT:
	case SF_ACTION_MOVE_TO_WORKSPACE_PREV:
	case SF_ACTION_MOVE_TO_WORKSPACE:
		return SF_CS_GROUP_WORKSPACES;
	case SF_ACTION_OVERVIEW_TOGGLE:
	case SF_ACTION_OVERVIEW_OPEN:
	case SF_ACTION_OVERVIEW_CLOSE:
	case SF_ACTION_OVERVIEW_CONFIRM:
	case SF_ACTION_OVERVIEW_SCROLL:
		return SF_CS_GROUP_OVERVIEW;
	case SF_ACTION_QUIT:
	case SF_ACTION_RELOAD:
	case SF_ACTION_CHEATSHEET_TOGGLE:
	case SF_ACTION_CHEATSHEET_OPEN:
	case SF_ACTION_CHEATSHEET_CLOSE:
	default:
		return SF_CS_GROUP_SYSTEM;
	}
}

const enum sf_cheatsheet_group *sf_cheatsheet_group_order(size_t *out_n) {
	static const enum sf_cheatsheet_group order[] = {
		SF_CS_GROUP_APPS,     SF_CS_GROUP_FOCUS,   SF_CS_GROUP_MOVE,
		SF_CS_GROUP_WINDOWS,  SF_CS_GROUP_SCRATCH, SF_CS_GROUP_WORKSPACES,
		SF_CS_GROUP_OVERVIEW, SF_CS_GROUP_SYSTEM,
	};
	if (out_n != NULL) {
		*out_n = sizeof order / sizeof order[0];
	}
	return order;
}

/* Write a display key name into out (always NUL-terminated). */
static void prettify_keysym(const char *raw, char *out, size_t out_len) {
	if (out == NULL || out_len == 0) {
		return;
	}
	out[0] = '\0';
	if (raw == NULL || raw[0] == '\0') {
		snprintf(out, out_len, "?");
		return;
	}
	if (raw[0] >= 'a' && raw[0] <= 'z' && raw[1] == '\0') {
		snprintf(out, out_len, "%c", (char)(raw[0] - 'a' + 'A'));
		return;
	}
	static const struct {
		const char *from;
		const char *to;
	} aliases[] = {
		{"Left", "←"},	      {"Right", "→"},	     {"Up", "↑"},
		{"Down", "↓"},	      {"comma", ","},	     {"period", "."},
		{"slash", "/"},	      {"minus", "-"},	     {"equal", "="},
		{"semicolon", ";"},   {"apostrophe", "'"},   {"grave", "`"},
		{"bracketleft", "["}, {"bracketright", "]"}, {"backslash", "\\"},
		{"space", "Space"},   {"Prior", "PgUp"},     {"Next", "PgDn"},
		{"Escape", "Esc"},    {"Return", "Enter"},
	};
	for (size_t i = 0; i < sizeof aliases / sizeof aliases[0]; i++) {
		if (strcmp(raw, aliases[i].from) == 0) {
			snprintf(out, out_len, "%s", aliases[i].to);
			return;
		}
	}
	snprintf(out, out_len, "%s", raw);
}

static void chord_push(char *buf, size_t buflen, size_t *used, const char *part) {
	if (buf == NULL || buflen == 0 || used == NULL || part == NULL) {
		return;
	}
	if (*used >= buflen) {
		buf[buflen - 1] = '\0';
		*used = buflen - 1;
		return;
	}
	size_t len = strlen(part);
	if (*used > 0 && *used + 1 < buflen) {
		buf[(*used)++] = '+';
		buf[*used] = '\0';
	}
	size_t space = buflen - *used - 1;
	if (len > space) {
		len = space;
	}
	if (len > 0) {
		memcpy(buf + *used, part, len);
		*used += len;
	}
	buf[*used] = '\0';
}

void sf_cheatsheet_format_chord(uint32_t mods, xkb_keysym_t sym, char *buf, size_t buflen) {
	if (buf == NULL || buflen == 0) {
		return;
	}
	buf[0] = '\0';
	size_t used = 0;
	if ((mods & SF_SUPER) != 0) {
		chord_push(buf, buflen, &used, "Super");
	}
	if ((mods & SF_CTRL) != 0) {
		chord_push(buf, buflen, &used, "Ctrl");
	}
	if ((mods & SF_ALT) != 0) {
		chord_push(buf, buflen, &used, "Alt");
	}
	if ((mods & SF_SHIFT) != 0) {
		chord_push(buf, buflen, &used, "Shift");
	}

	char raw[64];
	char pretty[32];
	xkb_keysym_get_name(sym, raw, sizeof raw);
	prettify_keysym(raw, pretty, sizeof pretty);
	chord_push(buf, buflen, &used, pretty);
}

static void truncate_label(char *dst, size_t dst_len, const char *src) {
	if (dst == NULL || dst_len == 0) {
		return;
	}
	if (src == NULL) {
		dst[0] = '\0';
		return;
	}
	size_t n = strlen(src);
	if (n + 1 <= dst_len) {
		memcpy(dst, src, n + 1);
		return;
	}
	if (dst_len < 2) {
		dst[0] = '\0';
		return;
	}
	/* Leave room for … (U+2026, 3 bytes) + NUL. */
	size_t keep = dst_len - 1 - 3;
	memcpy(dst, src, keep);
	dst[keep] = (char)0xe2;
	dst[keep + 1] = (char)0x80;
	dst[keep + 2] = (char)0xa6;
	dst[keep + 3] = '\0';
}

static const char *cmd_basename(const char *cmd) {
	if (cmd == NULL || cmd[0] == '\0') {
		return "";
	}
	const char *slash = strrchr(cmd, '/');
	const char *base = slash != NULL ? slash + 1 : cmd;
	return base;
}

static void make_label(enum sf_action action, const char *cmd, char *out, size_t out_len) {
	char tmp[160];
	tmp[0] = '\0';
	if (action == SF_ACTION_EXEC) {
		const char *base = cmd_basename(cmd);
		const char *space = cmd != NULL ? strchr(cmd, ' ') : NULL;
		if (space != NULL && space[1] != '\0') {
			/* Prefer "binary args" with path stripped from binary. */
			snprintf(tmp, sizeof tmp, "%s %s", base, space + 1);
		} else {
			snprintf(tmp, sizeof tmp, "%s", base[0] != '\0' ? base : "exec");
		}
	} else if (cmd != NULL && cmd[0] != '\0') {
		snprintf(tmp, sizeof tmp, "%s %s", sf_action_name(action), cmd);
	} else {
		snprintf(tmp, sizeof tmp, "%s", sf_action_name(action));
	}
	truncate_label(out, out_len, tmp);
}

struct raw_row {
	char chord[96];
	char label[80];
	char merge_key[160]; /* action\0cmd for strcmp — packed as "action\tcmd" */
	enum sf_action action;
	enum sf_cheatsheet_group group;
	uint32_t mods;
	xkb_keysym_t sym;
	char ws_cmd[8];
	bool drop;
};

static void fill_merge_key(char *key, size_t key_len, enum sf_action action, const char *cmd) {
	snprintf(key, key_len, "%d\t%s", (int)action, cmd != NULL ? cmd : "");
}

static bool cmd_is_digit_1_9(const char *cmd) {
	return cmd != NULL && cmd[0] >= '1' && cmd[0] <= '9' && cmd[1] == '\0';
}

static void collapse_workspace_runs(struct raw_row *rows, size_t n, enum sf_action ws_action) {
	bool mods_done[256];
	memset(mods_done, 0, sizeof mods_done);

	for (size_t i = 0; i < n; i++) {
		if (rows[i].drop || rows[i].action != ws_action) {
			continue;
		}
		if (!cmd_is_digit_1_9(rows[i].ws_cmd) || rows[i].sym < XKB_KEY_1 ||
		    rows[i].sym > XKB_KEY_9) {
			continue;
		}
		uint32_t mods = rows[i].mods;
		unsigned mods_slot = mods & 0xffu;
		if (mods_done[mods_slot]) {
			continue;
		}
		mods_done[mods_slot] = true;

		size_t found[9];
		memset(found, 0, sizeof found);
		size_t n_found = 0;
		for (size_t j = 0; j < n; j++) {
			if (rows[j].drop || rows[j].action != ws_action || rows[j].mods != mods) {
				continue;
			}
			if (!cmd_is_digit_1_9(rows[j].ws_cmd)) {
				continue;
			}
			if (rows[j].sym < XKB_KEY_1 || rows[j].sym > XKB_KEY_9) {
				continue;
			}
			int digit = (int)(rows[j].sym - XKB_KEY_1);
			int want = rows[j].ws_cmd[0] - '1';
			if (digit != want || found[digit] != 0) {
				continue;
			}
			found[digit] = j + 1;
			n_found++;
		}
		if (n_found < 9) {
			continue;
		}

		size_t keep = found[0] - 1;
		char chord[96];
		sf_cheatsheet_format_chord(mods, XKB_KEY_1, chord, sizeof chord);
		size_t clen = strlen(chord);
		if (clen > 0 && chord[clen - 1] == '1') {
			snprintf(chord + clen - 1, sizeof chord - (clen - 1), "1…9");
		}
		snprintf(rows[keep].chord, sizeof rows[keep].chord, "%s", chord);
		snprintf(rows[keep].label, sizeof rows[keep].label, "%s 1-9",
			 sf_action_name(ws_action));
		rows[keep].ws_cmd[0] = '\0';
		for (int d = 1; d < 9; d++) {
			rows[found[d] - 1].drop = true;
		}
	}
}

static int cmp_raw_group(const void *a, const void *b) {
	const struct raw_row *ra = a;
	const struct raw_row *rb = b;
	if (ra->group != rb->group) {
		return (int)ra->group - (int)rb->group;
	}
	return strcmp(ra->label, rb->label);
}

struct sf_cheatsheet_row *sf_cheatsheet_rows_build(const struct sf_keybind *binds, size_t n_binds,
						   size_t *n_out) {
	if (n_out != NULL) {
		*n_out = 0;
	}
	if (binds == NULL || n_binds == 0) {
		return NULL;
	}

	struct raw_row *raw = calloc(n_binds, sizeof *raw);
	if (raw == NULL) {
		return NULL;
	}
	size_t n_raw = 0;

	for (size_t i = 0; i < n_binds; i++) {
		const struct sf_keybind *b = &binds[i];
		if (b->n_steps == 0 || b->steps == NULL) {
			continue;
		}
		enum sf_action action = b->steps[0].action;
		if (action == SF_ACTION_NONE) {
			continue;
		}
		const char *cmd = b->steps[0].cmd;
		char chord[96];
		sf_cheatsheet_format_chord(b->mods, b->sym, chord, sizeof chord);

		/* Merge into existing row with same action+cmd. */
		char key[160];
		fill_merge_key(key, sizeof key, action, cmd);
		size_t hit = (size_t)-1;
		for (size_t j = 0; j < n_raw; j++) {
			if (strcmp(raw[j].merge_key, key) == 0) {
				hit = j;
				break;
			}
		}
		if (hit != (size_t)-1) {
			/* Append chord if not already present. */
			if (strstr(raw[hit].chord, chord) == NULL) {
				size_t have = strlen(raw[hit].chord);
				size_t need = strlen(chord);
				if (have + 2 + need < sizeof raw[hit].chord) {
					raw[hit].chord[have] = ',';
					raw[hit].chord[have + 1] = ' ';
					memcpy(raw[hit].chord + have + 2, chord, need + 1);
				}
			}
			continue;
		}

		struct raw_row *r = &raw[n_raw++];
		snprintf(r->chord, sizeof r->chord, "%s", chord);
		make_label(action, cmd, r->label, sizeof r->label);
		snprintf(r->merge_key, sizeof r->merge_key, "%s", key);
		r->action = action;
		r->group = sf_cheatsheet_group_for_action(action);
		r->mods = b->mods;
		r->sym = b->sym;
		r->drop = false;
		if (cmd_is_digit_1_9(cmd)) {
			snprintf(r->ws_cmd, sizeof r->ws_cmd, "%s", cmd);
		} else {
			r->ws_cmd[0] = '\0';
		}
	}

	collapse_workspace_runs(raw, n_raw, SF_ACTION_WORKSPACE_SWITCH);
	collapse_workspace_runs(raw, n_raw, SF_ACTION_MOVE_TO_WORKSPACE);

	/* Compact dropped rows. */
	size_t n_keep = 0;
	for (size_t i = 0; i < n_raw; i++) {
		if (!raw[i].drop) {
			if (n_keep != i) {
				raw[n_keep] = raw[i];
			}
			n_keep++;
		}
	}
	n_raw = n_keep;

	if (n_raw == 0) {
		free(raw);
		return NULL;
	}

	qsort(raw, n_raw, sizeof *raw, cmp_raw_group);

	struct sf_cheatsheet_row *out = calloc(n_raw, sizeof *out);
	if (out == NULL) {
		free(raw);
		return NULL;
	}
	for (size_t i = 0; i < n_raw; i++) {
		snprintf(out[i].chord, sizeof out[i].chord, "%s", raw[i].chord);
		snprintf(out[i].label, sizeof out[i].label, "%s", raw[i].label);
		out[i].group = raw[i].group;
		out[i].action = raw[i].action;
	}
	free(raw);
	if (n_out != NULL) {
		*n_out = n_raw;
	}
	return out;
}
