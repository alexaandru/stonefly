/* Host-only stub for unit tests (abuild uses real wlroots headers). */
#ifndef TEST_STUB_WLR_XDG_DECORATION_V1_H
#define TEST_STUB_WLR_XDG_DECORATION_V1_H

enum wlr_xdg_toplevel_decoration_v1_mode {
	WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_NONE = 0,
	WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE = 1,
	WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE = 2,
};

#endif
