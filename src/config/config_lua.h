#ifndef STONEFLY_CONFIG_LUA_H
#define STONEFLY_CONFIG_LUA_H

#include <stdbool.h>

struct stonefly_config;

void stonefly_config_lua_close(struct stonefly_config *cfg);

/* Lua string.find(subject, pattern). Empty/NULL pattern → true; no VM → false. */
bool stonefly_lua_pattern_match(const struct stonefly_config *cfg, const char *subject,
				const char *pattern);

#endif
