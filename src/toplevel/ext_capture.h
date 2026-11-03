#ifndef STONEFLY_EXT_CAPTURE_H
#define STONEFLY_EXT_CAPTURE_H

#include "server/server.h"
#include "xdg/xdg.h"

/* ext-foreign-toplevel-list + image-capture-source + image-copy-capture.
 * Parallel to zwlr foreign (taskbar); Noctalia switcher snapshots use this. */
void stonefly_ext_capture_setup(struct stonefly_server *server);
void stonefly_ext_capture_finish(struct stonefly_server *server);

void stonefly_ext_capture_on_map(struct stonefly_toplevel *toplevel);
void stonefly_ext_capture_on_unmap(struct stonefly_toplevel *toplevel);
void stonefly_ext_capture_on_title_change(struct stonefly_toplevel *toplevel);
void stonefly_ext_capture_on_app_id_change(struct stonefly_toplevel *toplevel);
void stonefly_ext_capture_on_close(struct stonefly_toplevel *toplevel);

/* Re-evaluate config capture_block_app_ids against current app_id. */
void stonefly_ext_capture_apply_block_rules(struct stonefly_toplevel *toplevel);

#endif
