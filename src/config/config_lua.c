#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>

#include "config/action.h"
#include "config/config.h"
#include "config/config_embed.h"
#include "config/config_lua.h"
#include "log/log.h"
#include "server/server.h"
#include "ssd/ssd_policy.h"
#include "util/str.h"
#include "wm/workspace.h"

#ifndef STONEFLY_DATADIR
#define STONEFLY_DATADIR "/usr/share/stonefly"
#endif

#define RTP_MAX 32
#define PATH_MAX_SF 512

struct lua_runtime {
	lua_State *L;
	struct stonefly_server *server;
	char *rtp[RTP_MAX];
	size_t n_rtp;
	bool binds_replaced;
	bool taps_replaced;
	bool scratches_replaced;
	bool rules_replaced;
	unsigned rule_seq;
};

static struct lua_runtime *G;

void stonefly_config_lua_close(struct stonefly_config *cfg) {
	if (cfg == NULL || cfg->lua == NULL) {
		return;
	}
	struct lua_runtime *rt = cfg->lua;
	if (rt->L != NULL) {
		lua_close(rt->L);
	}
	for (size_t i = 0; i < rt->n_rtp; i++) {
		free(rt->rtp[i]);
	}
	free(rt);
	cfg->lua = NULL;
	if (G == rt) {
		G = NULL;
	}
}

static void rtp_add(struct lua_runtime *rt, const char *path) {
	if (path == NULL || path[0] == '\0' || rt->n_rtp >= RTP_MAX) {
		return;
	}
	for (size_t i = 0; i < rt->n_rtp; i++) {
		if (strcmp(rt->rtp[i], path) == 0) {
			return;
		}
	}
	rt->rtp[rt->n_rtp++] = stonefly_xstrdup(path);
}

static void package_path_append(lua_State *L, const char *dir) {
	char entry[PATH_MAX_SF * 2];
	snprintf(entry, sizeof entry, "%s/lua/?.lua;%s/lua/?/init.lua", dir, dir);
	lua_getglobal(L, "package");
	lua_getfield(L, -1, "path");
	const char *cur = lua_tostring(L, -1);
	lua_pushfstring(L, "%s;%s", cur ? cur : "", entry);
	lua_setfield(L, -3, "path");
	lua_pop(L, 2);
}

static int l_stdpath(lua_State *L) {
	const char *which = luaL_checkstring(L, 1);
	char buf[PATH_MAX_SF];
	int rc = -1;
	if (strcmp(which, "config") == 0) {
		rc = stonefly_config_dir(buf, sizeof buf);
	} else if (strcmp(which, "data") == 0) {
		rc = stonefly_data_dir(buf, sizeof buf);
	} else if (strcmp(which, "cache") == 0) {
		rc = stonefly_cache_dir(buf, sizeof buf);
	} else {
		return luaL_error(L, "stdpath: unknown %s", which);
	}
	if (rc != 0) {
		return luaL_error(L, "stdpath: resolve failed");
	}
	lua_pushstring(L, buf);
	return 1;
}

static int l_fs_mtime(lua_State *L) {
	const char *path = luaL_checkstring(L, 1);
	struct stat st;
	if (stat(path, &st) != 0) {
		lua_pushnil(L);
		return 1;
	}
	lua_pushinteger(L, (lua_Integer)st.st_mtime);
	return 1;
}

static int l_runtime_file(lua_State *L) {
	const char *rel = luaL_checkstring(L, 1);
	if (G == NULL) {
		lua_pushnil(L);
		return 1;
	}
	char path[PATH_MAX_SF];
	for (size_t i = 0; i < G->n_rtp; i++) {
		snprintf(path, sizeof path, "%s/%s", G->rtp[i], rel);
		if (access(path, R_OK) == 0) {
			lua_pushstring(L, path);
			return 1;
		}
	}
	lua_pushnil(L);
	return 1;
}

static void load_pack_plugins(struct lua_runtime *rt, const char *pack_root) {
	char plugin[PATH_MAX_SF];
	snprintf(plugin, sizeof plugin, "%s/plugin/fennel.lua", pack_root);
	if (access(plugin, R_OK) != 0) {
		/* any *.lua in plugin/ */
		snprintf(plugin, sizeof plugin, "%s/plugin", pack_root);
		return;
	}
	if (luaL_dofile(rt->L, plugin) != LUA_OK) {
		SF_LOGW("pack plugin failed", sf_str("path", plugin),
			sf_str("err", lua_tostring(rt->L, -1)));
		lua_pop(rt->L, 1);
	}
}

static int l_packadd(lua_State *L) {
	const char *name = luaL_checkstring(L, 1);
	if (G == NULL) {
		return luaL_error(L, "packadd: no runtime");
	}
	char data[PATH_MAX_SF];
	stonefly_data_dir(data, sizeof data);

	const char *bases[] = {data, STONEFLY_DATADIR, NULL};
	char pack[PATH_MAX_SF];
	for (int b = 0; bases[b] != NULL; b++) {
		/* pack/dist/opt/name, else pack/opt/name */
		snprintf(pack, sizeof pack, "%s/pack/dist/opt/%s", bases[b], name);
		if (access(pack, R_OK) != 0) {
			snprintf(pack, sizeof pack, "%s/pack/opt/%s", bases[b], name);
		}
		if (access(pack, R_OK) != 0) {
			continue;
		}
		rtp_add(G, pack);
		package_path_append(G->L, pack);
		load_pack_plugins(G, pack);
		lua_pushboolean(L, 1);
		return 1;
	}
	return luaL_error(L, "packadd: pack '%s' not found", name);
}

