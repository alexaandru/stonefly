#ifndef STONEFLY_IPC_CONFIG_H
#define STONEFLY_IPC_CONFIG_H

#include <stddef.h>
#include <stdio.h>

struct stonefly_server;

/*
 * Encode live server->config as JSON.
 * section NULL or "" → full object with keys:
 *   options, binds, taps, scratchpads, window_rules, inputs, outputs
 * section one of those keys → that value only.
 * Returns bytes written (excl. NUL), or -1 on error / unknown section.
 */
int stonefly_config_to_json(struct stonefly_server *server, const char *section, char *buf,
			    size_t cap);

/* Human-readable dump of a config JSON object/array/value. */
void stonefly_config_print_json(const char *json, FILE *fp);

#endif
