#ifndef SF_CONFIG_LUA_H
#define SF_CONFIG_LUA_H

#include <stdbool.h>

struct sf_config;

void sf_config_lua_close(struct sf_config *cfg);

/* Lua string.find(subject, pattern). Empty/NULL pattern → true; no VM → false. */
bool sf_lua_pattern_match(const struct sf_config *cfg, const char *subject, const char *pattern);

#endif