static uint32_t parse_mod(const char *s) {
	if (strcasecmp(s, "super") == 0 || strcasecmp(s, "logo") == 0 ||
	    strcasecmp(s, "mod4") == 0) {
		return STONEFLY_SUPER;
	}
	if (strcasecmp(s, "shift") == 0) {
		return STONEFLY_SHIFT;
	}
	if (strcasecmp(s, "ctrl") == 0 || strcasecmp(s, "control") == 0) {
		return STONEFLY_CTRL;
	}
	if (strcasecmp(s, "alt") == 0 || strcasecmp(s, "mod1") == 0) {
		return STONEFLY_ALT;
	}
	return 0;
}

/* Trim leading/trailing ASCII space in-place; returns start. */
static char *trim_inplace(char *s) {
	while (*s != '\0' && isspace((unsigned char)*s)) {
		s++;
	}
	if (*s == '\0') {
		return s;
	}
	char *end = s + strlen(s) - 1;
	while (end > s && isspace((unsigned char)*end)) {
		*end-- = '\0';
	}
	return s;
}

/*
 * Resolve a key token to an xkb keysym. Single Latin-1 chars use their
 * code point (so "q" vs "Q" stay distinct under Shift). Longer names go
 * through xkb_keysym_from_name (case-insensitive) with a few aliases.
 * No generated keysym table — libxkbcommon already ships the ABI map
 * peeble had to go:generate for Go.
 */
static int lookup_keysym(const char *key, xkb_keysym_t *out) {
	static const struct {
		const char *alias;
		const char *xkb;
	} aliases[] = {
		{"esc", "Escape"},  {"enter", "Return"}, {"return", "Return"},
		{"space", "space"}, {" ", "space"},
	};
	size_t len = strlen(key);
	if (len == 0) {
		return -1;
	}
	if (len == 1) {
		*out = (xkb_keysym_t)(unsigned char)key[0];
		return 0;
	}
	const char *name = key;
	for (size_t i = 0; i < sizeof aliases / sizeof aliases[0]; i++) {
		if (strcasecmp(key, aliases[i].alias) == 0) {
			name = aliases[i].xkb;
			break;
		}
	}
	xkb_keysym_t sym = xkb_keysym_from_name(name, XKB_KEYSYM_CASE_INSENSITIVE);
	if (sym == XKB_KEY_NoSymbol) {
		return -1;
	}
	*out = sym;
	return 0;
}

/*
 * Parse peeble/i3/sway-style chords: "Super+w", "Super+Shift+Tab", "Print".
 * Returns 0 on success. Unknown modifier or key → -1.
 */
static int parse_chord(const char *s, uint32_t *mods_out, xkb_keysym_t *sym_out) {
	char buf[128];
	size_t n = strlen(s);
	if (n == 0 || n >= sizeof buf) {
		return -1;
	}
	memcpy(buf, s, n + 1);

	char *last_plus = strrchr(buf, '+');
	char *key;
	uint32_t mods = 0;
	if (last_plus == NULL) {
		key = trim_inplace(buf);
	} else {
		*last_plus = '\0';
		key = trim_inplace(last_plus + 1);
		char *p = buf;
		while (p != NULL && *p != '\0') {
			char *plus = strchr(p, '+');
			if (plus != NULL) {
				*plus = '\0';
			}
			char *tok = trim_inplace(p);
			if (*tok != '\0') {
				uint32_t bit = parse_mod(tok);
				if (bit == 0) {
					return -1;
				}
				mods |= bit;
			}
			p = plus != NULL ? plus + 1 : NULL;
		}
	}
	if (lookup_keysym(key, sym_out) != 0) {
		return -1;
	}
	*mods_out = mods;
	return 0;
}

/* Lone-modifier tap names → left keysym (+ caller adds right as alt). */
static int parse_tap_mod(const char *s, xkb_keysym_t *sym_out, xkb_keysym_t *sym_alt_out) {
	if (strcasecmp(s, "super") == 0 || strcasecmp(s, "mod4") == 0 ||
	    strcasecmp(s, "logo") == 0) {
		*sym_out = XKB_KEY_Super_L;
		*sym_alt_out = XKB_KEY_Super_R;
		return 0;
	}
	if (strcasecmp(s, "alt") == 0 || strcasecmp(s, "mod1") == 0) {
		*sym_out = XKB_KEY_Alt_L;
		*sym_alt_out = XKB_KEY_Alt_R;
		return 0;
	}
	if (strcasecmp(s, "shift") == 0) {
		*sym_out = XKB_KEY_Shift_L;
		*sym_alt_out = XKB_KEY_Shift_R;
		return 0;
	}
	if (strcasecmp(s, "ctrl") == 0 || strcasecmp(s, "control") == 0) {
		*sym_out = XKB_KEY_Control_L;
		*sym_alt_out = XKB_KEY_Control_R;
		return 0;
	}
	return -1;
}

/* Shift+Tab often surfaces as ISO_Left_Tab; accept both when Shift is set. */
static void maybe_tab_alt(uint32_t mods, xkb_keysym_t sym, xkb_keysym_t *sym_alt) {
	if (*sym_alt == 0 && sym == XKB_KEY_Tab && (mods & STONEFLY_SHIFT)) {
		*sym_alt = XKB_KEY_ISO_Left_Tab;
	}
}

static void clear_keybinds(struct stonefly_config *cfg) {
	for (size_t i = 0; i < cfg->n_keybinds; i++) {
		free(cfg->keybinds[i].cmd);
	}
	free(cfg->keybinds);
	cfg->keybinds = NULL;
	cfg->n_keybinds = 0;
}

static void clear_tapbinds(struct stonefly_config *cfg) {
	for (size_t i = 0; i < cfg->n_tapbinds; i++) {
		free(cfg->tapbinds[i].single_cmd);
		free(cfg->tapbinds[i].double_cmd);
	}
	free(cfg->tapbinds);
	cfg->tapbinds = NULL;
	cfg->n_tapbinds = 0;
}

