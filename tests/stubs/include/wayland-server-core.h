/* Unit-test stub: drive.py -I's this ahead of system headers (host + APK check).
 * The compositor Meson build uses real wayland-server; unit suites do not. */
#ifndef TEST_STUB_WAYLAND_SERVER_CORE_H
#define TEST_STUB_WAYLAND_SERVER_CORE_H

struct wl_list {
	struct wl_list *prev;
	struct wl_list *next;
};

#endif
