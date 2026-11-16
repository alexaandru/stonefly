/* Workspace overview — Umbriel-shaped filmstrip (thin v1). */
#ifndef SF_OVERVIEW_H
#define SF_OVERVIEW_H

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#include "wm/arrange_math.h"

struct sf_server;
struct sf_toplevel;
struct sf_workspace;

/* True while presentation is up (open, opening, or closing zoom). */
bool sf_overview_active(struct sf_server *server);
/* True while open and not already closing — pointer edits still apply. */
bool sf_overview_interactive(struct sf_server *server);

void sf_overview_toggle(struct sf_server *server);
void sf_overview_open(struct sf_server *server);
void sf_overview_close(struct sf_server *server);
/* Instant teardown (session lock, output loss). */
void sf_overview_force_close(struct sf_server *server);

/*
 * 4-finger swipe. progress is rubber-bandable slightly outside [0, 1].
 * gesture_end commits open or close from the release.
 */
void sf_overview_gesture_update(struct sf_server *server, double progress);
void sf_overview_gesture_end(struct sf_server *server, bool commit_open);

/* Continuous filmstrip pan (dx/dy layout px; natural applied by caller). */
void sf_overview_pan(struct sf_server *server, double dx, double dy);
/* Discrete step: up|down|left|right (workspace row or ~80 px sideways). */
void sf_overview_scroll(struct sf_server *server, const char *dir);
/* Focus the highlighted window and close. */
void sf_overview_confirm(struct sf_server *server);
/* Cycle card selection across all workspaces (Super+Tab while overview open). */
void sf_overview_focus_cycle(struct sf_server *server, bool backward);

/* Pointer: left-click card → focus + close; empty → close. Returns true if consumed. */
bool sf_overview_handle_button(struct sf_server *server, uint32_t button, bool pressed, double lx,
			       double ly);

/* Bind/gesture filter context: OVERVIEW while interactive, else workspace arrange. */
enum sf_arrange sf_bind_context_arrange(struct sf_server *server);

/* Keep cards in sync when windows appear/leave while open. */
void sf_overview_on_toplevel_mapped(struct sf_toplevel *toplevel);
void sf_overview_on_toplevel_unmapped(struct sf_toplevel *toplevel);
/* Re-layout filmstrip after workspace switch while open. */
void sf_overview_relayout(struct sf_server *server);

void sf_overview_tick(struct sf_server *server, const struct timespec *now);
bool sf_overview_anim_busy(struct sf_server *server);

#endif
