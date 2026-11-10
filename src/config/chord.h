#ifndef SF_CONFIG_CHORD_H
#define SF_CONFIG_CHORD_H

#include <stdint.h>
#include <xkbcommon/xkbcommon.h>

/* Peeble/i3/sway-style chord: "Super+w", "Super+Shift+Tab", "Print".
 * Returns 0 on success; -1 on unknown mod/key or empty input. */
int sf_chord_parse(const char *s, uint32_t *mods_out, xkb_keysym_t *sym_out);

/* Lone-modifier tap names → left keysym (+ right as alt). Returns 0 / -1. */
int sf_chord_parse_tap_mod(const char *s, xkb_keysym_t *sym_out, xkb_keysym_t *sym_alt_out);

/* Shift+Tab often surfaces as ISO_Left_Tab; set *sym_alt when Shift is held. */
void sf_chord_maybe_tab_alt(uint32_t mods, xkb_keysym_t sym, xkb_keysym_t *sym_alt);

/* Resolve a key token to an xkb keysym (aliases + xkb_keysym_from_name). */
int sf_chord_lookup_keysym(const char *key, xkb_keysym_t *out);

#endif
