#ifndef SF_UTIL_SPAWN_H
#define SF_UTIL_SPAWN_H

/*
 * User-configured process spawn (CLI -s, sf.exec, EXEC actions, scratchpad cmd).
 * Internal plumbing must not shell out — use in-process helpers instead.
 */

/* SIGCHLD reaper via waitpid(WNOHANG). Prefer over SIG_IGN so children that
 * libraries exec keep default SIGCHLD (wlroots Xwayland / xkbcomp). */
void sf_spawn_install_reaper(void);

/* fork + execl("/bin/sh", "-c", cmd). Fire-and-forget; empty cmd is a no-op. */
void sf_spawn_cmd(const char *cmd);

/* Like sf_spawn_cmd, but the child also gets XDG_ACTIVATION_TOKEN and
 * DESKTOP_STARTUP_ID set to `token` (cleared when token is NULL/empty). */
void sf_spawn_cmd_activation(const char *cmd, const char *token);

#endif
