/* Pure cheatsheet content: keybinds → display rows (no scene / fcft). */
#ifndef SF_UI_CHEATSHEET_ROWS_H
#define SF_UI_CHEATSHEET_ROWS_H

#include <stddef.h>

#include "config/config.h"

enum sf_cheatsheet_group {
	SF_CS_GROUP_APPS = 0,
	SF_CS_GROUP_FOCUS,
	SF_CS_GROUP_MOVE,
	SF_CS_GROUP_WINDOWS,
	SF_CS_GROUP_SCRATCH,
	SF_CS_GROUP_WORKSPACES,
	SF_CS_GROUP_OVERVIEW,
	SF_CS_GROUP_SYSTEM,
	SF_CS_GROUP_COUNT,
};

struct sf_cheatsheet_row {
	char chord[96];
	char label[80];
	enum sf_cheatsheet_group group;
	enum sf_action action;
};

const char *sf_cheatsheet_group_title(enum sf_cheatsheet_group group);
enum sf_cheatsheet_group sf_cheatsheet_group_for_action(enum sf_action action);

/* Fixed display order (Apps … System). */
const enum sf_cheatsheet_group *sf_cheatsheet_group_order(size_t *out_n);

/*
 * Build display rows from keybinds. Merges identical action+cmd into one row
 * with comma-joined chords; collapses Super+1…9 workspace runs. Caller frees
 * with free(). Returns NULL when n_out is 0 (also sets *n_out = 0).
 */
struct sf_cheatsheet_row *sf_cheatsheet_rows_build(const struct sf_keybind *binds, size_t n_binds,
						   size_t *n_out);

/* Format one chord into buf (NUL-terminated). */
void sf_cheatsheet_format_chord(uint32_t mods, xkb_keysym_t sym, char *buf, size_t buflen);

#endif
