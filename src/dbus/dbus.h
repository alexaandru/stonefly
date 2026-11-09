#ifndef SF_DBUS_H
#define SF_DBUS_H

struct sf_server;

/*
 * Session-bus helpers via sd-bus (libelogind / libsystemd).
 * Failures are logged; none of these abort the compositor.
 */

/* Push the process environment into D-Bus activation (WAYLAND_DISPLAY, …). */
void sf_dbus_update_activation_environment(void);

/*
 * If server->config_warn is set: wait (≤20s) for org.freedesktop.Notifications
 * via NameOwnerChanged, then Notify. Bail on the toast only if nothing claims
 * the name. Non-blocking — hooks the Wayland event loop.
 */
void sf_dbus_notify_config_warn(struct sf_server *server);

/* Cancel a pending config-warn wait (safe if none). */
void sf_dbus_finish(struct sf_server *server);

#endif
