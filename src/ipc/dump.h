#ifndef SF_IPC_DUMP_H
#define SF_IPC_DUMP_H

#include <stddef.h>

struct sf_server;

/*
 * Kitchen-sink compositor snapshot for debug and integration asserts.
 * One JSON object (versioned). Returns bytes written excl. NUL, or -1.
 */
int sf_dump_to_json(struct sf_server *server, char *buf, size_t cap);

#endif
