#ifndef SF_EXT_CAPTURE_H
#define SF_EXT_CAPTURE_H

#include "server/server.h"
#include "toplevel/toplevel.h"

/* ext-foreign-toplevel-list + image-capture-source + image-copy-capture.
 * Parallel to zwlr foreign (taskbar); Noctalia switcher snapshots use this. */
void sf_ext_capture_setup(struct sf_server *server);
void sf_ext_capture_finish(struct sf_server *server);

void sf_ext_capture_on_map(struct sf_toplevel *toplevel);
void sf_ext_capture_on_unmap(struct sf_toplevel *toplevel);
void sf_ext_capture_on_title_change(struct sf_toplevel *toplevel);
void sf_ext_capture_on_app_id_change(struct sf_toplevel *toplevel);
void sf_ext_capture_on_close(struct sf_toplevel *toplevel);

/* Re-evaluate config capture_block_app_ids against current app_id. */
void sf_ext_capture_apply_block_rules(struct sf_toplevel *toplevel);

#endif