static void clear_scratchpads(struct stonefly_config *cfg) {
	for (size_t i = 0; i < cfg->n_scratchpads; i++) {
		free(cfg->scratchpads[i].name);
		free(cfg->scratchpads[i].cmd);
		free(cfg->scratchpads[i].app_id);
	}
	free(cfg->scratchpads);
	cfg->scratchpads = NULL;
	cfg->n_scratchpads = 0;
}

static void clear_window_rules(struct stonefly_config *cfg) {
	for (size_t i = 0; i < cfg->n_window_rules; i++) {
		free(cfg->window_rules[i].app_id);
		free(cfg->window_rules[i].title);
	}
	free(cfg->window_rules);
	cfg->window_rules = NULL;
	cfg->n_window_rules = 0;
}

bool stonefly_lua_pattern_match(const struct stonefly_config *cfg, const char *subject,
				const char *pattern) {
	if (pattern == NULL || pattern[0] == '\0') {
		return true;
	}
	if (subject == NULL || cfg == NULL || cfg->lua == NULL) {
		return false;
	}
	struct lua_runtime *rt = cfg->lua;
	lua_State *L = rt->L;
	if (L == NULL) {
		return false;
	}
	lua_getglobal(L, "string");
	lua_getfield(L, -1, "find");
	lua_remove(L, -2);
	lua_pushstring(L, subject);
	lua_pushstring(L, pattern);
	if (lua_pcall(L, 2, 1, 0) != LUA_OK) {
		lua_pop(L, 1);
		return false;
	}
	bool ok = !lua_isnil(L, -1);
	lua_pop(L, 1);
	return ok;
}

/* Reject bad patterns at config load (string.find("", pat)). */
static int check_lua_pattern(lua_State *L, const char *pat, const char *which) {
	lua_getglobal(L, "string");
	lua_getfield(L, -1, "find");
	lua_remove(L, -2);
	lua_pushliteral(L, "");
	lua_pushstring(L, pat);
	if (lua_pcall(L, 2, 0, 0) != LUA_OK) {
		const char *err = lua_tostring(L, -1);
		return luaL_error(L, "window_rule: bad %s pattern: %s", which,
				  err != NULL ? err : "?");
	}
	return 0;
}

static const struct stonefly_window_rule *find_window_rule(const struct stonefly_config *cfg,
							   const char *id) {
	if (id == NULL || id[0] == '\0') {
		return NULL;
	}
	for (size_t i = 0; i < cfg->n_window_rules; i++) {
		if (strcmp(cfg->window_rules[i].id, id) == 0) {
			return &cfg->window_rules[i];
		}
	}
	return NULL;
}

static int parse_rule_match(lua_State *L, int idx, struct stonefly_window_rule *out) {
	if (!lua_istable(L, idx)) {
		return luaL_error(L, "window_rule: match must be a table");
	}
	lua_pushnil(L);
	while (lua_next(L, idx) != 0) {
		const char *k = lua_tostring(L, -2);
		if (k == NULL) {
			lua_pop(L, 1);
			continue;
		}
		if (strcmp(k, "app_id") == 0) {
			const char *s = lua_tostring(L, -1);
			if (s == NULL) {
				return luaL_error(L, "window_rule: match.app_id must be a string");
			}
			check_lua_pattern(L, s, "app_id");
			free(out->app_id);
			out->app_id = stonefly_xstrdup(s);
		} else if (strcmp(k, "title") == 0) {
			const char *s = lua_tostring(L, -1);
			if (s == NULL) {
				return luaL_error(L, "window_rule: match.title must be a string");
			}
			check_lua_pattern(L, s, "title");
			free(out->title);
			out->title = stonefly_xstrdup(s);
		} else if (strcmp(k, "title_exact") == 0) {
			return luaL_error(L,
					  "window_rule: title_exact removed; use title \"^...$\" "
					  "(Lua pattern)");
		} else {
			return luaL_error(L, "window_rule: unknown match key %s", k);
		}
		lua_pop(L, 1);
	}
	return 0;
}

static int parse_rule_actions(lua_State *L, int idx, struct stonefly_window_rule *out) {
	if (!lua_istable(L, idx)) {
		return luaL_error(L, "window_rule: actions must be a table");
	}
	lua_pushnil(L);
	while (lua_next(L, idx) != 0) {
		const char *k = lua_tostring(L, -2);
		if (k == NULL) {
			lua_pop(L, 1);
			continue;
		}
		if (strcmp(k, "center") == 0) {
			if (!lua_isboolean(L, -1)) {
				return luaL_error(L, "window_rule: actions.center must be a bool");
			}
			out->center = lua_toboolean(L, -1);
			out->center_set = true;
		} else if (strcmp(k, "float") == 0) {
			if (!lua_isboolean(L, -1)) {
				return luaL_error(L, "window_rule: actions.float must be a bool");
			}
			out->float_win = lua_toboolean(L, -1);
			out->float_set = true;
		} else if (strcmp(k, "borderless") == 0) {
			if (!lua_isboolean(L, -1)) {
				return luaL_error(L,
						  "window_rule: actions.borderless must be a bool");
			}
			out->borderless = lua_toboolean(L, -1);
			out->borderless_set = true;
		} else if (strcmp(k, "ssd") == 0) {
			const char *s = lua_tostring(L, -1);
			if (s == NULL) {
				return luaL_error(L, "window_rule: actions.ssd must be a string");
			}
			out->ssd_mode = (int)stonefly_ssd_mode_parse(s);
			out->ssd_set = true;
		} else if (strcmp(k, "width") == 0) {
			if (!lua_isnumber(L, -1)) {
				return luaL_error(L, "window_rule: actions.width must be a number");
			}
			int w = (int)lua_tonumber(L, -1);
			if (w < 1) {
				return luaL_error(L, "window_rule: actions.width must be >= 1");
			}
			out->width = w;
			out->width_set = true;
		} else if (strcmp(k, "height") == 0) {
			if (!lua_isnumber(L, -1)) {
				return luaL_error(L,
						  "window_rule: actions.height must be a number");
			}
			int h = (int)lua_tonumber(L, -1);
			if (h < 1) {
				return luaL_error(L, "window_rule: actions.height must be >= 1");
			}
			out->height = h;
			out->height_set = true;
		} else {
			return luaL_error(L, "window_rule: unknown actions key %s", k);
		}
		lua_pop(L, 1);
	}
	return 0;
}

