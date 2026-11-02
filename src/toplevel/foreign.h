#ifndef STONEFLY_FOREIGN_H
#define STONEFLY_FOREIGN_H

#include "server/server.h"
#include "xdg/xdg.h"

void stonefly_foreign_toplevel_setup(struct stonefly_server *server);
void stonefly_foreign_toplevel_finish(struct stonefly_server *server);

/* Bridge compositor state into zwlr_foreign_toplevel_management_v1. */
void stonefly_foreign_toplevel_on_map(struct stonefly_toplevel *toplevel);
void stonefly_foreign_toplevel_on_unmap(struct stonefly_toplevel *toplevel);
void stonefly_foreign_toplevel_on_title_change(struct stonefly_toplevel *toplevel);
void stonefly_foreign_toplevel_on_app_id_change(struct stonefly_toplevel *toplevel);
void stonefly_foreign_toplevel_on_parent_change(struct stonefly_toplevel *toplevel);
void stonefly_foreign_toplevel_on_state_change(struct stonefly_toplevel *toplevel);
void stonefly_foreign_toplevel_on_outputs_change(struct stonefly_toplevel *toplevel);
void stonefly_foreign_toplevel_on_close(struct stonefly_toplevel *toplevel);

#endif
