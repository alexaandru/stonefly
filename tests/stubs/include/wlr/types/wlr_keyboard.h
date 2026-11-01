/* Unit-test stub: drive.py -I's this ahead of system headers (host + APK check).
 * The compositor Meson build uses real wlroots; unit suites do not. */
#ifndef TEST_STUB_WLR_KEYBOARD_H
#define TEST_STUB_WLR_KEYBOARD_H

#define WLR_MODIFIER_SHIFT (1 << 0)
#define WLR_MODIFIER_CAPS (1 << 1)
#define WLR_MODIFIER_CTRL (1 << 2)
#define WLR_MODIFIER_ALT (1 << 3)
#define WLR_MODIFIER_MOD2 (1 << 4)
#define WLR_MODIFIER_MOD3 (1 << 5)
#define WLR_MODIFIER_LOGO (1 << 6)
#define WLR_MODIFIER_MOD5 (1 << 7)

#endif
