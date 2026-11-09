#ifndef STONEFLY_IPC_H
#define STONEFLY_IPC_H

#include <stdbool.h>

struct stonefly_server;

/*
 * Unix-socket control plane (peeble wire shape).
 *
 * One JSON object per line:
 *   {"cmd":"windows"}                 → JSON array of windows
 *   {"cmd":"layers"}                  → JSON array of layer-shell surfaces
 *   {"cmd":"outputs"}                 → JSON array of outputs
 *   {"cmd":"workspaces"}              → JSON array of workspaces
 *   {"cmd":"workspace","arg":"next|prev|N"} → {"ok":true}
 *   {"cmd":"move-to-workspace","arg":"next|prev|N"} → {"ok":true}
 *   {"cmd":"focus","arg":"<id>"}      → {"ok":true}
 *   {"cmd":"close","arg":"<id>"}      → {"ok":true}
 *   {"cmd":"block-capture","arg":"<id>"}   → {"ok":true}
 *   {"cmd":"unblock-capture","arg":"<id>"} → {"ok":true}
 *   {"cmd":"set-urgent","arg":"<id>"}      → {"ok":true}
 *   {"cmd":"unset-urgent","arg":"<id>"}    → {"ok":true}
 *   {"cmd":"toggle-quick-terminal"}   → {"ok":true} (pad "term")
 *   {"cmd":"config"}                  → full live config JSON object
 *   {"cmd":"config","arg":"binds"}    → one section (options|binds|taps|
 *                                       scratchpads|window_rules|cmds|
 *                                       inputs|outputs)
 *
 * Socket: $STONEFLY_SOCKET, else
 * $XDG_RUNTIME_DIR/stonefly-$WAYLAND_DISPLAY.sock.
 */

bool stonefly_ipc_start(struct stonefly_server *server);
void stonefly_ipc_stop(struct stonefly_server *server);

/* Client entries. Return process exit status. */
int stonefly_ipc_cmd_windows(int argc, char **argv);
int stonefly_ipc_cmd_layers(int argc, char **argv);
int stonefly_ipc_cmd_outputs(int argc, char **argv);
int stonefly_ipc_cmd_workspaces(int argc, char **argv);
int stonefly_ipc_cmd_config(int argc, char **argv);
int stonefly_ipc_cmd_focus(int argc, char **argv);
int stonefly_ipc_cmd_close(int argc, char **argv);
int stonefly_ipc_cmd_block_capture(int argc, char **argv);
int stonefly_ipc_cmd_unblock_capture(int argc, char **argv);
int stonefly_ipc_cmd_set_urgent(int argc, char **argv);
int stonefly_ipc_cmd_unset_urgent(int argc, char **argv);
int stonefly_ipc_cmd_toggle_quick_terminal(int argc, char **argv);
/* Terminate the compositor that owns STONEFLY_SOCKET / WAYLAND_DISPLAY. */
int stonefly_ipc_cmd_exit(int argc, char **argv);

#endif
