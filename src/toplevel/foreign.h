#ifndef SF_FOREIGN_H
#define SF_FOREIGN_H

#include "server/server.h"
#include "toplevel/toplevel.h"

void sf_foreign_toplevel_setup(struct sf_server *server);
void sf_foreign_toplevel_finish(struct sf_server *server);

/* Bridge compositor state into zwlr_foreign_toplevel_management_v1. */
void sf_foreign_toplevel_on_map(struct sf_toplevel *toplevel);
void sf_foreign_toplevel_on_unmap(struct sf_toplevel *toplevel);
void sf_foreign_toplevel_on_title_change(struct sf_toplevel *toplevel);
void sf_foreign_toplevel_on_app_id_change(struct sf_toplevel *toplevel);
void sf_foreign_toplevel_on_parent_change(struct sf_toplevel *toplevel);
void sf_foreign_toplevel_on_state_change(struct sf_toplevel *toplevel);
void sf_foreign_toplevel_on_outputs_change(struct sf_toplevel *toplevel);
void sf_foreign_toplevel_on_close(struct sf_toplevel *toplevel);

#endif
