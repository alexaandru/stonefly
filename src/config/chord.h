#ifndef SF_CONFIG_CHORD_H
#define SF_CONFIG_CHORD_H

#include <stdbool.h>
#include <stdint.h>
#include <xkbcommon/xkbcommon.h>

/* Peeble/i3/sway-style chord: "Super+w", "Super+Shift+Tab", "Print".
 * Returns 0 on success; -1 on unknown mod/key or empty input. */
int sf_chord_parse(const char *s, uint32_t *mods_out, xkb_keysym_t *sym_out);

/* Mouse-button chord: "Super+BTN_LEFT", "BTN_RIGHT", "Super+LMB", "mouse:272".
 * Returns 0 on success; -1 if not a mouse chord / bad token. */
int sf_chord_parse_mouse(const char *s, uint32_t *mods_out, uint32_t *button_out);

/* Resolve BTN_* / LMB / mouse:N. Returns 0 / -1. */
int sf_chord_lookup_button(const char *key, uint32_t *out);

/* True if the key token is a known mouse button name (no mods). */
bool sf_chord_is_mouse_button(const char *key);

/* Modifiers only: "Super", "Super+Shift", "ctrl+alt". Empty → 0. Returns 0 / -1. */
int sf_chord_parse_mods(const char *s, uint32_t *mods_out);

/* Lone-modifier tap names → left keysym (+ right as alt). Returns 0 / -1. */
int sf_chord_parse_tap_mod(const char *s, xkb_keysym_t *sym_out, xkb_keysym_t *sym_alt_out);

/* Shift+Tab often surfaces as ISO_Left_Tab; set *sym_alt when Shift is held. */
void sf_chord_maybe_tab_alt(uint32_t mods, xkb_keysym_t sym, xkb_keysym_t *sym_alt);

/* Resolve a key token to an xkb keysym (aliases + xkb_keysym_from_name). */
int sf_chord_lookup_keysym(const char *key, xkb_keysym_t *out);

#endif
