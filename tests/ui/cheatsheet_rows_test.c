/* sf-suite cover=ui/cheatsheet_rows.c src=ui/cheatsheet_rows.c,config/action.c,util/str.c stub=log */
#include "sf_test.h"

#include <stdlib.h>
#include <string.h>

#include "config/action.h"
#include "ui/cheatsheet_rows.h"

static struct sf_keybind make_bind(uint32_t mods, xkb_keysym_t sym, enum sf_action action,
				   const char *cmd) {
	struct sf_keybind b;
	memset(&b, 0, sizeof b);
	b.mods = mods;
	b.sym = sym;
	b.arrange = SF_ARRANGE_UNSET;
	if (!sf_action_steps_one(action, cmd, &b.steps, &b.n_steps)) {
		SF_TEST_FAILF("steps_one OOM");
	}
	return b;
}

static void free_binds(struct sf_keybind *binds, size_t n) {
	for (size_t i = 0; i < n; i++) {
		sf_action_steps_free(binds[i].steps, binds[i].n_steps);
	}
}

static void test_chord_format(void) {
	char buf[64];
	sf_cheatsheet_format_chord(SF_SUPER | SF_SHIFT, XKB_KEY_slash, buf, sizeof buf);
	SF_TEST_STREQ(buf, "Super+Shift+/");

	sf_cheatsheet_format_chord(SF_SUPER, XKB_KEY_q, buf, sizeof buf);
	SF_TEST_STREQ(buf, "Super+Q");
}

static void test_chord_arrows(void) {
	char buf[64];
	sf_cheatsheet_format_chord(SF_SUPER, XKB_KEY_Left, buf, sizeof buf);
	SF_TEST_STREQ(buf, "Super+←");
}

static void test_group_for_action(void) {
	SF_TEST_EQ_INT((int)sf_cheatsheet_group_for_action(SF_ACTION_EXEC), (int)SF_CS_GROUP_APPS);
	SF_TEST_EQ_INT((int)sf_cheatsheet_group_for_action(SF_ACTION_FOCUS_DIR),
		       (int)SF_CS_GROUP_FOCUS);
	SF_TEST_EQ_INT((int)sf_cheatsheet_group_for_action(SF_ACTION_CLOSE),
		       (int)SF_CS_GROUP_WINDOWS);
	SF_TEST_EQ_INT((int)sf_cheatsheet_group_for_action(SF_ACTION_WORKSPACE_SWITCH),
		       (int)SF_CS_GROUP_WORKSPACES);
	SF_TEST_EQ_INT((int)sf_cheatsheet_group_for_action(SF_ACTION_CHEATSHEET_TOGGLE),
		       (int)SF_CS_GROUP_SYSTEM);
	SF_TEST_STREQ(sf_cheatsheet_group_title(SF_CS_GROUP_MOVE), "Move & size");
}

static void test_merge_chords(void) {
	struct sf_keybind binds[2];
	binds[0] = make_bind(SF_SUPER, XKB_KEY_q, SF_ACTION_CLOSE, NULL);
	binds[1] = make_bind(SF_CTRL, XKB_KEY_q, SF_ACTION_CLOSE, NULL);

	size_t n = 0;
	struct sf_cheatsheet_row *rows = sf_cheatsheet_rows_build(binds, 2, &n);
	SF_TEST_EQ_INT((int)n, 1);
	SF_TEST_TRUE(rows != NULL);
	if (rows != NULL) {
		SF_TEST_TRUE(strstr(rows[0].chord, "Super+Q") != NULL);
		SF_TEST_TRUE(strstr(rows[0].chord, "Ctrl+Q") != NULL);
		SF_TEST_STREQ(rows[0].label, "close");
	}
	free(rows);
	free_binds(binds, 2);
}

static void test_workspace_collapse(void) {
	struct sf_keybind binds[9];
	for (int i = 0; i < 9; i++) {
		char cmd[2] = {(char)('1' + i), '\0'};
		binds[i] = make_bind(SF_SUPER, (xkb_keysym_t)(XKB_KEY_1 + i),
				     SF_ACTION_WORKSPACE_SWITCH, cmd);
	}
	size_t n = 0;
	struct sf_cheatsheet_row *rows = sf_cheatsheet_rows_build(binds, 9, &n);
	SF_TEST_EQ_INT((int)n, 1);
	SF_TEST_TRUE(rows != NULL);
	if (rows != NULL) {
		SF_TEST_TRUE(strstr(rows[0].chord, "1…9") != NULL);
		SF_TEST_TRUE(strstr(rows[0].label, "1-9") != NULL);
	}
	free(rows);
	free_binds(binds, 9);
}

static void test_exec_label(void) {
	struct sf_keybind b = make_bind(SF_SUPER, XKB_KEY_space, SF_ACTION_EXEC, "foot");
	size_t n = 0;
	struct sf_cheatsheet_row *rows = sf_cheatsheet_rows_build(&b, 1, &n);
	SF_TEST_EQ_INT((int)n, 1);
	if (rows != NULL) {
		SF_TEST_STREQ(rows[0].label, "foot");
		SF_TEST_EQ_INT((int)rows[0].group, (int)SF_CS_GROUP_APPS);
	}
	free(rows);
	free_binds(&b, 1);
}

int main(void) {
	const struct sf_test tests[] = {
		{"chord_format", test_chord_format},
		{"chord_arrows", test_chord_arrows},
		{"group_for_action", test_group_for_action},
		{"merge_chords", test_merge_chords},
		{"workspace_collapse", test_workspace_collapse},
		{"exec_label", test_exec_label},
	};
	return sf_test_run(tests, sizeof tests / sizeof tests[0]) == 0 ? 0 : 1;
}
