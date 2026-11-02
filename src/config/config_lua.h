#ifndef STONEFLY_CONFIG_LUA_H
#define STONEFLY_CONFIG_LUA_H

#include <stdbool.h>

struct stonefly_server;
struct stonefly_config;

void stonefly_config_lua_close(struct stonefly_config *cfg);
bool stonefly_config_load(struct stonefly_server *server);

#endif
