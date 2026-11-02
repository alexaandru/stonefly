#ifndef STONEFLY_DBUS_H
#define STONEFLY_DBUS_H

struct stonefly_server;

/*
 * Session-bus helpers via sd-bus (libelogind / libsystemd).
 * Failures are logged; none of these abort the compositor.
 */

/* Push the process environment into D-Bus activation (WAYLAND_DISPLAY, …). */
void stonefly_dbus_update_activation_environment(void);

/*
 * If server->config_warn is set: wait (≤20s) for org.freedesktop.Notifications
 * via NameOwnerChanged, then Notify. Bail on the toast only if nothing claims
 * the name. Non-blocking — hooks the Wayland event loop.
 */
void stonefly_dbus_notify_config_warn(struct stonefly_server *server);

/* Cancel a pending config-warn wait (safe if none). */
void stonefly_dbus_finish(struct stonefly_server *server);

#endif
