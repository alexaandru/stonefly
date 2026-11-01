#ifndef SF_SERVER_SECURITY_CONTEXT_H
#define SF_SERVER_SECURITY_CONTEXT_H

#include <stdbool.h>

/*
 * Return true iff a Wayland global interface is safe to expose to sandboxed
 * clients (connected via wp_security_context_v1). Privileged shell, capture,
 * input-injection, and nested security-context globals stay hidden.
 */
bool sf_security_context_global_allowed(const char *interface_name);

#endif
