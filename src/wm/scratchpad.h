#ifndef SF_SCRATCHPAD_H
#define SF_SCRATCHPAD_H

#include <stdbool.h>

#include "server/server.h"
#include "toplevel/toplevel.h"

void sf_scratchpad_init(struct sf_server *server);
void sf_scratchpad_finish(struct sf_server *server);

/* Toggle show/hide, or spawn on first use. */
void sf_scratchpad_toggle(struct sf_server *server, const char *name);

/* Associate the focused toplevel with a named pad (Super+t). */
void sf_scratchpad_claim_focused(struct sf_server *server, const char *name);

/* On map: claim a pending spawn by app_id. */
void sf_scratchpad_try_claim(struct sf_toplevel *toplevel);

/* Clear pad association when the window is destroyed. */
void sf_scratchpad_on_destroy(struct sf_toplevel *toplevel);

/*
 * Peeble Scratchpads.OnFocusChange: hide auto_hide pads when focus moves to
 * another window on the same output. Nil / other-head focus leaves them shown.
 */
void sf_scratchpad_on_focus_change(struct sf_toplevel *focused);

/* Named pads are omitted from Super+Tab (peeble Alt-Tab omit). */
bool sf_toplevel_is_scratchpad(const struct sf_toplevel *toplevel);

#endif
