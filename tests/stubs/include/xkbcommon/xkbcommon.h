/* Host-only stub for unit tests (abuild uses real libxkbcommon). */
#ifndef TEST_STUB_XKBCOMMON_H
#define TEST_STUB_XKBCOMMON_H

#include <stddef.h>
#include <stdint.h>
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

static inline xkb_keysym_t xkb_keysym_from_name(const char *name, int flags) {
	(void)flags;
	if (name == NULL || name[0] == '\0') {
		return XKB_KEY_NoSymbol;
	}
	static const struct {
		const char *n;
		xkb_keysym_t sym;
	} table[] = {
		{"Escape", XKB_KEY_Escape},
		{"Return", XKB_KEY_Return},
		{"Tab", XKB_KEY_Tab},
		{"space", XKB_KEY_space},
		{"Print", XKB_KEY_Print},
		{"ISO_Left_Tab", XKB_KEY_ISO_Left_Tab},
	};
	for (size_t i = 0; i < sizeof table / sizeof table[0]; i++) {
		if (strcasecmp(name, table[i].n) == 0) {
			return table[i].sym;
		}
	}
	return XKB_KEY_NoSymbol;
}

#endif
