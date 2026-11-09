#ifndef STONEFLY_SCRATCHPAD_H
#define STONEFLY_SCRATCHPAD_H

#include <stdbool.h>

#include "server/server.h"
#include "toplevel/toplevel.h"

void stonefly_scratchpad_init(struct stonefly_server *server);
void stonefly_scratchpad_finish(struct stonefly_server *server);

/* Toggle show/hide, or spawn on first use. */
void stonefly_scratchpad_toggle(struct stonefly_server *server, const char *name);

/* Associate the focused toplevel with a named pad (Super+t). */
void stonefly_scratchpad_claim_focused(struct stonefly_server *server, const char *name);

/* On map: claim a pending spawn by app_id. */
void stonefly_scratchpad_try_claim(struct stonefly_toplevel *toplevel);

/* Clear pad association when the window is destroyed. */
void stonefly_scratchpad_on_destroy(struct stonefly_toplevel *toplevel);

/*
 * Peeble Scratchpads.OnFocusChange: hide auto_hide pads when focus moves to
 * another window on the same output. Nil / other-head focus leaves them shown.
 */
void stonefly_scratchpad_on_focus_change(struct stonefly_toplevel *focused);

/* Named pads are omitted from Super+Tab (peeble Alt-Tab omit). */
bool stonefly_toplevel_is_scratchpad(const struct stonefly_toplevel *toplevel);

#endif
