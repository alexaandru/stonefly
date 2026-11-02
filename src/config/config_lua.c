#define _POSIX_C_SOURCE 200809L

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

#include "config/config.h"
#include "config/config_embed.h"
#include "config/config_lua.h"
#include "log/log.h"
#include "server/server.h"

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
};

static struct lua_runtime *G;

static char *xstrdup(const char *s) {
	return s ? strdup(s) : NULL;
}

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
	rt->rtp[rt->n_rtp++] = xstrdup(path);
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
		SF_LOGW("pack plugin failed",
			sf_str("path", plugin),
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

	const char *bases[] = { data, STONEFLY_DATADIR, NULL };
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
	if (strcasecmp(s, "super") == 0 || strcasecmp(s, "logo") == 0
			|| strcasecmp(s, "mod4") == 0) {
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
		{ "esc", "Escape" },
		{ "enter", "Return" },
		{ "return", "Return" },
		{ "space", "space" },
		{ " ", "space" },
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
	xkb_keysym_t sym = xkb_keysym_from_name(name,
		XKB_KEYSYM_CASE_INSENSITIVE);
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
static int parse_tap_mod(const char *s, xkb_keysym_t *sym_out,
		xkb_keysym_t *sym_alt_out) {
	if (strcasecmp(s, "super") == 0 || strcasecmp(s, "mod4") == 0
			|| strcasecmp(s, "logo") == 0) {
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

static enum stonefly_action parse_action(const char *s) {
	if (s == NULL) {
		return STONEFLY_ACTION_NONE;
	}
	static const struct {
		const char *name;
		enum stonefly_action a;
	} map[] = {
		{ "none", STONEFLY_ACTION_NONE },
		{ "quit", STONEFLY_ACTION_QUIT },
		{ "close", STONEFLY_ACTION_CLOSE },
		{ "focus_next", STONEFLY_ACTION_FOCUS_NEXT },
		{ "focus_prev", STONEFLY_ACTION_FOCUS_PREV },
		{ "focus_next_all", STONEFLY_ACTION_FOCUS_NEXT_ALL },
		{ "focus_prev_all", STONEFLY_ACTION_FOCUS_PREV_ALL },
		{ "toggle_max_edges", STONEFLY_ACTION_TOGGLE_MAX_EDGES },
		{ "toggle_max_gaps", STONEFLY_ACTION_TOGGLE_MAX_GAPS },
		{ "minimize", STONEFLY_ACTION_MINIMIZE },
		{ "unminimize", STONEFLY_ACTION_UNMINIMIZE },
		{ "snap_left", STONEFLY_ACTION_SNAP_LEFT },
		{ "snap_right", STONEFLY_ACTION_SNAP_RIGHT },
		{ "snap_top", STONEFLY_ACTION_SNAP_TOP },
		{ "snap_bottom", STONEFLY_ACTION_SNAP_BOTTOM },
		{ "unsnap", STONEFLY_ACTION_UNSNAP },
		{ "spawn_foot", STONEFLY_ACTION_SPAWN_FOOT },
		{ "spawn_nautilus", STONEFLY_ACTION_SPAWN_NAUTILUS },
		{ "spawn_ghostty", STONEFLY_ACTION_SPAWN_GHOSTTY },
		{ "spawn_ghostty_qt", STONEFLY_ACTION_SPAWN_GHOSTTY_QT },
		{ "spawn_editor", STONEFLY_ACTION_SPAWN_EDITOR },
		{ "spawn_launcher", STONEFLY_ACTION_SPAWN_LAUNCHER },
		{ "scratch_toggle_term", STONEFLY_ACTION_SCRATCH_TOGGLE_TERM },
		{ "scratch_claim_term", STONEFLY_ACTION_SCRATCH_CLAIM_TERM },
		{ "exec", STONEFLY_ACTION_EXEC },
		{ "scratch_toggle", STONEFLY_ACTION_SCRATCH_TOGGLE },
		{ "scratch_claim", STONEFLY_ACTION_SCRATCH_CLAIM },
	};
	for (size_t i = 0; i < sizeof map / sizeof map[0]; i++) {
		if (strcmp(s, map[i].name) == 0) {
			return map[i].a;
		}
	}
	return STONEFLY_ACTION_NONE;
}

static uint32_t table_mods(lua_State *L, int idx) {
	uint32_t mods = 0;
	if (!lua_istable(L, idx)) {
		return 0;
	}
	lua_pushnil(L);
	while (lua_next(L, idx) != 0) {
		if (lua_isstring(L, -1)) {
			mods |= parse_mod(lua_tostring(L, -1));
		}
		lua_pop(L, 1);
	}
	return mods;
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

/*
 * bind forms (peeble/i3/sway chord strings; legacy map still accepted):
 *   bind("Super+w", { action = "spawn_ghostty" })
 *   bind({ key = "Super+Shift+Tab", action = "focus_prev" })
 *   bind({ mods = {"super"}, key = "w", action = "..." })  -- legacy
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

	int opts = 0; /* stack index of options table, or 0 */
	uint32_t mods = 0;
	xkb_keysym_t sym = XKB_KEY_NoSymbol;
	const char *chord_err = NULL;

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
		lua_getfield(L, opts, "mods");
		bool have_mods = lua_istable(L, -1);
		if (key != NULL && !have_mods) {
			/* Chord-in-key: { key = "Super+w", action = ... } */
			if (parse_chord(key, &mods, &sym) != 0) {
				chord_err = key;
			}
		} else if (key != NULL) {
			/* Legacy mods+key table; preserve case for single letters. */
			mods = table_mods(L, lua_gettop(L));
			sym = xkb_keysym_from_name(key, 0);
			if (sym == XKB_KEY_NoSymbol) {
				sym = xkb_keysym_from_name(key,
					XKB_KEYSYM_CASE_INSENSITIVE);
			}
			if (sym == XKB_KEY_NoSymbol && strlen(key) == 1) {
				sym = (xkb_keysym_t)(unsigned char)key[0];
			}
			if (sym == XKB_KEY_NoSymbol) {
				chord_err = key;
			}
		} else {
			chord_err = "(missing key)";
		}
		lua_pop(L, 2);
		if (chord_err != NULL) {
			return luaL_error(L, "bind: unknown key %s", chord_err);
		}
	}

	xkb_keysym_t sym_alt = 0;
	lua_getfield(L, opts, "key_alt");
	if (lua_isstring(L, -1)) {
		const char *alt = lua_tostring(L, -1);
		if (lookup_keysym(alt, &sym_alt) != 0) {
			sym_alt = xkb_keysym_from_name(alt,
				XKB_KEYSYM_CASE_INSENSITIVE);
		}
	}
	lua_pop(L, 1);
	maybe_tab_alt(mods, sym, &sym_alt);

	lua_getfield(L, opts, "action");
	const char *act = luaL_checkstring(L, -1);
	enum stonefly_action action = parse_action(act);
	lua_pop(L, 1);

	lua_getfield(L, opts, "cmd");
	const char *cmd = lua_isstring(L, -1) ? lua_tostring(L, -1) : NULL;

	struct stonefly_keybind *nb = realloc(cfg->keybinds,
		(cfg->n_keybinds + 1) * sizeof *nb);
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
	b->cmd = xstrdup(cmd);
	lua_pop(L, 1);
	return 0;
}

/*
 * tap forms:
 *   tap("Super", { single = "spawn_launcher", double = "scratch_toggle_term" })
 *   tap({ key = "Super", ... })           -- modifier name → L+R
 *   tap({ key = "Super_L", key_alt = "Super_R", ... })  -- legacy
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
			return luaL_error(L, "tap: expected modifier name, got %s",
				name);
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
			/* Legacy physical keysym names (Super_L, …). */
			sym = xkb_keysym_from_name(key, 0);
			if (sym == XKB_KEY_NoSymbol) {
				return luaL_error(L, "tap: unknown key %s", key);
			}
		}
	}

	lua_getfield(L, opts, "key_alt");
	if (lua_isstring(L, -1)) {
		xkb_keysym_t alt = xkb_keysym_from_name(lua_tostring(L, -1), 0);
		if (alt != XKB_KEY_NoSymbol) {
			sym_alt = alt;
		}
	}
	lua_pop(L, 1);

	lua_getfield(L, opts, "single");
	enum stonefly_action single = parse_action(
		lua_isstring(L, -1) ? lua_tostring(L, -1) : NULL);
	lua_pop(L, 1);
	lua_getfield(L, opts, "double");
	enum stonefly_action dbl = parse_action(
		lua_isstring(L, -1) ? lua_tostring(L, -1) : NULL);
	lua_pop(L, 1);

	lua_getfield(L, opts, "single_cmd");
	char *scmd_c = xstrdup(lua_isstring(L, -1) ? lua_tostring(L, -1) : NULL);
	lua_pop(L, 1);
	lua_getfield(L, opts, "double_cmd");
	char *dcmd_c = xstrdup(lua_isstring(L, -1) ? lua_tostring(L, -1) : NULL);
	lua_pop(L, 1);

	struct stonefly_tapbind *nb = realloc(cfg->tapbinds,
		(cfg->n_tapbinds + 1) * sizeof *nb);
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

	struct stonefly_scratchpad_spec *nb = realloc(cfg->scratchpads,
		(cfg->n_scratchpads + 1) * sizeof *nb);
	if (nb == NULL) {
		return luaL_error(L, "scratchpad: OOM");
	}
	cfg->scratchpads = nb;
	struct stonefly_scratchpad_spec *s = &cfg->scratchpads[cfg->n_scratchpads++];
	s->name = xstrdup(name);
	s->cmd = xstrdup(cmd);
	s->app_id = xstrdup(app);
	s->auto_hide = auto_hide;
	lua_pop(L, 4);
	return 0;
}

