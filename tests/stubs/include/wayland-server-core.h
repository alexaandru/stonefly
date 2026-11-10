/* Host-only stub for unit tests (abuild uses real wayland-server). */
#ifndef TEST_STUB_WAYLAND_SERVER_CORE_H
#define TEST_STUB_WAYLAND_SERVER_CORE_H

struct wl_list {
	struct wl_list *prev;
	struct wl_list *next;
};

#endif
