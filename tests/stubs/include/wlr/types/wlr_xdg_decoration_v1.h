/* Unit-test stub: drive.py -I's this ahead of system headers (host + APK check).
 * The compositor Meson build uses real wlroots; unit suites do not. */
#ifndef TEST_STUB_WLR_XDG_DECORATION_V1_H
#define TEST_STUB_WLR_XDG_DECORATION_V1_H

enum wlr_xdg_toplevel_decoration_v1_mode {
	WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_NONE = 0,
	WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE = 1,
	WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE = 2,
};

#endif