static void merge_window_rule(struct stonefly_window_rule *dst,
			      const struct stonefly_window_rule *parent,
			      const struct stonefly_window_rule *child) {
	*dst = (struct stonefly_window_rule){0};
	snprintf(dst->id, sizeof dst->id, "%s", child->id);
	snprintf(dst->parent_id, sizeof dst->parent_id, "%s", parent->id);

	dst->app_id = parent->app_id != NULL ? stonefly_xstrdup(parent->app_id) : NULL;
	dst->title = parent->title != NULL ? stonefly_xstrdup(parent->title) : NULL;
	if (child->app_id != NULL && child->app_id[0] != '\0') {
		free(dst->app_id);
		dst->app_id = stonefly_xstrdup(child->app_id);
	}
	if (child->title != NULL && child->title[0] != '\0') {
		free(dst->title);
		dst->title = stonefly_xstrdup(child->title);
	}

	dst->center = parent->center;
	dst->center_set = parent->center_set;
	dst->float_win = parent->float_win;
	dst->float_set = parent->float_set;
	dst->borderless = parent->borderless;
	dst->borderless_set = parent->borderless_set;
	dst->ssd_set = parent->ssd_set;
	dst->ssd_mode = parent->ssd_mode;
	dst->width_set = parent->width_set;
	dst->height_set = parent->height_set;
	dst->width = parent->width;
	dst->height = parent->height;

	if (child->center_set) {
		dst->center = child->center;
		dst->center_set = true;
	}
	if (child->float_set) {
		dst->float_win = child->float_win;
		dst->float_set = true;
	}
	if (child->borderless_set) {
		dst->borderless = child->borderless;
		dst->borderless_set = true;
	}
	if (child->ssd_set) {
		dst->ssd_mode = child->ssd_mode;
		dst->ssd_set = true;
	}
	if (child->width_set) {
		dst->width = child->width;
		dst->width_set = true;
	}
	if (child->height_set) {
		dst->height = child->height;
		dst->height_set = true;
	}
}

/*
 * window_rule(match, actions [, parent_tag]) → tag string
 * Parent composition: child overwrites keys it sets (peeble window_rule).
 */
static int l_window_rule(lua_State *L) {
	int narg = lua_gettop(L);
	if (narg < 2 || narg > 3) {
		return luaL_error(
			L, "window_rule expects (match, actions) or (match, actions, parent)");
	}
	if (G == NULL) {
		return luaL_error(L, "window_rule: no runtime");
	}
	struct stonefly_config *cfg = &G->server->config;
	if (!G->rules_replaced) {
		clear_window_rules(cfg);
		G->rules_replaced = true;
		G->rule_seq = 0;
	}
	if (cfg->n_window_rules >= STONEFLY_WINDOW_RULE_MAX) {
		return luaL_error(L, "window_rule: max %d", STONEFLY_WINDOW_RULE_MAX);
	}

	struct stonefly_window_rule raw = {0};
	parse_rule_match(L, 1, &raw);
	parse_rule_actions(L, 2, &raw);

	/* Parent id only — realloc may move window_rules; re-resolve after. */
	char parent_id[40] = {0};
	if (narg == 3 && !lua_isnil(L, 3)) {
		const char *pid = lua_tostring(L, 3);
		if (pid == NULL || pid[0] == '\0') {
			free(raw.app_id);
			free(raw.title);
			return luaL_error(L, "window_rule: parent must be a rule tag string");
		}
		if (find_window_rule(cfg, pid) == NULL) {
			free(raw.app_id);
			free(raw.title);
			return luaL_error(L, "window_rule: unknown parent tag %s", pid);
		}
		snprintf(parent_id, sizeof parent_id, "%s", pid);
	}

	G->rule_seq++;
	snprintf(raw.id, sizeof raw.id, "rule-%u", G->rule_seq);

	struct stonefly_window_rule *nb =
		realloc(cfg->window_rules, (cfg->n_window_rules + 1) * sizeof *nb);
	if (nb == NULL) {
		free(raw.app_id);
		free(raw.title);
		return luaL_error(L, "window_rule: OOM");
	}
	cfg->window_rules = nb;
	struct stonefly_window_rule *dst = &cfg->window_rules[cfg->n_window_rules];

	if (parent_id[0] != '\0') {
		const struct stonefly_window_rule *parent = find_window_rule(cfg, parent_id);
		if (parent == NULL) {
			free(raw.app_id);
			free(raw.title);
			return luaL_error(L, "window_rule: unknown parent tag %s", parent_id);
		}
		merge_window_rule(dst, parent, &raw);
		free(raw.app_id);
		free(raw.title);
	} else {
		*dst = raw;
		raw.app_id = NULL;
		raw.title = NULL;
	}

	bool has_match = (dst->app_id != NULL && dst->app_id[0] != '\0') ||
			 (dst->title != NULL && dst->title[0] != '\0');
	if (!has_match) {
		free(dst->app_id);
		free(dst->title);
		return luaL_error(L, "window_rule: match needs app_id or title");
	}
	cfg->n_window_rules++;
	lua_pushstring(L, dst->id);
	return 1;
}

/*
 * bind forms:
 *   bind("Super+w", { action = "exec", cmd = "..." })
 *   bind({ key = "Super+Shift+Tab", action = "focus_prev" })  -- chord-in-key
 * Optional: key_alt, cmd. Shift+Tab auto-adds ISO_Left_Tab as key_alt.
 */
