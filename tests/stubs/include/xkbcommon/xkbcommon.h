/* Unit-test stub: drive.py -I's this ahead of system headers (host + APK check).
 * The compositor Meson build uses real libxkbcommon; unit suites do not. */
#ifndef TEST_STUB_XKBCOMMON_H
#define TEST_STUB_XKBCOMMON_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <strings.h>

typedef uint32_t xkb_keysym_t;

#define XKB_KEY_NoSymbol 0
#define XKB_KEYSYM_CASE_INSENSITIVE (1 << 0)

/* Enough keysyms for chord unit tests (values match common xkb ranges). */
#define XKB_KEY_Tab 0xff09
#define XKB_KEY_ISO_Left_Tab 0xfe20
#define XKB_KEY_Escape 0xff1b
#define XKB_KEY_Return 0xff0d
#define XKB_KEY_space 0x20
#define XKB_KEY_Print 0xff61
#define XKB_KEY_Super_L 0xffeb
#define XKB_KEY_Super_R 0xffec
#define XKB_KEY_Alt_L 0xffe9
#define XKB_KEY_Alt_R 0xffea
#define XKB_KEY_Shift_L 0xffe1
#define XKB_KEY_Shift_R 0xffe2
#define XKB_KEY_Control_L 0xffe3
#define XKB_KEY_Control_R 0xffe4
#define XKB_KEY_1 0x31
#define XKB_KEY_2 0x32
#define XKB_KEY_3 0x33
#define XKB_KEY_4 0x34
#define XKB_KEY_5 0x35
#define XKB_KEY_6 0x36
#define XKB_KEY_7 0x37
#define XKB_KEY_8 0x38
#define XKB_KEY_9 0x39
#define XKB_KEY_q 0x71
#define XKB_KEY_slash 0x2f
#define XKB_KEY_Left 0xff51
#define XKB_KEY_Right 0xff53
#define XKB_KEY_Up 0xff52
#define XKB_KEY_Down 0xff54

static inline int xkb_keysym_get_name(xkb_keysym_t sym, char *buffer, size_t size) {
	if (buffer == NULL || size == 0) {
		return -1;
	}
	char tmp[16];
	const char *name = tmp;
	if (sym >= XKB_KEY_1 && sym <= XKB_KEY_9) {
		tmp[0] = (char)('1' + (sym - XKB_KEY_1));
		tmp[1] = '\0';
	} else if (sym >= (xkb_keysym_t)'a' && sym <= (xkb_keysym_t)'z') {
		tmp[0] = (char)sym;
		tmp[1] = '\0';
	} else {
		switch (sym) {
		case XKB_KEY_slash:
			name = "slash";
			break;
		case XKB_KEY_Left:
			name = "Left";
			break;
		case XKB_KEY_Right:
			name = "Right";
			break;
		case XKB_KEY_Up:
			name = "Up";
			break;
		case XKB_KEY_Down:
			name = "Down";
			break;
		case XKB_KEY_Escape:
			name = "Escape";
			break;
		case XKB_KEY_Tab:
			name = "Tab";
			break;
		case XKB_KEY_space:
			name = "space";
			break;
		case XKB_KEY_Return:
			name = "Return";
			break;
		default:
			name = "unknown";
			break;
		}
	}
	size_t n = strlen(name);
	if (n + 1 > size) {
		n = size - 1;
	}
	memcpy(buffer, name, n);
	buffer[n] = '\0';
	return (int)n;
}

static inline xkb_keysym_t xkb_keysym_from_name(const char *name, int flags) {
	(void)flags;
	if (name == NULL || name[0] == '\0') {
		return XKB_KEY_NoSymbol;
	}
	static const struct {
		const char *n;
		xkb_keysym_t sym;
	} table[] = {
		{"Escape", XKB_KEY_Escape}, {"Return", XKB_KEY_Return},
		{"Tab", XKB_KEY_Tab},	    {"space", XKB_KEY_space},
		{"Print", XKB_KEY_Print},   {"ISO_Left_Tab", XKB_KEY_ISO_Left_Tab},
	};
	for (size_t i = 0; i < sizeof table / sizeof table[0]; i++) {
		if (strcasecmp(name, table[i].n) == 0) {
			return table[i].sym;
		}
	}
	return XKB_KEY_NoSymbol;
}

#endif
