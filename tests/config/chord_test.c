#include "sf_test.h"
#include "config/chord.h"

#include <wlr/types/wlr_keyboard.h>

#define SF_SUPER ((uint32_t)WLR_MODIFIER_LOGO)
#define SF_ALT ((uint32_t)WLR_MODIFIER_ALT)
#define SF_CTRL ((uint32_t)WLR_MODIFIER_CTRL)
#define SF_SHIFT ((uint32_t)WLR_MODIFIER_SHIFT)

static void test_chord_parse(void) {
	uint32_t mods = 0;
	xkb_keysym_t sym = 0;

	SF_TEST_EQ_INT(sf_chord_parse(NULL, &mods, &sym), -1);
	SF_TEST_EQ_INT(sf_chord_parse("", &mods, &sym), -1);
	SF_TEST_EQ_INT(sf_chord_parse("Super+w", NULL, &sym), -1);

	SF_TEST_EQ_INT(sf_chord_parse("Print", &mods, &sym), 0);
	SF_TEST_EQ_INT((int)mods, 0);
	SF_TEST_EQ_INT((int)sym, (int)XKB_KEY_Print);

	SF_TEST_EQ_INT(sf_chord_parse("Super+w", &mods, &sym), 0);
	SF_TEST_EQ_INT((int)mods, (int)SF_SUPER);
	SF_TEST_EQ_INT((int)sym, (int)'w');

	SF_TEST_EQ_INT(sf_chord_parse("Super+Shift+Tab", &mods, &sym), 0);
	SF_TEST_EQ_INT((int)mods, (int)(SF_SUPER | SF_SHIFT));
	SF_TEST_EQ_INT((int)sym, (int)XKB_KEY_Tab);

	SF_TEST_EQ_INT(sf_chord_parse("ctrl + alt + esc", &mods, &sym), 0);
	SF_TEST_EQ_INT((int)mods, (int)(SF_CTRL | SF_ALT));
	SF_TEST_EQ_INT((int)sym, (int)XKB_KEY_Escape);

	SF_TEST_EQ_INT(sf_chord_parse("Super+NotAKey", &mods, &sym), -1);
	SF_TEST_EQ_INT(sf_chord_parse("Hyper+w", &mods, &sym), -1);
}

static void test_tap_mod_and_tab_alt(void) {
	xkb_keysym_t sym = 0, alt = 0;

	SF_TEST_EQ_INT(sf_chord_parse_tap_mod("super", &sym, &alt), 0);
	SF_TEST_EQ_INT((int)sym, (int)XKB_KEY_Super_L);
	SF_TEST_EQ_INT((int)alt, (int)XKB_KEY_Super_R);

	SF_TEST_EQ_INT(sf_chord_parse_tap_mod("control", &sym, &alt), 0);
	SF_TEST_EQ_INT((int)sym, (int)XKB_KEY_Control_L);

	SF_TEST_EQ_INT(sf_chord_parse_tap_mod("nope", &sym, &alt), -1);

	alt = 0;
	sf_chord_maybe_tab_alt(SF_SHIFT, XKB_KEY_Tab, &alt);
	SF_TEST_EQ_INT((int)alt, (int)XKB_KEY_ISO_Left_Tab);

	alt = 0;
	sf_chord_maybe_tab_alt(0, XKB_KEY_Tab, &alt);
	SF_TEST_EQ_INT((int)alt, 0);
}

static void test_lookup_keysym(void) {
	xkb_keysym_t sym = 0;

	SF_TEST_EQ_INT(sf_chord_lookup_keysym("q", &sym), 0);
	SF_TEST_EQ_INT((int)sym, (int)'q');
	SF_TEST_EQ_INT(sf_chord_lookup_keysym("enter", &sym), 0);
	SF_TEST_EQ_INT((int)sym, (int)XKB_KEY_Return);
	SF_TEST_EQ_INT(sf_chord_lookup_keysym(" ", &sym), 0);
	SF_TEST_EQ_INT((int)sym, (int)XKB_KEY_space);
	SF_TEST_EQ_INT(sf_chord_lookup_keysym("", &sym), -1);
	SF_TEST_EQ_INT(sf_chord_lookup_keysym("NopeKey", &sym), -1);
}

static const struct sf_test tests[] = {
	{"sf_chord_parse", test_chord_parse},
	{"sf_chord_tap_tab", test_tap_mod_and_tab_alt},
	{"sf_chord_lookup_keysym", test_lookup_keysym},
};

int main(void) {
	return sf_test_run(tests, sizeof(tests) / sizeof(tests[0])) == 0 ? 0 : 1;
}