static int l_bind(lua_State *L) {
	if (G == NULL) {
		return luaL_error(L, "bind: no runtime");
	}
	struct stonefly_config *cfg = &G->server->config;
	if (!G->binds_replaced) {
		clear_keybinds(cfg);
		G->binds_replaced = true;
	}

	int opts = 0; /* stack index of options table */
	uint32_t mods = 0;
	xkb_keysym_t sym = XKB_KEY_NoSymbol;

	if (lua_type(L, 1) == LUA_TSTRING) {
		const char *chord = lua_tostring(L, 1);
		if (parse_chord(chord, &mods, &sym) != 0) {
			return luaL_error(L, "bind: bad chord %s", chord);
		}
		luaL_checktype(L, 2, LUA_TTABLE);
		opts = 2;
	} else {
		luaL_checktype(L, 1, LUA_TTABLE);
		opts = 1;
		lua_getfield(L, opts, "key");
		const char *key = lua_isstring(L, -1) ? lua_tostring(L, -1) : NULL;
		if (key == NULL) {
			lua_pop(L, 1);
			return luaL_error(L, "bind: missing key");
		}
		if (parse_chord(key, &mods, &sym) != 0) {
			lua_pop(L, 1);
			return luaL_error(L, "bind: unknown key %s", key);
		}
		lua_pop(L, 1);
	}

	xkb_keysym_t sym_alt = 0;
	lua_getfield(L, opts, "key_alt");
	if (lua_isstring(L, -1)) {
		const char *alt = lua_tostring(L, -1);
		if (lookup_keysym(alt, &sym_alt) != 0) {
			sym_alt = xkb_keysym_from_name(alt, XKB_KEYSYM_CASE_INSENSITIVE);
		}
	}
	lua_pop(L, 1);
	maybe_tab_alt(mods, sym, &sym_alt);

	lua_getfield(L, opts, "action");
	const char *act = luaL_checkstring(L, -1);
	enum stonefly_action action = stonefly_action_parse(act);
	lua_pop(L, 1);

	lua_getfield(L, opts, "cmd");
	const char *cmd = lua_isstring(L, -1) ? lua_tostring(L, -1) : NULL;

	struct stonefly_keybind *nb = realloc(cfg->keybinds, (cfg->n_keybinds + 1) * sizeof *nb);
	if (nb == NULL) {
		lua_pop(L, 1);
		return luaL_error(L, "bind: OOM");
	}
	cfg->keybinds = nb;
	struct stonefly_keybind *b = &cfg->keybinds[cfg->n_keybinds++];
	memset(b, 0, sizeof *b);
	b->mods = mods;
	b->sym = sym;
	b->sym_alt = sym_alt;
	b->action = action;
	b->cmd = stonefly_xstrdup(cmd);
	lua_pop(L, 1);
	return 0;
}

/*
 * tap forms (modifier name only; L+R via parse_tap_mod):
 *   tap("Super", { single = "exec", double = "scratch_toggle", ... })
 *   tap({ key = "Super", ... })
 */
static int l_tap(lua_State *L) {
	if (G == NULL) {
		return luaL_error(L, "tap: no runtime");
	}
	struct stonefly_config *cfg = &G->server->config;
	if (!G->taps_replaced) {
		clear_tapbinds(cfg);
		G->taps_replaced = true;
	}

	int opts = 0;
	xkb_keysym_t sym = XKB_KEY_NoSymbol;
	xkb_keysym_t sym_alt = 0;

	if (lua_type(L, 1) == LUA_TSTRING) {
		const char *name = lua_tostring(L, 1);
		if (parse_tap_mod(name, &sym, &sym_alt) != 0) {
			return luaL_error(L, "tap: expected modifier name, got %s", name);
		}
		luaL_checktype(L, 2, LUA_TTABLE);
		opts = 2;
	} else {
		luaL_checktype(L, 1, LUA_TTABLE);
		opts = 1;
		lua_getfield(L, opts, "key");
		const char *key = luaL_checkstring(L, -1);
		lua_pop(L, 1);
		if (parse_tap_mod(key, &sym, &sym_alt) != 0) {
			return luaL_error(L, "tap: expected modifier name, got %s", key);
		}
	}

	lua_getfield(L, opts, "single");
	enum stonefly_action single =
		stonefly_action_parse(lua_isstring(L, -1) ? lua_tostring(L, -1) : NULL);
	lua_pop(L, 1);
	lua_getfield(L, opts, "double");
	enum stonefly_action dbl =
		stonefly_action_parse(lua_isstring(L, -1) ? lua_tostring(L, -1) : NULL);
	lua_pop(L, 1);

	lua_getfield(L, opts, "single_cmd");
	char *scmd_c = stonefly_xstrdup(lua_isstring(L, -1) ? lua_tostring(L, -1) : NULL);
	lua_pop(L, 1);
	lua_getfield(L, opts, "double_cmd");
	char *dcmd_c = stonefly_xstrdup(lua_isstring(L, -1) ? lua_tostring(L, -1) : NULL);
	lua_pop(L, 1);

	struct stonefly_tapbind *nb = realloc(cfg->tapbinds, (cfg->n_tapbinds + 1) * sizeof *nb);
	if (nb == NULL) {
		free(scmd_c);
		free(dcmd_c);
		return luaL_error(L, "tap: OOM");
	}
	cfg->tapbinds = nb;
	struct stonefly_tapbind *t = &cfg->tapbinds[cfg->n_tapbinds++];
	memset(t, 0, sizeof *t);
	t->sym = sym;
	t->sym_alt = sym_alt;
	t->single = single;
	t->double_tap = dbl;
	t->single_cmd = scmd_c;
	t->double_cmd = dcmd_c;
	return 0;
}

