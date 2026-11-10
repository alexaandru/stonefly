/* WM module-private decls shared across src/wm/*.c. */
#ifndef SF_WM_INTERNAL_H
#define SF_WM_INTERNAL_H

#include <stdbool.h>

struct sf_server;
struct sf_toplevel;

void sf_close_toplevel(struct sf_toplevel *toplevel);
void sf_spawn_cmd(const char *cmd);
void sf_focus_cycle(struct sf_server *server, bool backward, bool include_minimized);
void sf_unminimize_most_recent(struct sf_server *server);

#endif
