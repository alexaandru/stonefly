#ifndef SF_SESSION_H
#define SF_SESSION_H

#include <stdbool.h>

struct sf_server;
struct wlr_surface;

/* ext-session-lock-v1 + ext-idle-notify-v1 + zwp-idle-inhibit-v1
 * + zwlr-output-power-management-v1 (Noctalia lock / idle / DPMS). */
void sf_session_setup(struct sf_server *server);
void sf_session_finish(struct sf_server *server);

bool sf_session_locked(const struct sf_server *server);

/* Seat activity resets idle timers (keyboard / pointer / touch). */
void sf_session_notify_activity(struct sf_server *server);

/* Recompute idle-inhibit from zwp_idle_inhibit_v1 clients. */
void sf_session_check_inhibitors(struct sf_server *server, struct wlr_surface *exclude);

#endif
