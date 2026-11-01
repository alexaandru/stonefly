/* sf-suite cover=config/mousebind.c src=config/mousebind.c,config/action.c,util/str.c,wm/arrange_math.c stub=log */
#include "sf_test.h"
#include "config/mousebind.h"
#include "config/action.h"

#include <linux/input-event-codes.h>
#include <stdlib.h>
#include <wlr/types/wlr_keyboard.h>

#define SF_SUPER ((uint32_t)WLR_MODIFIER_LOGO)
#define SF_SHIFT ((uint32_t)WLR_MODIFIER_SHIFT)

static void test_seed_and_match(void) {
	struct sf_config cfg = {0};
	SF_TEST_TRUE(sf_mousebind_seed_defaults(&cfg));
	SF_TEST_EQ_INT((int)cfg.n_mousebinds, 3);

	const struct sf_mousebind *m =
		sf_mousebind_match(&cfg, SF_SUPER, BTN_LEFT, SF_ARRANGE_COLUMNS);
	SF_TEST_TRUE(m != NULL);
	SF_TEST_EQ_INT((int)m->steps[0].action, (int)SF_ACTION_MOVE_WINDOW);

	m = sf_mousebind_match(&cfg, SF_SUPER, BTN_RIGHT, SF_ARRANGE_STACKING);
	SF_TEST_TRUE(m != NULL);
	SF_TEST_EQ_INT((int)m->steps[0].action, (int)SF_ACTION_RESIZE_WINDOW);

	m = sf_mousebind_match(&cfg, SF_SUPER | SF_SHIFT, BTN_RIGHT, SF_ARRANGE_COLUMNS);
	SF_TEST_TRUE(m != NULL);
	SF_TEST_EQ_INT((int)m->steps[0].action, (int)SF_ACTION_RESIZE_CENTER);

	/* Exact mods — Super alone must not hit Shift+RMB. */
	SF_TEST_TRUE(sf_mousebind_match(&cfg, SF_SUPER, BTN_RIGHT, SF_ARRANGE_COLUMNS) != NULL);
	SF_TEST_TRUE(sf_mousebind_match(&cfg, 0, BTN_LEFT, SF_ARRANGE_COLUMNS) == NULL);
	SF_TEST_TRUE(sf_mousebind_match(&cfg, SF_SUPER | SF_SHIFT, BTN_LEFT, SF_ARRANGE_COLUMNS) ==
		     NULL);

	SF_TEST_STREQ(sf_mousebind_button_name(BTN_LEFT), "LMB");
	SF_TEST_STREQ(sf_mousebind_button_name(BTN_RIGHT), "RMB");

	for (size_t i = 0; i < cfg.n_mousebinds; i++) {
		sf_action_steps_free(cfg.mousebinds[i].steps, cfg.mousebinds[i].n_steps);
	}
	free(cfg.mousebinds);
}

static void test_arrange_prefer_specific(void) {
	struct sf_config cfg = {0};
	struct sf_mousebind *m = calloc(2, sizeof *m);
	SF_TEST_TRUE(m != NULL);
	m[0].mods = SF_SUPER;
	m[0].button = BTN_LEFT;
	m[0].arrange = SF_ARRANGE_UNSET;
	SF_TEST_TRUE(sf_action_steps_one(SF_ACTION_MOVE_WINDOW, NULL, &m[0].steps, &m[0].n_steps));
	m[1].mods = SF_SUPER;
	m[1].button = BTN_LEFT;
	m[1].arrange = SF_ARRANGE_COLUMNS;
	SF_TEST_TRUE(
		sf_action_steps_one(SF_ACTION_RESIZE_WINDOW, NULL, &m[1].steps, &m[1].n_steps));
	cfg.mousebinds = m;
	cfg.n_mousebinds = 2;

	const struct sf_mousebind *hit =
		sf_mousebind_match(&cfg, SF_SUPER, BTN_LEFT, SF_ARRANGE_COLUMNS);
	SF_TEST_TRUE(hit != NULL);
	SF_TEST_EQ_INT((int)hit->steps[0].action, (int)SF_ACTION_RESIZE_WINDOW);

	hit = sf_mousebind_match(&cfg, SF_SUPER, BTN_LEFT, SF_ARRANGE_STACKING);
	SF_TEST_TRUE(hit != NULL);
	SF_TEST_EQ_INT((int)hit->steps[0].action, (int)SF_ACTION_MOVE_WINDOW);

	for (size_t i = 0; i < cfg.n_mousebinds; i++) {
		sf_action_steps_free(cfg.mousebinds[i].steps, cfg.mousebinds[i].n_steps);
	}
	free(cfg.mousebinds);
}

static const struct sf_test tests[] = {
	{"sf_mousebind_seed_match", test_seed_and_match},
	{"sf_mousebind_arrange", test_arrange_prefer_specific},
};

int main(void) {
	return sf_test_run(tests, sizeof(tests) / sizeof(tests[0])) == 0 ? 0 : 1;
}
