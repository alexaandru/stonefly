/* sf-suite cover=config/wheelbind.c src=config/wheelbind.c,config/action.c,util/str.c,wm/arrange_math.c stub=log */
#include "sf_test.h"
#include "config/wheelbind.h"
#include "config/action.h"

#include <stdlib.h>
#include <wlr/types/wlr_keyboard.h>

#define SF_SUPER ((uint32_t)WLR_MODIFIER_LOGO)
#define SF_SHIFT ((uint32_t)WLR_MODIFIER_SHIFT)

static void test_seed_and_match(void) {
	struct sf_config cfg = {0};
	SF_TEST_TRUE(sf_wheelbind_seed_defaults(&cfg));
	SF_TEST_EQ_INT((int)cfg.n_wheelbinds, 4);

	const struct sf_wheelbind *w =
		sf_wheelbind_match(&cfg, SF_SUPER, SF_WHEEL_UP, SF_ARRANGE_COLUMNS);
	SF_TEST_TRUE(w != NULL);
	SF_TEST_EQ_INT((int)w->steps[0].action, (int)SF_ACTION_WORKSPACE_PREV);

	w = sf_wheelbind_match(&cfg, SF_SUPER, SF_WHEEL_DOWN, SF_ARRANGE_STACKING);
	SF_TEST_TRUE(w != NULL);
	SF_TEST_EQ_INT((int)w->steps[0].action, (int)SF_ACTION_WORKSPACE_NEXT);

	w = sf_wheelbind_match(&cfg, SF_SUPER | SF_SHIFT, SF_WHEEL_UP, SF_ARRANGE_COLUMNS);
	SF_TEST_TRUE(w != NULL);
	SF_TEST_EQ_INT((int)w->steps[0].action, (int)SF_ACTION_FOCUS_DIR);

	w = sf_wheelbind_match(&cfg, SF_SUPER | SF_SHIFT, SF_WHEEL_DOWN, SF_ARRANGE_COLUMNS);
	SF_TEST_TRUE(w != NULL);
	SF_TEST_EQ_INT((int)w->steps[0].action, (int)SF_ACTION_FOCUS_DIR);

	/* Shift+Wheel focus_dir is tiling-only — stacking falls through. */
	SF_TEST_TRUE(sf_wheelbind_match(&cfg, SF_SUPER | SF_SHIFT, SF_WHEEL_UP,
					SF_ARRANGE_STACKING) == NULL);

	/* Bare wheel / wrong mods never match. */
	SF_TEST_TRUE(sf_wheelbind_match(&cfg, 0, SF_WHEEL_UP, SF_ARRANGE_COLUMNS) == NULL);
	SF_TEST_TRUE(sf_wheelbind_match(&cfg, SF_SUPER | SF_SHIFT, SF_WHEEL_LEFT,
					SF_ARRANGE_COLUMNS) == NULL);

	SF_TEST_STREQ(sf_wheelbind_dir_name(SF_WHEEL_UP), "WheelUp");
	SF_TEST_STREQ(sf_wheelbind_dir_name(SF_WHEEL_DOWN), "WheelDown");

	for (size_t i = 0; i < cfg.n_wheelbinds; i++) {
		sf_action_steps_free(cfg.wheelbinds[i].steps, cfg.wheelbinds[i].n_steps);
	}
	free(cfg.wheelbinds);
}

static void test_arrange_prefer_specific(void) {
	struct sf_config cfg = {0};
	struct sf_wheelbind *w = calloc(2, sizeof *w);
	SF_TEST_TRUE(w != NULL);
	w[0].mods = SF_SUPER;
	w[0].dir = SF_WHEEL_UP;
	w[0].arrange = SF_ARRANGE_UNSET;
	SF_TEST_TRUE(
		sf_action_steps_one(SF_ACTION_WORKSPACE_PREV, NULL, &w[0].steps, &w[0].n_steps));
	w[1].mods = SF_SUPER;
	w[1].dir = SF_WHEEL_UP;
	w[1].arrange = SF_ARRANGE_COLUMNS;
	SF_TEST_TRUE(
		sf_action_steps_one(SF_ACTION_WORKSPACE_NEXT, NULL, &w[1].steps, &w[1].n_steps));
	cfg.wheelbinds = w;
	cfg.n_wheelbinds = 2;

	const struct sf_wheelbind *hit =
		sf_wheelbind_match(&cfg, SF_SUPER, SF_WHEEL_UP, SF_ARRANGE_COLUMNS);
	SF_TEST_TRUE(hit != NULL);
	SF_TEST_EQ_INT((int)hit->steps[0].action, (int)SF_ACTION_WORKSPACE_NEXT);

	hit = sf_wheelbind_match(&cfg, SF_SUPER, SF_WHEEL_UP, SF_ARRANGE_STACKING);
	SF_TEST_TRUE(hit != NULL);
	SF_TEST_EQ_INT((int)hit->steps[0].action, (int)SF_ACTION_WORKSPACE_PREV);

	for (size_t i = 0; i < cfg.n_wheelbinds; i++) {
		sf_action_steps_free(cfg.wheelbinds[i].steps, cfg.wheelbinds[i].n_steps);
	}
	free(cfg.wheelbinds);
}

static const struct sf_test tests[] = {
	{"sf_wheelbind_seed_match", test_seed_and_match},
	{"sf_wheelbind_arrange", test_arrange_prefer_specific},
};

int main(void) {
	return sf_test_run(tests, sizeof(tests) / sizeof(tests[0])) == 0 ? 0 : 1;
}
