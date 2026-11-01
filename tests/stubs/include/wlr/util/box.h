/* Unit-test stub: drive.py -I's this ahead of system headers (host + APK check).
 * The compositor Meson build uses real wlroots; unit suites do not. */
#ifndef TEST_STUB_WLR_UTIL_BOX_H
#define TEST_STUB_WLR_UTIL_BOX_H

struct wlr_box {
	int x, y;
	int width, height;
};

#endif