static void opt_set_number(struct stonefly_config *cfg, const char *k, double v) {
	if (strcmp(k, "scale") == 0) {
		cfg->output_scale = (float)v;
	} else if (strcmp(k, "cursor_size") == 0) {
		cfg->cursor_size_default = (int)v;
	} else if (strcmp(k, "gap") == 0) {
		cfg->gap_size = (int)v;
	} else if (strcmp(k, "cascade_step") == 0) {
		cfg->cascade_step = (int)v;
	} else if (strcmp(k, "cascade_wrap") == 0) {
		cfg->cascade_wrap = (int)v;
	} else if (strcmp(k, "slide_duration_ms") == 0) {
		cfg->slide_duration_ms = (int)v;
	} else if (strcmp(k, "morph_duration_ms") == 0) {
		cfg->morph_duration_ms = (int)v;
	} else if (strcmp(k, "client_opacity") == 0) {
		cfg->client_opacity = (float)v;
	} else if (strcmp(k, "corner_radius") == 0) {
		cfg->corner_radius = (int)v;
	} else if (strcmp(k, "border_thickness") == 0) {
		cfg->border_thickness = (int)v;
	} else if (strcmp(k, "shadow_blur_sigma") == 0) {
		cfg->shadow_blur_sigma = (float)v;
	} else if (strcmp(k, "bell_flash_ms") == 0) {
		cfg->bell_flash_ms = (int)v;
	} else if (strcmp(k, "snap_threshold") == 0) {
		cfg->snap_threshold = (int)v;
	} else if (strcmp(k, "snap_preview_border") == 0) {
		cfg->snap_preview_border = (int)v;
	} else if (strcmp(k, "snap_preview_opacity") == 0) {
		cfg->snap_preview_opacity = (float)v;
	} else if (strcmp(k, "tap_max_hold_ms") == 0) {
		cfg->tap_max_hold_ms = (int)v;
	} else if (strcmp(k, "double_tap_gap_ms") == 0) {
		cfg->double_tap_gap_ms = (int)v;
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
		if (strcmp(k, "slide_dir_closest") == 0) {
			cfg->slide_dir_closest = v;
		} else if (strcmp(k, "window_blur") == 0) {
			cfg->window_blur = v;
		}
		return 0;
	}
	if (lua_isnumber(L, 3)) {
		opt_set_number(cfg, k, lua_tonumber(L, 3));
		return 0;
	}
	if (lua_isstring(L, 3)) {
		const char *s = lua_tostring(L, 3);
		if (strcmp(k, "cmd_foot") == 0) {
			free(cfg->cmd_foot);
			cfg->cmd_foot = xstrdup(s);
		} else if (strcmp(k, "cmd_nautilus") == 0) {
			free(cfg->cmd_nautilus);
			cfg->cmd_nautilus = xstrdup(s);
		} else if (strcmp(k, "cmd_ghostty") == 0) {
			free(cfg->cmd_ghostty);
			cfg->cmd_ghostty = xstrdup(s);
		} else if (strcmp(k, "cmd_ghostty_qt") == 0) {
			free(cfg->cmd_ghostty_qt);
			cfg->cmd_ghostty_qt = xstrdup(s);
		} else if (strcmp(k, "cmd_editor") == 0) {
			free(cfg->cmd_editor);
			cfg->cmd_editor = xstrdup(s);
		} else if (strcmp(k, "cmd_launcher") == 0) {
			free(cfg->cmd_launcher);
			cfg->cmd_launcher = xstrdup(s);
		}
		return 0;
	}
	if (lua_istable(L, 3)) {
		float *dst = NULL;
		if (strcmp(k, "border_active") == 0) {
			dst = cfg->border_active;
		} else if (strcmp(k, "border_inactive") == 0) {
			dst = cfg->border_inactive;
		} else if (strcmp(k, "border_urgent") == 0) {
			dst = cfg->border_urgent;
		} else if (strcmp(k, "shadow_color") == 0) {
			dst = cfg->shadow_color;
		}
		if (dst != NULL) {
			for (int i = 0; i < 4; i++) {
				lua_rawgeti(L, 3, i + 1);
				dst[i] = (float)lua_tonumber(L, -1);
				lua_pop(L, 1);
			}
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

static bool eval_buffer(struct lua_runtime *rt, const char *name,
		const unsigned char *data, size_t len) {
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
	if (mkdir(dir, 0755) != 0 && errno != EEXIST) {
		/* try parents via config write helper path — best effort */
		char cmd[PATH_MAX_SF + 32];
		snprintf(cmd, sizeof cmd, "mkdir -p '%s'", dir);
		if (system(cmd) != 0) {
			return false;
		}
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
		SF_LOGE("config: failed to materialize embedded default",
			sf_str("dir", emb));
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
	if (stonefly_config_init_path(init_path, sizeof init_path) == 0
			&& access(init_path, R_OK) == 0) {
		SF_LOGI("config", sf_str("path", init_path));
		ok = luaL_dofile(G->L, init_path) == LUA_OK;
		if (!ok) {
			SF_LOGE("config user init failed; trying embedded",
				sf_str("err", lua_tostring(G->L, -1)));
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
