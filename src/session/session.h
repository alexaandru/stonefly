#ifndef STONEFLY_SESSION_H
#define STONEFLY_SESSION_H

#include <stdbool.h>

struct stonefly_server;
struct wlr_surface;

/* ext-session-lock-v1 + ext-idle-notify-v1 + zwp-idle-inhibit-v1
 * + zwlr-output-power-management-v1 (Noctalia lock / idle / DPMS). */
void stonefly_session_setup(struct stonefly_server *server);
void stonefly_session_finish(struct stonefly_server *server);

bool stonefly_session_locked(const struct stonefly_server *server);

/* Seat activity resets idle timers (keyboard / pointer / touch). */
void stonefly_session_notify_activity(struct stonefly_server *server);

/* Recompute idle-inhibit from zwp_idle_inhibit_v1 clients. */
void stonefly_session_check_inhibitors(struct stonefly_server *server, struct wlr_surface *exclude);

#endif
