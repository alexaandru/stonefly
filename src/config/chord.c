#include "config/chord.h"

#include <ctype.h>
#include <string.h>
#include <strings.h>
#include <wlr/types/wlr_keyboard.h>

#define SF_SUPER ((uint32_t)WLR_MODIFIER_LOGO)
#define SF_ALT ((uint32_t)WLR_MODIFIER_ALT)
#define SF_CTRL ((uint32_t)WLR_MODIFIER_CTRL)
#define SF_SHIFT ((uint32_t)WLR_MODIFIER_SHIFT)

static uint32_t parse_mod(const char *s) {
	if (strcasecmp(s, "super") == 0 || strcasecmp(s, "logo") == 0 ||
	    strcasecmp(s, "mod4") == 0) {
		return SF_SUPER;
	}
	if (strcasecmp(s, "shift") == 0) {
		return SF_SHIFT;
	}
	if (strcasecmp(s, "ctrl") == 0 || strcasecmp(s, "control") == 0) {
		return SF_CTRL;
	}
	if (strcasecmp(s, "alt") == 0 || strcasecmp(s, "mod1") == 0) {
		return SF_ALT;
	}
	return 0;
}

/* Trim leading/trailing ASCII space in-place; returns start. */
static char *trim_inplace(char *s) {
	while (*s != '\0' && isspace((unsigned char)*s)) {
		s++;
	}
	if (*s == '\0') {
		return s;
	}
	char *end = s + strlen(s) - 1;
	while (end > s && isspace((unsigned char)*end)) {
		*end-- = '\0';
	}
	return s;
}

int sf_chord_lookup_keysym(const char *key, xkb_keysym_t *out) {
	static const struct {
		const char *alias;
		const char *xkb;
	} aliases[] = {
		{"esc", "Escape"},  {"enter", "Return"}, {"return", "Return"},
		{"space", "space"}, {" ", "space"},
	};
	size_t len = strlen(key);
	if (len == 0) {
		return -1;
	}
	if (len == 1) {
		*out = (xkb_keysym_t)(unsigned char)key[0];
		return 0;
	}
	const char *name = key;
	for (size_t i = 0; i < sizeof aliases / sizeof aliases[0]; i++) {
		if (strcasecmp(key, aliases[i].alias) == 0) {
			name = aliases[i].xkb;
			break;
		}
	}
	xkb_keysym_t sym = xkb_keysym_from_name(name, XKB_KEYSYM_CASE_INSENSITIVE);
	if (sym == XKB_KEY_NoSymbol) {
		return -1;
	}
	*out = sym;
	return 0;
}

int sf_chord_parse(const char *s, uint32_t *mods_out, xkb_keysym_t *sym_out) {
	if (s == NULL || mods_out == NULL || sym_out == NULL) {
		return -1;
	}
	char buf[128];
	size_t n = strlen(s);
	if (n == 0 || n >= sizeof buf) {
		return -1;
	}
	memcpy(buf, s, n + 1);

	char *last_plus = strrchr(buf, '+');
	char *key;
	uint32_t mods = 0;
	if (last_plus == NULL) {
		key = trim_inplace(buf);
	} else {
		*last_plus = '\0';
		key = trim_inplace(last_plus + 1);
		char *p = buf;
		while (p != NULL && *p != '\0') {
			char *plus = strchr(p, '+');
			if (plus != NULL) {
				*plus = '\0';
			}
			char *tok = trim_inplace(p);
			if (*tok != '\0') {
				uint32_t bit = parse_mod(tok);
				if (bit == 0) {
					return -1;
				}
				mods |= bit;
			}
			p = plus != NULL ? plus + 1 : NULL;
		}
	}
	if (sf_chord_lookup_keysym(key, sym_out) != 0) {
		return -1;
	}
	*mods_out = mods;
	return 0;
}

int sf_chord_parse_tap_mod(const char *s, xkb_keysym_t *sym_out, xkb_keysym_t *sym_alt_out) {
	if (s == NULL || sym_out == NULL || sym_alt_out == NULL) {
		return -1;
	}
	if (strcasecmp(s, "super") == 0 || strcasecmp(s, "mod4") == 0 ||
	    strcasecmp(s, "logo") == 0) {
		*sym_out = XKB_KEY_Super_L;
		*sym_alt_out = XKB_KEY_Super_R;
		return 0;
	}
	if (strcasecmp(s, "alt") == 0 || strcasecmp(s, "mod1") == 0) {
		*sym_out = XKB_KEY_Alt_L;
		*sym_alt_out = XKB_KEY_Alt_R;
		return 0;
	}
	if (strcasecmp(s, "shift") == 0) {
		*sym_out = XKB_KEY_Shift_L;
		*sym_alt_out = XKB_KEY_Shift_R;
		return 0;
	}
	if (strcasecmp(s, "ctrl") == 0 || strcasecmp(s, "control") == 0) {
		*sym_out = XKB_KEY_Control_L;
		*sym_alt_out = XKB_KEY_Control_R;
		return 0;
	}
	return -1;
}

void sf_chord_maybe_tab_alt(uint32_t mods, xkb_keysym_t sym, xkb_keysym_t *sym_alt) {
	if (sym_alt == NULL) {
		return;
	}
	if (*sym_alt == 0 && sym == XKB_KEY_Tab && (mods & SF_SHIFT)) {
		*sym_alt = XKB_KEY_ISO_Left_Tab;
	}
}
