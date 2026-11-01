#ifndef SF_SEAT_IME_H
#define SF_SEAT_IME_H

#include <stdbool.h>

struct sf_server;
struct wlr_keyboard;
struct wlr_keyboard_key_event;

/* Advertise text-input-v3 + input-method-v2 and relay between them (fcitx/ibus). */
void sf_ime_setup(struct sf_server *server);
void sf_ime_finish(struct sf_server *server);

/* Forward to the input-method keyboard grab when present. Returns true when
 * the event was consumed by the grab (skip seat client notify). */
bool sf_ime_grab_forward_key(struct sf_server *server, struct wlr_keyboard *keyboard,
			     struct wlr_keyboard_key_event *event);
bool sf_ime_grab_forward_modifiers(struct sf_server *server, struct wlr_keyboard *keyboard);

/* Share the grab keymap with an IME-owned virtual keyboard (avoids client churn). */
void sf_ime_canonicalize_owned_keyboard(struct sf_server *server, struct wlr_keyboard *keyboard);

#endif