static int l_scratchpad(lua_State *L) {
	luaL_checktype(L, 1, LUA_TTABLE);
	if (G == NULL) {
		return luaL_error(L, "scratchpad: no runtime");
	}
	struct stonefly_config *cfg = &G->server->config;
	if (!G->scratches_replaced) {
		clear_scratchpads(cfg);
		G->scratches_replaced = true;
	}
	if (cfg->n_scratchpads >= STONEFLY_SCRATCHPAD_MAX) {
		return luaL_error(L, "scratchpad: max %d", STONEFLY_SCRATCHPAD_MAX);
	}
	lua_getfield(L, 1, "name");
	const char *name = luaL_checkstring(L, -1);
	lua_getfield(L, 1, "cmd");
	const char *cmd = luaL_checkstring(L, -1);
	lua_getfield(L, 1, "app_id");
	const char *app = lua_isstring(L, -1) ? lua_tostring(L, -1) : "";
	lua_getfield(L, 1, "auto_hide");
	/* Peeble default: auto_hide true when omitted. */
	bool auto_hide = !lua_isboolean(L, -1) || lua_toboolean(L, -1);

	struct stonefly_scratchpad_spec *nb =
		realloc(cfg->scratchpads, (cfg->n_scratchpads + 1) * sizeof *nb);
	if (nb == NULL) {
		return luaL_error(L, "scratchpad: OOM");
	}
	cfg->scratchpads = nb;
	struct stonefly_scratchpad_spec *s = &cfg->scratchpads[cfg->n_scratchpads++];
	s->name = stonefly_xstrdup(name);
	s->cmd = stonefly_xstrdup(cmd);
	s->app_id = stonefly_xstrdup(app);
	s->auto_hide = auto_hide;
	lua_pop(L, 4);
	return 0;
}

enum opt_num_kind {
	OPT_NUM_FLOAT,
	OPT_NUM_INT,
	OPT_NUM_FN,
};

struct opt_num {
	const char *name;
	enum opt_num_kind kind;
	size_t off;
	void (*set)(struct stonefly_config *cfg, double v);
};

static void opt_set_ssd_height(struct stonefly_config *cfg, double v) {
	int h = (int)v;
	cfg->ssd_height = h < 16 ? 16 : h;
}

static void opt_set_ssd_font_size(struct stonefly_config *cfg, double v) {
	int s = (int)v;
	if (s < 8) {
		s = 8;
	}
	if (s > 72) {
		s = 72;
	}
	cfg->ssd_font_size = s;
}

static void opt_set_resize_border_in(struct stonefly_config *cfg, double v) {
	int n = (int)v;
	cfg->resize_border_in = n < 0 ? 0 : n;
}

static void opt_set_resize_border_out(struct stonefly_config *cfg, double v) {
	int n = (int)v;
	cfg->resize_border_out = n < 0 ? 0 : n;
}

static const struct opt_num opt_numbers[] = {
	{"scale", OPT_NUM_FLOAT, offsetof(struct stonefly_config, output_scale), NULL},
	{"cursor_size", OPT_NUM_INT, offsetof(struct stonefly_config, cursor_size_default), NULL},
	{"gap", OPT_NUM_INT, offsetof(struct stonefly_config, gap_size), NULL},
	{"cascade_step", OPT_NUM_INT, offsetof(struct stonefly_config, cascade_step), NULL},
	{"cascade_wrap", OPT_NUM_INT, offsetof(struct stonefly_config, cascade_wrap), NULL},
	{"slide_duration_ms", OPT_NUM_INT, offsetof(struct stonefly_config, slide_duration_ms),
	 NULL},
	{"morph_duration_ms", OPT_NUM_INT, offsetof(struct stonefly_config, morph_duration_ms),
	 NULL},
	{"client_opacity", OPT_NUM_FLOAT, offsetof(struct stonefly_config, client_opacity), NULL},
	{"inactive_opacity", OPT_NUM_FLOAT, offsetof(struct stonefly_config, inactive_opacity),
	 NULL},
	{"corner_radius", OPT_NUM_INT, offsetof(struct stonefly_config, corner_radius), NULL},
	{"border_thickness", OPT_NUM_INT, offsetof(struct stonefly_config, border_thickness), NULL},
	{"shadow_blur_sigma", OPT_NUM_FLOAT, offsetof(struct stonefly_config, shadow_blur_sigma),
	 NULL},
	{"blur_num_passes", OPT_NUM_INT, offsetof(struct stonefly_config, blur_num_passes), NULL},
	{"blur_radius", OPT_NUM_INT, offsetof(struct stonefly_config, blur_radius), NULL},
	{"blur_noise", OPT_NUM_FLOAT, offsetof(struct stonefly_config, blur_noise), NULL},
	{"blur_brightness", OPT_NUM_FLOAT, offsetof(struct stonefly_config, blur_brightness), NULL},
	{"blur_contrast", OPT_NUM_FLOAT, offsetof(struct stonefly_config, blur_contrast), NULL},
	{"blur_saturation", OPT_NUM_FLOAT, offsetof(struct stonefly_config, blur_saturation), NULL},
	{"bell_flash_ms", OPT_NUM_INT, offsetof(struct stonefly_config, bell_flash_ms), NULL},
	{"snap_threshold", OPT_NUM_INT, offsetof(struct stonefly_config, snap_threshold), NULL},
	{"snap_preview_border", OPT_NUM_INT, offsetof(struct stonefly_config, snap_preview_border),
	 NULL},
	{"snap_preview_opacity", OPT_NUM_FLOAT,
	 offsetof(struct stonefly_config, snap_preview_opacity), NULL},
	{"tap_max_hold_ms", OPT_NUM_INT, offsetof(struct stonefly_config, tap_max_hold_ms), NULL},
	{"double_tap_gap_ms", OPT_NUM_INT, offsetof(struct stonefly_config, double_tap_gap_ms),
	 NULL},
	{"swipe_workspace_threshold", OPT_NUM_INT,
	 offsetof(struct stonefly_config, swipe_workspace_threshold), NULL},
	{"ssd_height", OPT_NUM_FN, 0, opt_set_ssd_height},
	{"ssd_font_size", OPT_NUM_FN, 0, opt_set_ssd_font_size},
	{"resize_border_in", OPT_NUM_FN, 0, opt_set_resize_border_in},
	{"resize_border_out", OPT_NUM_FN, 0, opt_set_resize_border_out},
};

