/* In-compositor UI stack (cheatsheet first; quit-confirm later). */
#ifndef SF_UI_H
#define SF_UI_H

#include <stdbool.h>

#include "config/config.h"

struct sf_server;

void sf_ui_init(struct sf_server *server);
void sf_ui_fini(struct sf_server *server);
/* Reload cheatsheet fcft face from sf.opt (size + family). */
void sf_ui_apply_config(struct sf_server *server);

bool sf_cheatsheet_is_action(enum sf_action action);
bool sf_cheatsheet_visible(const struct sf_server *server);

void sf_cheatsheet_show(struct sf_server *server);
void sf_cheatsheet_hide(struct sf_server *server);
void sf_cheatsheet_toggle(struct sf_server *server);
/* Startup: show when sf.opt.show_cheatsheet and not locked. */
void sf_cheatsheet_show_on_startup(struct sf_server *server);
/* Live reload / output change while visible → rebuild; opt false → hide. */
void sf_cheatsheet_on_config_reload(struct sf_server *server);
/* Nested/DRM mode change: rebuild while visible so center tracks the box. */
void sf_cheatsheet_on_output_change(struct sf_server *server);
void sf_cheatsheet_on_lock(struct sf_server *server);

/*
 * Dismiss when visible unless the matched action is cheatsheet_* or the press
 * is modifier-only. Bound actions still run (caller order: dismiss then run).
 */
void sf_cheatsheet_dismiss_key(struct sf_server *server, enum sf_action matched,
			       bool modifier_only);
void sf_cheatsheet_dismiss_pointer(struct sf_server *server, enum sf_action matched);

#endif
