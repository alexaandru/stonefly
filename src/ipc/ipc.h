#ifndef STONEFLY_IPC_H
#define STONEFLY_IPC_H

#include <stdbool.h>

struct stonefly_server;

/*
 * Unix-socket control plane (peeble wire shape).
 *
 * One JSON object per line. Today: {"cmd":"windows"} → JSON array.
 *
 * Socket: $STONEFLY_SOCKET, else
 * $XDG_RUNTIME_DIR/stonefly-$WAYLAND_DISPLAY.sock.
 */

bool stonefly_ipc_start(struct stonefly_server *server);
void stonefly_ipc_stop(struct stonefly_server *server);

/* Client entry for `stonefly windows [--json]`. Returns process exit status. */
int stonefly_ipc_cmd_windows(int argc, char **argv);

#endif