static const struct {
	const char *name;
	size_t off;
} opt_bools[] = {
	{"slide_dir_closest", offsetof(struct stonefly_config, slide_dir_closest)},
	{"window_blur", offsetof(struct stonefly_config, window_blur)},
	{"blur_xray", offsetof(struct stonefly_config, blur_xray)},
	{"swipe_workspace_natural", offsetof(struct stonefly_config, swipe_workspace_natural)},
};

static const struct {
	const char *name;
	size_t off; /* float[4] */
} opt_colors[] = {
	{"border_active", offsetof(struct stonefly_config, border_active)},
	{"border_inactive", offsetof(struct stonefly_config, border_inactive)},
	{"border_urgent", offsetof(struct stonefly_config, border_urgent)},
	{"shadow_color", offsetof(struct stonefly_config, shadow_color)},
};

static void opt_set_number(struct stonefly_config *cfg, const char *k, double v) {
	for (size_t i = 0; i < sizeof opt_numbers / sizeof opt_numbers[0]; i++) {
		const struct opt_num *e = &opt_numbers[i];
		if (strcmp(k, e->name) != 0) {
			continue;
		}
		if (e->kind == OPT_NUM_FLOAT) {
			*(float *)((char *)cfg + e->off) = (float)v;
		} else if (e->kind == OPT_NUM_INT) {
			*(int *)((char *)cfg + e->off) = (int)v;
		} else {
			e->set(cfg, v);
		}
		return;
	}
}

static int l_opt_newindex(lua_State *L) {
	const char *k = luaL_checkstring(L, 2);
	if (G == NULL) {
		return 0;
	}
	struct stonefly_config *cfg = &G->server->config;
	if (lua_isboolean(L, 3)) {
		int v = lua_toboolean(L, 3);
		for (size_t i = 0; i < sizeof opt_bools / sizeof opt_bools[0]; i++) {
			if (strcmp(k, opt_bools[i].name) == 0) {
				*(int *)((char *)cfg + opt_bools[i].off) = v;
				break;
			}
		}
		return 0;
	}
	if (lua_isnumber(L, 3)) {
		opt_set_number(cfg, k, lua_tonumber(L, 3));
		return 0;
	}
	if (lua_isstring(L, 3)) {
		const char *s = lua_tostring(L, 3);
		if (strcmp(k, "capture_block_app_ids") == 0) {
			stonefly_replace_str(&cfg->capture_block_app_ids, s);
		} else if (strcmp(k, "layout") == 0) {
			cfg->layout = (int)stonefly_layout_parse(s);
		} else if (strcmp(k, "ssd_mode") == 0) {
			cfg->ssd_mode = (int)stonefly_ssd_mode_parse(s);
		}
		return 0;
	}
	if (lua_istable(L, 3)) {
		if (strcmp(k, "capture_block_app_ids") == 0) {
			/* Join table of strings into a comma-separated list. */
			/* LuaJIT is 5.1-shaped: lua_objlen, not lua_rawlen. */
			size_t n = (size_t)lua_objlen(L, 3);
			size_t cap = 64;
			char *buf = malloc(cap);
			size_t len = 0;
			if (buf == NULL) {
				return 0;
			}
			buf[0] = '\0';
			for (size_t i = 1; i <= n; i++) {
				lua_rawgeti(L, 3, (int)i);
				const char *s = lua_tostring(L, -1);
				lua_pop(L, 1);
				if (s == NULL || s[0] == '\0') {
					continue;
				}
				size_t slen = strlen(s);
				size_t need = len + slen + (len > 0 ? 1 : 0) + 1;
				if (need > cap) {
					cap = need * 2;
					char *nb = realloc(buf, cap);
					if (nb == NULL) {
						free(buf);
						return 0;
					}
					buf = nb;
				}
				if (len > 0) {
					buf[len++] = ',';
				}
				memcpy(buf + len, s, slen);
				len += slen;
				buf[len] = '\0';
			}
			free(cfg->capture_block_app_ids);
			cfg->capture_block_app_ids = buf;
			return 0;
		}
		for (size_t i = 0; i < sizeof opt_colors / sizeof opt_colors[0]; i++) {
			if (strcmp(k, opt_colors[i].name) != 0) {
				continue;
			}
			float *dst = (float *)((char *)cfg + opt_colors[i].off);
			for (int c = 0; c < 4; c++) {
				lua_rawgeti(L, 3, c + 1);
				dst[c] = (float)lua_tonumber(L, -1);
				lua_pop(L, 1);
			}
			break;
		}
	}
	return 0;
}

static void push_stonefly_table(struct lua_runtime *rt) {
	lua_State *L = rt->L;
	lua_newtable(L); /* stonefly */

	lua_pushcfunction(L, l_stdpath);
	lua_setfield(L, -2, "stdpath");
	lua_pushcfunction(L, l_runtime_file);
	lua_setfield(L, -2, "runtime_file");
	lua_pushcfunction(L, l_fs_mtime);
	lua_setfield(L, -2, "_fs_mtime");
	lua_pushcfunction(L, l_packadd);
	lua_setfield(L, -2, "packadd");
	lua_pushcfunction(L, l_bind);
	lua_setfield(L, -2, "bind");
	lua_pushcfunction(L, l_tap);
	lua_setfield(L, -2, "tap");
	lua_pushcfunction(L, l_scratchpad);
	lua_setfield(L, -2, "scratchpad");
	lua_pushcfunction(L, l_window_rule);
	lua_setfield(L, -2, "window_rule");

	/* stonefly.opt with __newindex */
	lua_newtable(L); /* opt */
	lua_newtable(L); /* mt */
	lua_pushcfunction(L, l_opt_newindex);
	lua_setfield(L, -2, "__newindex");
	lua_setmetatable(L, -2);
	lua_setfield(L, -2, "opt");

	lua_setglobal(L, "stonefly");
}

