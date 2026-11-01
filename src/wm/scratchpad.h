#ifndef SF_SCRATCHPAD_H
#define SF_SCRATCHPAD_H

#include <stdbool.h>

#include "server/server.h"
#include "toplevel/toplevel.h"

struct wlr_xdg_activation_token_v1;

void sf_scratchpad_init(struct sf_server *server);
void sf_scratchpad_finish(struct sf_server *server);

/* After a successful config reload: rebind slot→spec pointers by pad name,
 * keep live toplevel associations, orphan windows whose pad was removed. */
void sf_scratchpad_rebind(struct sf_server *server);

/* Toggle show/hide, or spawn on first use. */
void sf_scratchpad_toggle(struct sf_server *server, const char *name);

/* Toggle pad membership on the focused toplevel (Super+t): claim, or unclaim
 * into an empty slot. Claiming steals the pad from a prior occupant. */
void sf_scratchpad_claim_focused(struct sf_server *server, const char *name);

/* On map / set_app_id: claim a pending spawn (activation token or app_id). */
void sf_scratchpad_try_claim(struct sf_toplevel *toplevel);

/* On xdg-activation request_activate: claim if the token was issued for a
 * scratchpad spawn. Returns true when handled (skip urgency / focus-steal). */
bool sf_scratchpad_try_claim_activation(struct sf_toplevel *toplevel,
					struct wlr_xdg_activation_token_v1 *token);

/* Clear pad association when the window is destroyed. */
void sf_scratchpad_on_destroy(struct sf_toplevel *toplevel);

/*
 * Peeble Scratchpads.OnFocusChange: hide auto_hide pads when focus moves to
 * another window on the same output. Nil / other-head focus leaves them shown.
 */
void sf_scratchpad_on_focus_change(struct sf_toplevel *focused);

/* Named pads are omitted from Super+Tab (peeble Alt-Tab omit). */
bool sf_toplevel_is_scratchpad(const struct sf_toplevel *toplevel);

/* Hide every visible named scratchpad (overview open). */
void sf_scratchpad_hide_all(struct sf_server *server);

#endif
