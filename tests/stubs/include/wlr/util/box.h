/* Host-only stub for unit tests (abuild uses real wlroots). */
#ifndef TEST_STUB_WLR_UTIL_BOX_H
#define TEST_STUB_WLR_UTIL_BOX_H

struct wlr_box {
	int x, y;
	int width, height;
};

#endif
