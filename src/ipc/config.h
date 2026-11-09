#ifndef SF_IPC_CONFIG_H
#define SF_IPC_CONFIG_H

#include <stddef.h>
#include <stdio.h>

struct sf_server;

/*
 * Encode live server->config as JSON.
 * section NULL or "" → full object with keys:
 *   options, binds, taps, scratchpads, window_rules, inputs, outputs
 * section one of those keys → that value only.
 * Returns bytes written (excl. NUL), or -1 on error / unknown section.
 */
int sf_config_to_json(struct sf_server *server, const char *section, char *buf, size_t cap);

/* Human-readable dump of a config JSON object/array/value. */
void sf_config_print_json(const char *json, FILE *fp);

#endif