static void setup_rtp(struct lua_runtime *rt) {
	char config[PATH_MAX_SF], data[PATH_MAX_SF];
	if (stonefly_config_dir(config, sizeof config) == 0) {
		rtp_add(rt, config);
	}
	if (stonefly_data_dir(data, sizeof data) == 0) {
		rtp_add(rt, data);
	}
	rtp_add(rt, STONEFLY_DATADIR "/runtime");
	rtp_add(rt, STONEFLY_DATADIR);

	for (size_t i = 0; i < rt->n_rtp; i++) {
		package_path_append(rt->L, rt->rtp[i]);
	}
}

static bool eval_buffer(struct lua_runtime *rt, const char *name, const unsigned char *data,
			size_t len) {
	if (luaL_loadbuffer(rt->L, (const char *)data, len, name) != LUA_OK) {
		SF_LOGE("config load", sf_str("name", name),
			sf_str("err", lua_tostring(rt->L, -1)));
		lua_pop(rt->L, 1);
		return false;
	}
	if (lua_pcall(rt->L, 0, 0, 0) != LUA_OK) {
		SF_LOGE("config eval", sf_str("name", name),
			sf_str("err", lua_tostring(rt->L, -1)));
		lua_pop(rt->L, 1);
		return false;
	}
	return true;
}

static bool write_embed_tree_to(const char *dir) {
	if (stonefly_mkdir_p(dir) != 0) {
		return false;
	}
	for (size_t i = 0; i < stonefly_default_files_len; i++) {
		const struct stonefly_embed_file *f = &stonefly_default_files[i];
		char path[PATH_MAX_SF];
		snprintf(path, sizeof path, "%s/%s", dir, f->name);
		FILE *out = fopen(path, "we");
		if (out == NULL) {
			out = fopen(path, "w");
		}
		if (out == NULL) {
			return false;
		}
		if (fwrite(f->data, 1, f->len, out) != f->len) {
			fclose(out);
			return false;
		}
		fclose(out);
	}
	return true;
}

static bool run_embedded_default(struct lua_runtime *rt) {
	/*
	 * Materialize embedded tree into cache so Fennel require() can open
	 * real paths (bytecode cache + runtime_file). Not the user config dir.
	 */
	char cache[PATH_MAX_SF];
	char emb[PATH_MAX_SF];
	if (stonefly_cache_dir(cache, sizeof cache) != 0) {
		return false;
	}
	snprintf(emb, sizeof emb, "%s/embedded-default", cache);
	if (!write_embed_tree_to(emb)) {
		SF_LOGE("config: failed to materialize embedded default", sf_str("dir", emb));
		return false;
	}
	/* Prefer embedded tree on rtp for require("binds") etc. */
	rtp_add(rt, emb);
	package_path_append(rt->L, emb);

	const struct stonefly_embed_file *init = &stonefly_default_files[0];
	return eval_buffer(rt, "=embedded:init.lua", init->data, init->len);
}

static bool lua_boot(struct stonefly_server *server) {
	struct lua_runtime *rt = calloc(1, sizeof *rt);
	if (rt == NULL) {
		return false;
	}
	rt->server = server;
	rt->L = luaL_newstate();
	if (rt->L == NULL) {
		free(rt);
		return false;
	}
	luaL_openlibs(rt->L);
	G = rt;
	server->config.lua = rt;
	push_stonefly_table(rt);
	setup_rtp(rt);
	return true;
}

bool stonefly_config_load(struct stonefly_server *server) {
	const char *noconfig = getenv("STONEFLY_NOCONFIG");
	if (noconfig != NULL && noconfig[0] != '\0' && strcmp(noconfig, "0") != 0) {
		SF_LOGI("config", sf_str("mode", "STONEFLY_NOCONFIG"));
		return true;
	}

	if (!lua_boot(server)) {
		SF_LOGE("config: lua_boot failed");
		return false;
	}

	char init_path[PATH_MAX_SF];
	bool ok;
	if (stonefly_config_init_path(init_path, sizeof init_path) == 0 &&
	    access(init_path, R_OK) == 0) {
		SF_LOGI("config", sf_str("path", init_path));
		ok = luaL_dofile(G->L, init_path) == LUA_OK;
		if (!ok) {
			const char *lua_err = lua_tostring(G->L, -1);
			SF_LOGE("config user init failed; trying embedded",
				sf_str("err", lua_err != NULL ? lua_err : ""));
			/* Remember for a desktop notification after WAYLAND_DISPLAY is up. */
			free(server->config_warn);
			server->config_warn = NULL;
			{
				char body[1024];
				snprintf(body, sizeof body,
					 "%s\n\n%s\n\nFalling back to the default configuration.",
					 init_path,
					 lua_err != NULL ? lua_err : "(unknown Lua error)");
				server->config_warn = strdup(body);
			}
			lua_pop(G->L, 1);
			/* recreate clean state */
			stonefly_config_lua_close(&server->config);
			stonefly_config_finish(&server->config);
			stonefly_config_set_defaults(&server->config);
			if (!lua_boot(server)) {
				return false;
			}
			ok = run_embedded_default(G);
		}
	} else {
		SF_LOGI("config", sf_str("mode", "embedded-default"));
		ok = run_embedded_default(G);
	}

	if (!ok) {
		SF_LOGE("config: embedded default failed; keeping C defaults");
		stonefly_config_lua_close(&server->config);
		stonefly_config_finish(&server->config);
		stonefly_config_set_defaults(&server->config);
		return true; /* compositor still starts */
	}
	return true;
}
