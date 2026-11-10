#ifndef SF_IPC_PARSE_H
#define SF_IPC_PARSE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Parse {"cmd":"…","arg":"…"} from an IPC request line. arg may be NULL. */
bool sf_ipc_parse_cmd_arg(const char *line, char *cmd, size_t cmd_cap, char *arg, size_t arg_cap);

/* Decimal uint64 id string → out. */
bool sf_ipc_parse_id_arg(const char *arg, uint64_t *out);

/* 1-based workspace index string → out. */
bool sf_ipc_parse_workspace_index(const char *arg, int *out);

#endif
