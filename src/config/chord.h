#ifndef SF_CONFIG_CHORD_H
#define SF_CONFIG_CHORD_H

#include <stdbool.h>
#include <stdint.h>
#include <xkbcommon/xkbcommon.h>

/* sf.bind "Super+WheelUp" — axis notch (Umbriel WheelUp/Down/Left/Right). */
enum sf_wheel_dir {
	SF_WHEEL_UP = 0,
	SF_WHEEL_DOWN,
	SF_WHEEL_LEFT,
	SF_WHEEL_RIGHT,
};

/* Peeble/i3/sway-style chord: "Super+w", "Super+Shift+Tab", "Print".
 * Returns 0 on success; -1 on unknown mod/key or empty input. */
int sf_chord_parse(const char *s, uint32_t *mods_out, xkb_keysym_t *sym_out);

/* Mouse-button chord: "Super+LMB", "Super+Shift+RMB", "BTN_RIGHT", "mouse:272".
 * Preferred tokens LMB/RMB/MMB; also BTN_*, MouseLeft, mouse:N.
 * Returns 0 on success; -1 if not a mouse chord / bad token. */
int sf_chord_parse_mouse(const char *s, uint32_t *mods_out, uint32_t *button_out);

/* Resolve BTN_* / LMB / mouse:N. Returns 0 / -1. */
int sf_chord_lookup_button(const char *key, uint32_t *out);

/* True if the key token is a known mouse button name (no mods). */
bool sf_chord_is_mouse_button(const char *key);

/* Wheel chord: "Super+WheelUp", "Super+Shift+WheelDown", "mouse_up" (Hyprland).
 * Requires at least one modifier (bare wheel stays with clients). Returns 0 / -1. */
int sf_chord_parse_wheel(const char *s, uint32_t *mods_out, enum sf_wheel_dir *dir_out);

/* Resolve WheelUp/Down/Left/Right (+ Hyprland mouse_up/down). Returns 0 / -1. */
int sf_chord_lookup_wheel(const char *key, enum sf_wheel_dir *out);

bool sf_chord_is_wheel(const char *key);

/* Modifiers only: "Super", "Super+Shift", "ctrl+alt". Empty → 0. Returns 0 / -1. */
int sf_chord_parse_mods(const char *s, uint32_t *mods_out);

/* Lone-modifier tap names → left keysym (+ right as alt). Returns 0 / -1. */
int sf_chord_parse_tap_mod(const char *s, xkb_keysym_t *sym_out, xkb_keysym_t *sym_alt_out);

/* Shift+Tab often surfaces as ISO_Left_Tab; set *sym_alt when Shift is held. */
void sf_chord_maybe_tab_alt(uint32_t mods, xkb_keysym_t sym, xkb_keysym_t *sym_alt);

/* Resolve a key token to an xkb keysym (aliases + xkb_keysym_from_name). */
int sf_chord_lookup_keysym(const char *key, xkb_keysym_t *out);

#endif
