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
#include "config/chord.h"
#include "config/config.h"
#include "config/config_embed.h"
#include "config/config_lua.h"
#include "log/log.h"
#include "server/server.h"
#include "ssd/ssd_policy.h"
#include "util/shell_quote.h"
#include "util/str.h"
#include "wm/workspace.h"

#ifndef SF_DATADIR
#define SF_DATADIR "/usr/share/stonefly"
#endif

#define RTP_MAX 32
#define PATH_MAX_SF 512

struct lua_runtime {
	lua_State *L;
	struct sf_server *server;
	char *rtp[RTP_MAX];
	size_t n_rtp;
	bool binds_replaced;
	bool taps_replaced;
	bool scratches_replaced;
	bool rules_replaced;
	bool execs_replaced;
	bool inputs_replaced;
	unsigned rule_seq;
};

static struct lua_runtime *G;

void sf_config_lua_close(struct sf_config *cfg) {
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
	rt->rtp[rt->n_rtp++] = sf_xstrdup(path);
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
		rc = sf_config_dir(buf, sizeof buf);
	} else if (strcmp(which, "data") == 0) {
		rc = sf_data_dir(buf, sizeof buf);
	} else if (strcmp(which, "cache") == 0) {
		rc = sf_cache_dir(buf, sizeof buf);
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
	sf_data_dir(data, sizeof data);

	const char *bases[] = {data, SF_DATADIR, NULL};
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

static void clear_keybinds(struct sf_config *cfg) {
	for (size_t i = 0; i < cfg->n_keybinds; i++) {
		sf_action_steps_free(cfg->keybinds[i].steps, cfg->keybinds[i].n_steps);
	}
	free(cfg->keybinds);
	cfg->keybinds = NULL;
	cfg->n_keybinds = 0;
}

static void clear_tapbinds(struct sf_config *cfg) {
	for (size_t i = 0; i < cfg->n_tapbinds; i++) {
		sf_action_steps_free(cfg->tapbinds[i].single_steps,
				     cfg->tapbinds[i].n_single_steps);
		sf_action_steps_free(cfg->tapbinds[i].double_steps,
				     cfg->tapbinds[i].n_double_steps);
	}
	free(cfg->tapbinds);
	cfg->tapbinds = NULL;
	cfg->n_tapbinds = 0;
}

static void clear_scratchpads(struct sf_config *cfg) {
	for (size_t i = 0; i < cfg->n_scratchpads; i++) {
		free(cfg->scratchpads[i].name);
		free(cfg->scratchpads[i].cmd);
		free(cfg->scratchpads[i].app_id);
	}
	free(cfg->scratchpads);
	cfg->scratchpads = NULL;
	cfg->n_scratchpads = 0;
}

static void clear_window_rules(struct sf_config *cfg) {
	for (size_t i = 0; i < cfg->n_window_rules; i++) {
		free(cfg->window_rules[i].app_id);
		free(cfg->window_rules[i].title);
		sf_action_steps_free(cfg->window_rules[i].steps, cfg->window_rules[i].n_steps);
	}
	free(cfg->window_rules);
	cfg->window_rules = NULL;
	cfg->n_window_rules = 0;
}

static void clear_execs(struct sf_config *cfg) {
	for (size_t i = 0; i < cfg->n_execs; i++) {
		free(cfg->execs[i].cmd);
	}
	free(cfg->execs);
	cfg->execs = NULL;
	cfg->n_execs = 0;
}

static void clear_input_rules(struct sf_config *cfg) {
	for (size_t i = 0; i < cfg->n_input_rules; i++) {
		free(cfg->input_rules[i].match);
	}
	free(cfg->input_rules);
	cfg->input_rules = NULL;
	cfg->n_input_rules = 0;
}

static void parse_input_bool(lua_State *L, const char *key, int *dst) {
	if (!lua_isboolean(L, -1)) {
		luaL_error(L, "input: %s must be a boolean", key);
	}
	*dst = lua_toboolean(L, -1) ? 1 : 0;
}

/*
 * sf.input(match, opts)
 * match: "keyboard"|"pointer"|"mouse"|"touchpad"|"touch" or name substring.
 * opts: tap_to_click, natural_scroll, left_handed, dwt (bool);
 *       accel_speed (-1..1), accel_profile ("none"|"flat"|"adaptive");
 *       repeat_rate / repeat_delay (non-negative int, keyboards).
 * Later rules override earlier ones for the same key. Applied on device add.
 */
static int l_input(lua_State *L) {
	if (lua_gettop(L) != 2) {
		return luaL_error(L, "input expects (match, opts)");
	}
	if (G == NULL) {
		return luaL_error(L, "input: no runtime");
	}
	struct sf_config *cfg = &G->server->config;
	if (!G->inputs_replaced) {
		clear_input_rules(cfg);
		G->inputs_replaced = true;
	}
	if (cfg->n_input_rules >= SF_INPUT_RULE_MAX) {
		return luaL_error(L, "input: max %d", SF_INPUT_RULE_MAX);
	}

	const char *match = luaL_checkstring(L, 1);
	if (match[0] == '\0') {
		return luaL_error(L, "input: match must be non-empty");
	}
	luaL_checktype(L, 2, LUA_TTABLE);

	struct sf_input_rule rule = {
		.tap_to_click = -1,
		.natural_scroll = -1,
		.left_handed = -1,
		.dwt = -1,
		.accel_profile = SF_INPUT_ACCEL_UNSET,
	};

	lua_pushnil(L);
	while (lua_next(L, 2) != 0) {
		if (!lua_isstring(L, -2)) {
			return luaL_error(L, "input: opts keys must be strings");
		}
		const char *k = lua_tostring(L, -2);
		if (strcmp(k, "tap_to_click") == 0) {
			parse_input_bool(L, k, &rule.tap_to_click);
		} else if (strcmp(k, "natural_scroll") == 0) {
			parse_input_bool(L, k, &rule.natural_scroll);
		} else if (strcmp(k, "left_handed") == 0) {
			parse_input_bool(L, k, &rule.left_handed);
		} else if (strcmp(k, "dwt") == 0) {
			parse_input_bool(L, k, &rule.dwt);
		} else if (strcmp(k, "accel_speed") == 0) {
			if (!lua_isnumber(L, -1)) {
				return luaL_error(L, "input: accel_speed must be a number");
			}
			double v = lua_tonumber(L, -1);
			if (v < -1.0 || v > 1.0) {
				return luaL_error(L, "input: accel_speed must be in [-1, 1]");
			}
			rule.accel_speed_set = true;
			rule.accel_speed = v;
		} else if (strcmp(k, "accel_profile") == 0) {
			const char *p = lua_tostring(L, -1);
			if (p == NULL) {
				return luaL_error(L, "input: accel_profile must be a string");
			}
			if (strcasecmp(p, "none") == 0) {
				rule.accel_profile = SF_INPUT_ACCEL_NONE;
			} else if (strcasecmp(p, "flat") == 0) {
				rule.accel_profile = SF_INPUT_ACCEL_FLAT;
			} else if (strcasecmp(p, "adaptive") == 0) {
				rule.accel_profile = SF_INPUT_ACCEL_ADAPTIVE;
			} else {
				return luaL_error(
					L, "input: accel_profile must be none|flat|adaptive");
			}
		} else if (strcmp(k, "repeat_rate") == 0) {
			if (!lua_isnumber(L, -1)) {
				return luaL_error(L, "input: repeat_rate must be an integer");
			}
			int n = (int)lua_tonumber(L, -1);
			if (n < 0) {
				return luaL_error(L, "input: repeat_rate must not be negative");
			}
			rule.repeat_rate_set = true;
			rule.repeat_rate = n;
		} else if (strcmp(k, "repeat_delay") == 0) {
			if (!lua_isnumber(L, -1)) {
				return luaL_error(L, "input: repeat_delay must be an integer");
			}
			int n = (int)lua_tonumber(L, -1);
			if (n < 0) {
				return luaL_error(L, "input: repeat_delay must not be negative");
			}
			rule.repeat_delay_set = true;
			rule.repeat_delay = n;
		} else {
			return luaL_error(L, "input: unknown setting %s", k);
		}
		lua_pop(L, 1);
	}

	rule.match = sf_xstrdup(match);
	if (rule.match == NULL) {
		return luaL_error(L, "input: OOM");
	}

	struct sf_input_rule *nb = realloc(cfg->input_rules, (cfg->n_input_rules + 1) * sizeof *nb);
	if (nb == NULL) {
		free(rule.match);
		return luaL_error(L, "input: OOM");
	}
	cfg->input_rules = nb;
	cfg->input_rules[cfg->n_input_rules++] = rule;
	return 0;
}

/*
 * sf.exec(cmd...) [ {:reload bool} ]
 * One string → sh -c as-is (pipes/redir ok). Several strings → shell-quoted
 * argv join. Trailing table opts; :reload default false (once per start).
 */
static int l_exec(lua_State *L) {
	int narg = lua_gettop(L);
	if (narg < 1) {
		return luaL_error(L, "exec: need command string(s) [, {:reload bool}]");
	}
	if (G == NULL) {
		return luaL_error(L, "exec: no runtime");
	}
	struct sf_config *cfg = &G->server->config;
	if (!G->execs_replaced) {
		clear_execs(cfg);
		G->execs_replaced = true;
	}
	if (cfg->n_execs >= SF_EXEC_MAX) {
		return luaL_error(L, "exec: max %d", SF_EXEC_MAX);
	}

	bool reload = false;
	int nstr = narg;
	if (lua_istable(L, narg)) {
		nstr = narg - 1;
		if (nstr < 1) {
			return luaL_error(L, "exec: need command string(s) before opts table");
		}
		lua_pushnil(L);
		while (lua_next(L, narg) != 0) {
			if (!lua_isstring(L, -2)) {
				return luaL_error(L, "exec: opts keys must be strings");
			}
			const char *k = lua_tostring(L, -2);
			if (strcmp(k, "reload") == 0) {
				if (!lua_isboolean(L, -1)) {
					return luaL_error(L, "exec: opts.reload must be a bool");
				}
				reload = lua_toboolean(L, -1);
			} else {
				return luaL_error(L, "exec: unknown opts key %s", k);
			}
			lua_pop(L, 1);
		}
	}

	for (int i = 1; i <= nstr; i++) {
		if (!lua_isstring(L, i)) {
			return luaL_error(L, "exec: argument %d must be a string", i);
		}
	}

	char *cmd = NULL;
	if (nstr == 1) {
		cmd = sf_xstrdup(lua_tostring(L, 1));
	} else {
		/* Quote-join argv; grow with realloc so bounds stay local. */
		size_t len = 0;
		cmd = malloc(1);
		if (cmd == NULL) {
			return luaL_error(L, "exec: out of memory");
		}
		cmd[0] = '\0';
		for (int i = 1; i <= nstr; i++) {
			char *q = sf_shell_quote_dup(lua_tostring(L, i));
			if (q == NULL) {
				free(cmd);
				return luaL_error(L, "exec: out of memory");
			}
			size_t qlen = strlen(q);
			size_t add = qlen + (i > 1 ? 1 : 0);
			char *nb = realloc(cmd, len + add + 1);
			if (nb == NULL) {
				free(q);
				free(cmd);
				return luaL_error(L, "exec: out of memory");
			}
			cmd = nb;
			if (i > 1) {
				cmd[len++] = ' ';
			}
			memcpy(cmd + len, q, qlen + 1);
			len += qlen;
			free(q);
		}
	}
	if (cmd == NULL) {
		return luaL_error(L, "exec: out of memory");
	}

	struct sf_exec *nb = realloc(cfg->execs, (cfg->n_execs + 1) * sizeof *nb);
	if (nb == NULL) {
		free(cmd);
		return luaL_error(L, "exec: out of memory");
	}
	cfg->execs = nb;
	cfg->execs[cfg->n_execs].cmd = cmd;
	cfg->execs[cfg->n_execs].reload = reload;
	cfg->n_execs++;
	return 0;
}

/*
 * Parse one action step from a Lua value:
 *   "maximize" | {:action "exec" :cmd "…"}
 * Returns 0 and fills *step, or luaL_error.
 */
static int parse_action_step_value(lua_State *L, int idx, struct sf_action_step *step) {
	memset(step, 0, sizeof *step);
	if (lua_type(L, idx) == LUA_TSTRING) {
		const char *name = lua_tostring(L, idx);
		if (!sf_action_parse_ok(name, &step->action)) {
			return luaL_error(L, "unknown action %s", name);
		}
		return 0;
	}
	if (!lua_istable(L, idx)) {
		return luaL_error(L, "action step must be a string or table");
	}
	lua_getfield(L, idx, "action");
	const char *name = lua_isstring(L, -1) ? lua_tostring(L, -1) : NULL;
	if (name == NULL || !sf_action_parse_ok(name, &step->action)) {
		lua_pop(L, 1);
		return luaL_error(L, "action step: missing/unknown :action");
	}
	lua_pop(L, 1);
	lua_getfield(L, idx, "cmd");
	if (lua_isstring(L, -1)) {
		step->cmd = sf_xstrdup(lua_tostring(L, -1));
	}
	lua_pop(L, 1);
	return 0;
}

/* :action string | sf.chain table → heap steps. Empty/nil → n=0. */
static int parse_action_field(lua_State *L, int opts, const char *field,
			      struct sf_action_step **out, size_t *out_n) {
	*out = NULL;
	*out_n = 0;
	lua_getfield(L, opts, field);
	int idx = lua_gettop(L);
	if (lua_isnil(L, idx)) {
		lua_pop(L, 1);
		return 0;
	}
	if (lua_type(L, idx) == LUA_TSTRING) {
		struct sf_action_step step;
		if (parse_action_step_value(L, idx, &step) != 0) {
			return lua_error(L); /* already pushed */
		}
		lua_pop(L, 1);
		if (!sf_action_steps_one(step.action, step.cmd, out, out_n)) {
			free(step.cmd);
			return luaL_error(L, "action: OOM");
		}
		free(step.cmd);
		return 0;
	}
	if (!lua_istable(L, idx)) {
		lua_pop(L, 1);
		return luaL_error(L, "%s must be a string or chain table", field);
	}
	lua_getfield(L, idx, "__sf_chain");
	bool is_chain = lua_toboolean(L, -1);
	lua_pop(L, 1);
	if (!is_chain) {
		/* Single step table {:action :cmd} */
		struct sf_action_step step;
		if (parse_action_step_value(L, idx, &step) != 0) {
			return lua_error(L);
		}
		lua_pop(L, 1);
		if (!sf_action_steps_one(step.action, step.cmd, out, out_n)) {
			free(step.cmd);
			return luaL_error(L, "action: OOM");
		}
		free(step.cmd);
		return 0;
	}
	size_t n = (size_t)lua_objlen(L, idx);
	if (n == 0) {
		lua_pop(L, 1);
		return 0;
	}
	struct sf_action_step *steps = calloc(n, sizeof *steps);
	if (steps == NULL) {
		lua_pop(L, 1);
		return luaL_error(L, "action: OOM");
	}
	for (size_t i = 0; i < n; i++) {
		lua_rawgeti(L, idx, (int)i + 1);
		if (parse_action_step_value(L, -1, &steps[i]) != 0) {
			sf_action_steps_free(steps, i);
			return lua_error(L);
		}
		lua_pop(L, 1);
	}
	lua_pop(L, 1);
	*out = steps;
	*out_n = n;
	return 0;
}

/* sf.chain(...) → marked table of normalized step tables. */
static int l_chain(lua_State *L) {
	int narg = lua_gettop(L);
	if (narg < 1) {
		return luaL_error(L, "chain: need at least one step");
	}
	lua_createtable(L, narg, 1);
	lua_pushboolean(L, 1);
	lua_setfield(L, -2, "__sf_chain");
	for (int i = 1; i <= narg; i++) {
		struct sf_action_step step;
		if (parse_action_step_value(L, i, &step) != 0) {
			return lua_error(L);
		}
		lua_createtable(L, 0, 2);
		lua_pushstring(L, sf_action_name(step.action));
		lua_setfield(L, -2, "action");
		if (step.cmd != NULL) {
			lua_pushstring(L, step.cmd);
			lua_setfield(L, -2, "cmd");
			free(step.cmd);
		}
		lua_rawseti(L, -2, i);
	}
	return 1;
}

bool sf_lua_pattern_match(const struct sf_config *cfg, const char *subject, const char *pattern) {
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

static const struct sf_window_rule *find_window_rule(const struct sf_config *cfg, const char *id) {
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

static int parse_rule_match(lua_State *L, int idx, struct sf_window_rule *out) {
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
			out->app_id = sf_xstrdup(s);
		} else if (strcmp(k, "title") == 0) {
			const char *s = lua_tostring(L, -1);
			if (s == NULL) {
				return luaL_error(L, "window_rule: match.title must be a string");
			}
			check_lua_pattern(L, s, "title");
			free(out->title);
			out->title = sf_xstrdup(s);
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

static int parse_rule_actions(lua_State *L, int idx, struct sf_window_rule *out) {
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
		} else if (strcmp(k, "always_on_top") == 0) {
			if (!lua_isboolean(L, -1)) {
				return luaL_error(
					L, "window_rule: actions.always_on_top must be a bool");
			}
			out->always_on_top = lua_toboolean(L, -1);
			out->always_on_top_set = true;
		} else if (strcmp(k, "ssd") == 0) {
			const char *s = lua_tostring(L, -1);
			if (s == NULL) {
				return luaL_error(L, "window_rule: actions.ssd must be a string");
			}
			out->ssd_mode = sf_ssd_mode_parse(s);
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
		} else if (strcmp(k, "action") == 0) {
			/* Parsed after the loop via parse_action_field on the actions table. */
		} else {
			return luaL_error(L, "window_rule: unknown actions key %s", k);
		}
		lua_pop(L, 1);
	}
	return 0;
}

static void merge_window_rule(struct sf_window_rule *dst, const struct sf_window_rule *parent,
			      const struct sf_window_rule *child) {
	*dst = (struct sf_window_rule){0};
	snprintf(dst->id, sizeof dst->id, "%s", child->id);
	snprintf(dst->parent_id, sizeof dst->parent_id, "%s", parent->id);

	dst->app_id = parent->app_id != NULL ? sf_xstrdup(parent->app_id) : NULL;
	dst->title = parent->title != NULL ? sf_xstrdup(parent->title) : NULL;
	if (child->app_id != NULL && child->app_id[0] != '\0') {
		free(dst->app_id);
		dst->app_id = sf_xstrdup(child->app_id);
	}
	if (child->title != NULL && child->title[0] != '\0') {
		free(dst->title);
		dst->title = sf_xstrdup(child->title);
	}

	dst->center = parent->center;
	dst->center_set = parent->center_set;
	dst->float_win = parent->float_win;
	dst->float_set = parent->float_set;
	dst->borderless = parent->borderless;
	dst->borderless_set = parent->borderless_set;
	dst->always_on_top = parent->always_on_top;
	dst->always_on_top_set = parent->always_on_top_set;
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
	if (child->always_on_top_set) {
		dst->always_on_top = child->always_on_top;
		dst->always_on_top_set = true;
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
	if (child->n_steps > 0) {
		if (!sf_action_steps_dup(child->steps, child->n_steps, &dst->steps,
					 &dst->n_steps)) {
			dst->steps = NULL;
			dst->n_steps = 0;
		}
	} else if (parent->n_steps > 0) {
		if (!sf_action_steps_dup(parent->steps, parent->n_steps, &dst->steps,
					 &dst->n_steps)) {
			dst->steps = NULL;
			dst->n_steps = 0;
		}
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
	struct sf_config *cfg = &G->server->config;
	if (!G->rules_replaced) {
		clear_window_rules(cfg);
		G->rules_replaced = true;
		G->rule_seq = 0;
	}
	if (cfg->n_window_rules >= SF_WINDOW_RULE_MAX) {
		return luaL_error(L, "window_rule: max %d", SF_WINDOW_RULE_MAX);
	}

	struct sf_window_rule raw = {0};
	parse_rule_match(L, 1, &raw);
	parse_rule_actions(L, 2, &raw);
	parse_action_field(L, 2, "action", &raw.steps, &raw.n_steps);

	/* Parent id only — realloc may move window_rules; re-resolve after. */
	char parent_id[40] = {0};
	if (narg == 3 && !lua_isnil(L, 3)) {
		const char *pid = lua_tostring(L, 3);
		if (pid == NULL || pid[0] == '\0') {
			free(raw.app_id);
			free(raw.title);
			sf_action_steps_free(raw.steps, raw.n_steps);
			return luaL_error(L, "window_rule: parent must be a rule tag string");
		}
		if (find_window_rule(cfg, pid) == NULL) {
			free(raw.app_id);
			free(raw.title);
			sf_action_steps_free(raw.steps, raw.n_steps);
			return luaL_error(L, "window_rule: unknown parent tag %s", pid);
		}
		snprintf(parent_id, sizeof parent_id, "%s", pid);
	}

	G->rule_seq++;
	snprintf(raw.id, sizeof raw.id, "rule-%u", G->rule_seq);

	struct sf_window_rule *nb =
		realloc(cfg->window_rules, (cfg->n_window_rules + 1) * sizeof *nb);
	if (nb == NULL) {
		free(raw.app_id);
		free(raw.title);
		sf_action_steps_free(raw.steps, raw.n_steps);
		return luaL_error(L, "window_rule: OOM");
	}
	cfg->window_rules = nb;
	struct sf_window_rule *dst = &cfg->window_rules[cfg->n_window_rules];

	if (parent_id[0] != '\0') {
		const struct sf_window_rule *parent = find_window_rule(cfg, parent_id);
		if (parent == NULL) {
			free(raw.app_id);
			free(raw.title);
			sf_action_steps_free(raw.steps, raw.n_steps);
			return luaL_error(L, "window_rule: unknown parent tag %s", parent_id);
		}
		merge_window_rule(dst, parent, &raw);
		free(raw.app_id);
		free(raw.title);
		sf_action_steps_free(raw.steps, raw.n_steps);
	} else {
		*dst = raw;
		raw.app_id = NULL;
		raw.title = NULL;
		raw.steps = NULL;
		raw.n_steps = 0;
	}

	bool has_match = (dst->app_id != NULL && dst->app_id[0] != '\0') ||
			 (dst->title != NULL && dst->title[0] != '\0');
	if (!has_match) {
		free(dst->app_id);
		free(dst->title);
		sf_action_steps_free(dst->steps, dst->n_steps);
		dst->steps = NULL;
		dst->n_steps = 0;
		return luaL_error(L, "window_rule: match needs app_id or title");
	}
	cfg->n_window_rules++;
	lua_pushstring(L, dst->id);
	return 1;
}

/*
 * bind forms:
 *   bind("Super+w", { action = "exec", cmd = "..." })
 *   bind("Super+x", { action = (sf.chain "maximize" ...) })
 *   bind({ key = "Super+Shift+Tab", action = "focus_prev" })  -- chord-in-key
 * Optional: key_alt. Shift+Tab auto-adds ISO_Left_Tab as key_alt.
 * Legacy :cmd next to a string :action still works (one-step with cmd).
 */
static int l_bind(lua_State *L) {
	if (G == NULL) {
		return luaL_error(L, "bind: no runtime");
	}
	struct sf_config *cfg = &G->server->config;
	if (!G->binds_replaced) {
		clear_keybinds(cfg);
		G->binds_replaced = true;
	}

	int opts = 0; /* stack index of options table */
	uint32_t mods = 0;
	xkb_keysym_t sym = XKB_KEY_NoSymbol;

	if (lua_type(L, 1) == LUA_TSTRING) {
		const char *chord = lua_tostring(L, 1);
		if (sf_chord_parse(chord, &mods, &sym) != 0) {
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
		if (sf_chord_parse(key, &mods, &sym) != 0) {
			lua_pop(L, 1);
			return luaL_error(L, "bind: unknown key %s", key);
		}
		lua_pop(L, 1);
	}

	xkb_keysym_t sym_alt = 0;
	lua_getfield(L, opts, "key_alt");
	if (lua_isstring(L, -1)) {
		const char *alt = lua_tostring(L, -1);
		if (sf_chord_lookup_keysym(alt, &sym_alt) != 0) {
			sym_alt = xkb_keysym_from_name(alt, XKB_KEYSYM_CASE_INSENSITIVE);
		}
	}
	lua_pop(L, 1);
	sf_chord_maybe_tab_alt(mods, sym, &sym_alt);

	struct sf_action_step *steps = NULL;
	size_t n_steps = 0;
	parse_action_field(L, opts, "action", &steps, &n_steps);
	if (n_steps == 0) {
		return luaL_error(L, "bind: missing action");
	}
	/* Legacy: string action + sibling :cmd attaches cmd to the sole step. */
	if (n_steps == 1 && steps[0].cmd == NULL) {
		lua_getfield(L, opts, "cmd");
		if (lua_isstring(L, -1)) {
			steps[0].cmd = sf_xstrdup(lua_tostring(L, -1));
		}
		lua_pop(L, 1);
	}

	struct sf_keybind *nb = realloc(cfg->keybinds, (cfg->n_keybinds + 1) * sizeof *nb);
	if (nb == NULL) {
		sf_action_steps_free(steps, n_steps);
		return luaL_error(L, "bind: OOM");
	}
	cfg->keybinds = nb;
	struct sf_keybind *b = &cfg->keybinds[cfg->n_keybinds++];
	memset(b, 0, sizeof *b);
	b->mods = mods;
	b->sym = sym;
	b->sym_alt = sym_alt;
	b->steps = steps;
	b->n_steps = n_steps;
	return 0;
}

/*
 * tap forms (modifier name only; L+R via parse_tap_mod):
 *   tap("Super", { single = "exec", single_cmd = "…", double = "scratch_toggle", … })
 *   tap({ key = "Super", single = (sf.chain …), … })
 * :single / :double accept string, step table, or sf.chain (same as :action).
 */
static int l_tap(lua_State *L) {
	if (G == NULL) {
		return luaL_error(L, "tap: no runtime");
	}
	struct sf_config *cfg = &G->server->config;
	if (!G->taps_replaced) {
		clear_tapbinds(cfg);
		G->taps_replaced = true;
	}

	int opts = 0;
	xkb_keysym_t sym = XKB_KEY_NoSymbol;
	xkb_keysym_t sym_alt = 0;

	if (lua_type(L, 1) == LUA_TSTRING) {
		const char *name = lua_tostring(L, 1);
		if (sf_chord_parse_tap_mod(name, &sym, &sym_alt) != 0) {
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
		if (sf_chord_parse_tap_mod(key, &sym, &sym_alt) != 0) {
			return luaL_error(L, "tap: expected modifier name, got %s", key);
		}
	}

	struct sf_action_step *single_steps = NULL;
	size_t n_single = 0;
	struct sf_action_step *double_steps = NULL;
	size_t n_double = 0;
	parse_action_field(L, opts, "single", &single_steps, &n_single);
	parse_action_field(L, opts, "double", &double_steps, &n_double);
	if (n_single == 1 && single_steps[0].cmd == NULL) {
		lua_getfield(L, opts, "single_cmd");
		if (lua_isstring(L, -1)) {
			single_steps[0].cmd = sf_xstrdup(lua_tostring(L, -1));
		}
		lua_pop(L, 1);
	}
	if (n_double == 1 && double_steps != NULL && double_steps[0].cmd == NULL) {
		lua_getfield(L, opts, "double_cmd");
		if (lua_isstring(L, -1)) {
			double_steps[0].cmd = sf_xstrdup(lua_tostring(L, -1));
		}
		lua_pop(L, 1);
	}

	struct sf_tapbind *nb = realloc(cfg->tapbinds, (cfg->n_tapbinds + 1) * sizeof *nb);
	if (nb == NULL) {
		sf_action_steps_free(single_steps, n_single);
		sf_action_steps_free(double_steps, n_double);
		return luaL_error(L, "tap: OOM");
	}
	cfg->tapbinds = nb;
	struct sf_tapbind *t = &cfg->tapbinds[cfg->n_tapbinds++];
	memset(t, 0, sizeof *t);
	t->sym = sym;
	t->sym_alt = sym_alt;
	t->single_steps = single_steps;
	t->n_single_steps = n_single;
	t->double_steps = double_steps;
	t->n_double_steps = n_double;
	return 0;
}

static int l_scratchpad(lua_State *L) {
	luaL_checktype(L, 1, LUA_TTABLE);
	if (G == NULL) {
		return luaL_error(L, "scratchpad: no runtime");
	}
	struct sf_config *cfg = &G->server->config;
	if (!G->scratches_replaced) {
		clear_scratchpads(cfg);
		G->scratches_replaced = true;
	}
	if (cfg->n_scratchpads >= SF_SCRATCHPAD_MAX) {
		return luaL_error(L, "scratchpad: max %d", SF_SCRATCHPAD_MAX);
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

	struct sf_scratchpad_spec *nb =
		realloc(cfg->scratchpads, (cfg->n_scratchpads + 1) * sizeof *nb);
	if (nb == NULL) {
		return luaL_error(L, "scratchpad: OOM");
	}
	cfg->scratchpads = nb;
	struct sf_scratchpad_spec *s = &cfg->scratchpads[cfg->n_scratchpads++];
	s->name = sf_xstrdup(name);
	s->cmd = sf_xstrdup(cmd);
	s->app_id = sf_xstrdup(app);
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
	void (*set)(struct sf_config *cfg, double v);
};

static void opt_set_ssd_height(struct sf_config *cfg, double v) {
	int h = (int)v;
	cfg->ssd_height = h < 16 ? 16 : h;
}

static void opt_set_ssd_font_size(struct sf_config *cfg, double v) {
	int s = (int)v;
	if (s < 8) {
		s = 8;
	}
	if (s > 72) {
		s = 72;
	}
	cfg->ssd_font_size = s;
}

static void opt_set_resize_border_in(struct sf_config *cfg, double v) {
	int n = (int)v;
	cfg->resize_border_in = n < 0 ? 0 : n;
}

static void opt_set_resize_border_out(struct sf_config *cfg, double v) {
	int n = (int)v;
	cfg->resize_border_out = n < 0 ? 0 : n;
}

static const struct opt_num opt_numbers[] = {
	{"scale", OPT_NUM_FLOAT, offsetof(struct sf_config, output_scale), NULL},
	{"cursor_size", OPT_NUM_INT, offsetof(struct sf_config, cursor_size_default), NULL},
	{"gap", OPT_NUM_INT, offsetof(struct sf_config, gap_size), NULL},
	{"cascade_step", OPT_NUM_INT, offsetof(struct sf_config, cascade_step), NULL},
	{"cascade_wrap", OPT_NUM_INT, offsetof(struct sf_config, cascade_wrap), NULL},
	{"slide_duration_ms", OPT_NUM_INT, offsetof(struct sf_config, slide_duration_ms), NULL},
	{"morph_duration_ms", OPT_NUM_INT, offsetof(struct sf_config, morph_duration_ms), NULL},
	{"client_opacity", OPT_NUM_FLOAT, offsetof(struct sf_config, client_opacity), NULL},
	{"inactive_opacity", OPT_NUM_FLOAT, offsetof(struct sf_config, inactive_opacity), NULL},
	{"corner_radius", OPT_NUM_INT, offsetof(struct sf_config, corner_radius), NULL},
	{"border_thickness", OPT_NUM_INT, offsetof(struct sf_config, border_thickness), NULL},
	{"shadow_blur_sigma", OPT_NUM_FLOAT, offsetof(struct sf_config, shadow_blur_sigma), NULL},
	{"blur_num_passes", OPT_NUM_INT, offsetof(struct sf_config, blur_num_passes), NULL},
	{"blur_radius", OPT_NUM_INT, offsetof(struct sf_config, blur_radius), NULL},
	{"blur_noise", OPT_NUM_FLOAT, offsetof(struct sf_config, blur_noise), NULL},
	{"blur_brightness", OPT_NUM_FLOAT, offsetof(struct sf_config, blur_brightness), NULL},
	{"blur_contrast", OPT_NUM_FLOAT, offsetof(struct sf_config, blur_contrast), NULL},
	{"blur_saturation", OPT_NUM_FLOAT, offsetof(struct sf_config, blur_saturation), NULL},
	{"bell_flash_ms", OPT_NUM_INT, offsetof(struct sf_config, bell_flash_ms), NULL},
	{"snap_threshold", OPT_NUM_INT, offsetof(struct sf_config, snap_threshold), NULL},
	{"snap_preview_border", OPT_NUM_INT, offsetof(struct sf_config, snap_preview_border), NULL},
	{"snap_preview_opacity", OPT_NUM_FLOAT, offsetof(struct sf_config, snap_preview_opacity),
	 NULL},
	{"tap_max_hold_ms", OPT_NUM_INT, offsetof(struct sf_config, tap_max_hold_ms), NULL},
	{"double_tap_gap_ms", OPT_NUM_INT, offsetof(struct sf_config, double_tap_gap_ms), NULL},
	{"swipe_workspace_threshold", OPT_NUM_INT,
	 offsetof(struct sf_config, swipe_workspace_threshold), NULL},
	{"nudge_step", OPT_NUM_INT, offsetof(struct sf_config, nudge_step), NULL},
	{"ssd_height", OPT_NUM_FN, 0, opt_set_ssd_height},
	{"ssd_font_size", OPT_NUM_FN, 0, opt_set_ssd_font_size},
	{"resize_border_in", OPT_NUM_FN, 0, opt_set_resize_border_in},
	{"resize_border_out", OPT_NUM_FN, 0, opt_set_resize_border_out},
	{"master_factor", OPT_NUM_FLOAT, offsetof(struct sf_config, master_factor), NULL},
	{"master_count", OPT_NUM_INT, offsetof(struct sf_config, master_count), NULL},
	{"column_ratio", OPT_NUM_FLOAT, offsetof(struct sf_config, column_ratio), NULL},
};

static const struct {
	const char *name;
	size_t off;
} opt_bools[] = {
	{"slide_dir_closest", offsetof(struct sf_config, slide_dir_closest)},
	{"window_blur", offsetof(struct sf_config, window_blur)},
	{"blur_xray", offsetof(struct sf_config, blur_xray)},
	{"swipe_workspace_natural", offsetof(struct sf_config, swipe_workspace_natural)},
	{"cursor_follows_focus", offsetof(struct sf_config, cursor_follows_focus)},
};

static const struct {
	const char *name;
	size_t off; /* struct sf_rgba (contiguous float r,g,b,a) */
} opt_colors[] = {
	{"border_active", offsetof(struct sf_config, border_active)},
	{"border_inactive", offsetof(struct sf_config, border_inactive)},
	{"border_urgent", offsetof(struct sf_config, border_urgent)},
	{"shadow_color", offsetof(struct sf_config, shadow_color)},
};

static void opt_set_number(struct sf_config *cfg, const char *k, double v) {
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
	struct sf_config *cfg = &G->server->config;
	if (lua_isboolean(L, 3)) {
		int v = lua_toboolean(L, 3);
		for (size_t i = 0; i < sizeof opt_bools / sizeof opt_bools[0]; i++) {
			if (strcmp(k, opt_bools[i].name) == 0) {
				*(bool *)((char *)cfg + opt_bools[i].off) = v != 0;
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
			sf_replace_str(&cfg->capture_block_app_ids, s);
		} else if (strcmp(k, "arrange") == 0 || strcmp(k, "layout") == 0) {
			/* layout is a deprecated alias for arrange. */
			cfg->arrange = sf_arrange_parse(s);
		} else if (strcmp(k, "master_pos") == 0) {
			cfg->master_pos = sf_master_pos_parse(s);
		} else if (strcmp(k, "ssd_mode") == 0) {
			cfg->ssd_mode = sf_ssd_mode_parse(s);
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
			struct sf_rgba *color = (struct sf_rgba *)((char *)cfg + opt_colors[i].off);
			float *dst = sf_rgba_vec_mut(color);
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

static void push_sf_table(struct lua_runtime *rt) {
	lua_State *L = rt->L;
	lua_newtable(L); /* sf */

	lua_pushcfunction(L, l_stdpath);
	lua_setfield(L, -2, "stdpath");
	lua_pushcfunction(L, l_runtime_file);
	lua_setfield(L, -2, "runtime_file");
	lua_pushcfunction(L, l_fs_mtime);
	lua_setfield(L, -2, "_fs_mtime");
	lua_pushcfunction(L, l_packadd);
	lua_setfield(L, -2, "packadd");
	lua_pushcfunction(L, l_chain);
	lua_setfield(L, -2, "chain");
	lua_pushcfunction(L, l_bind);
	lua_setfield(L, -2, "bind");
	lua_pushcfunction(L, l_tap);
	lua_setfield(L, -2, "tap");
	lua_pushcfunction(L, l_scratchpad);
	lua_setfield(L, -2, "scratchpad");
	lua_pushcfunction(L, l_window_rule);
	lua_setfield(L, -2, "window_rule");
	lua_pushcfunction(L, l_exec);
	lua_setfield(L, -2, "exec");
	lua_pushcfunction(L, l_input);
	lua_setfield(L, -2, "input");

	/* sf.opt with __newindex */
	lua_newtable(L); /* opt */
	lua_newtable(L); /* mt */
	lua_pushcfunction(L, l_opt_newindex);
	lua_setfield(L, -2, "__newindex");
	lua_setmetatable(L, -2);
	lua_setfield(L, -2, "opt");

	lua_setglobal(L, "sf");
}

static void setup_rtp(struct lua_runtime *rt) {
	char config[PATH_MAX_SF], data[PATH_MAX_SF];
	if (sf_config_dir(config, sizeof config) == 0) {
		rtp_add(rt, config);
	}
	if (sf_data_dir(data, sizeof data) == 0) {
		rtp_add(rt, data);
	}
	rtp_add(rt, SF_DATADIR "/runtime");
	rtp_add(rt, SF_DATADIR);

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
	if (sf_mkdir_p(dir) != 0) {
		return false;
	}
	for (size_t i = 0; i < sf_default_files_len; i++) {
		const struct sf_embed_file *f = &sf_default_files[i];
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
	if (sf_cache_dir(cache, sizeof cache) != 0) {
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

	const struct sf_embed_file *init = &sf_default_files[0];
	return eval_buffer(rt, "=embedded:init.lua", init->data, init->len);
}

static bool lua_boot(struct sf_server *server) {
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
	push_sf_table(rt);
	setup_rtp(rt);
	return true;
}

bool sf_config_load(struct sf_server *server) {
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
	if (sf_config_init_path(init_path, sizeof init_path) == 0 && access(init_path, R_OK) == 0) {
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
			sf_config_lua_close(&server->config);
			sf_config_finish(&server->config);
			sf_config_set_defaults(&server->config);
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
		sf_config_lua_close(&server->config);
		sf_config_finish(&server->config);
		sf_config_set_defaults(&server->config);
		return true; /* compositor still starts */
	}
	return true;
}
