/* sf-suite cover=config/gesture.c src=config/gesture.c,config/action.c,config/chord.c,util/str.c stub=log */
#include "sf_test.h"
#include "config/gesture.h"
#include "config/action.h"
#include "config/chord.h"

#include <stdlib.h>
#include <string.h>
#include <wlr/types/wlr_keyboard.h>

#define SF_SUPER ((uint32_t)WLR_MODIFIER_LOGO)

static void test_dir_parse(void) {
	enum sf_gesture_dir d = SF_GESTURE_DIR_SWIPE;
	SF_TEST_TRUE(sf_gesture_dir_parse("horizontal", &d));
	SF_TEST_EQ_INT((int)d, (int)SF_GESTURE_DIR_HORIZONTAL);
	SF_TEST_TRUE(sf_gesture_dir_parse("vertical", &d));
	SF_TEST_EQ_INT((int)d, (int)SF_GESTURE_DIR_VERTICAL);
	SF_TEST_TRUE(sf_gesture_dir_parse("left", &d));
	SF_TEST_EQ_INT((int)d, (int)SF_GESTURE_DIR_LEFT);
	SF_TEST_TRUE(sf_gesture_dir_parse("any", &d));
	SF_TEST_EQ_INT((int)d, (int)SF_GESTURE_DIR_SWIPE);
	SF_TEST_TRUE(sf_gesture_dir_parse("pinchin", &d));
	SF_TEST_EQ_INT((int)d, (int)SF_GESTURE_DIR_PINCH_IN);
	SF_TEST_TRUE(sf_gesture_dir_parse("pinchout", &d));
	SF_TEST_EQ_INT((int)d, (int)SF_GESTURE_DIR_PINCH_OUT);
	SF_TEST_TRUE(!sf_gesture_dir_parse("diagonal", &d));
	SF_TEST_TRUE(!sf_gesture_dir_parse(NULL, &d));
	SF_TEST_STREQ(sf_gesture_dir_name(SF_GESTURE_DIR_UP), "up");
	SF_TEST_STREQ(sf_gesture_dir_name(SF_GESTURE_DIR_PINCH_IN), "pinchin");
}

static void test_kind_parse(void) {
	enum sf_gesture_kind k = SF_GESTURE_KIND_ACTION;
	SF_TEST_TRUE(sf_gesture_kind_parse("workspace", &k));
	SF_TEST_EQ_INT((int)k, (int)SF_GESTURE_KIND_WORKSPACE);
	SF_TEST_TRUE(sf_gesture_kind_parse("columns_pan", &k));
	SF_TEST_EQ_INT((int)k, (int)SF_GESTURE_KIND_COLUMNS_PAN);
	SF_TEST_TRUE(sf_gesture_kind_parse("overview", &k));
	SF_TEST_EQ_INT((int)k, (int)SF_GESTURE_KIND_OVERVIEW);
	SF_TEST_TRUE(!sf_gesture_kind_parse("close", &k));
	SF_TEST_STREQ(sf_gesture_kind_name(SF_GESTURE_KIND_WORKSPACE), "workspace");
	SF_TEST_STREQ(sf_gesture_kind_name(SF_GESTURE_KIND_OVERVIEW), "overview");
}

static void test_match_specificity(void) {
	struct sf_config cfg = {0};
	SF_TEST_TRUE(sf_gesture_seed_defaults(&cfg));
	SF_TEST_EQ_INT((int)cfg.n_gestures, 3);

	const struct sf_gesture *g = sf_gesture_match(&cfg, 3, 0, SF_GESTURE_DIR_VERTICAL, -40.0);
	SF_TEST_TRUE(g != NULL);
	SF_TEST_EQ_INT((int)g->kind, (int)SF_GESTURE_KIND_WORKSPACE);

	g = sf_gesture_match(&cfg, 3, 0, SF_GESTURE_DIR_HORIZONTAL, 20.0);
	SF_TEST_TRUE(g != NULL);
	SF_TEST_EQ_INT((int)g->kind, (int)SF_GESTURE_KIND_COLUMNS_PAN);

	g = sf_gesture_match(&cfg, 4, 0, SF_GESTURE_DIR_VERTICAL, -40.0);
	SF_TEST_TRUE(g != NULL);
	SF_TEST_EQ_INT((int)g->kind, (int)SF_GESTURE_KIND_OVERVIEW);

	/* Wrong finger count → no match. */
	SF_TEST_TRUE(sf_gesture_match(&cfg, 5, 0, SF_GESTURE_DIR_VERTICAL, -40.0) == NULL);

	/* Exact direction beats axis. */
	struct sf_gesture *extra = realloc(cfg.gestures, 4 * sizeof *extra);
	SF_TEST_TRUE(extra != NULL);
	cfg.gestures = extra;
	cfg.gestures[3] = (struct sf_gesture){
		.fingers = 3,
		.dir = SF_GESTURE_DIR_UP,
		.kind = SF_GESTURE_KIND_ACTION,
	};
	SF_TEST_TRUE(sf_action_steps_one(SF_ACTION_CLOSE, NULL, &cfg.gestures[3].steps,
					 &cfg.gestures[3].n_steps));
	cfg.n_gestures = 4;

	g = sf_gesture_match(&cfg, 3, 0, SF_GESTURE_DIR_VERTICAL, -40.0);
	SF_TEST_TRUE(g != NULL);
	SF_TEST_EQ_INT((int)g->kind, (int)SF_GESTURE_KIND_ACTION);
	SF_TEST_EQ_INT((int)g->dir, (int)SF_GESTURE_DIR_UP);

	/* Finger down still hits vertical workspace, not up. */
	g = sf_gesture_match(&cfg, 3, 0, SF_GESTURE_DIR_VERTICAL, 40.0);
	SF_TEST_TRUE(g != NULL);
	SF_TEST_EQ_INT((int)g->kind, (int)SF_GESTURE_KIND_WORKSPACE);

	/* Mods must match exactly. */
	SF_TEST_TRUE(sf_gesture_match(&cfg, 3, SF_SUPER, SF_GESTURE_DIR_VERTICAL, -40.0) == NULL);

	for (size_t i = 0; i < cfg.n_gestures; i++) {
		sf_action_steps_free(cfg.gestures[i].steps, cfg.gestures[i].n_steps);
	}
	free(cfg.gestures);
}

static void test_match_pinch(void) {
	struct sf_config cfg = {0};
	struct sf_gesture *g = calloc(3, sizeof *g);
	SF_TEST_TRUE(g != NULL);
	g[0].fingers = 2;
	g[0].dir = SF_GESTURE_DIR_PINCH;
	g[0].kind = SF_GESTURE_KIND_ACTION;
	SF_TEST_TRUE(sf_action_steps_one(SF_ACTION_CLOSE, NULL, &g[0].steps, &g[0].n_steps));
	g[1].fingers = 2;
	g[1].dir = SF_GESTURE_DIR_PINCH_OUT;
	g[1].mods = SF_SUPER;
	g[1].kind = SF_GESTURE_KIND_ACTION;
	SF_TEST_TRUE(
		sf_action_steps_one(SF_ACTION_MAXIMIZE_TO_EDGES, NULL, &g[1].steps, &g[1].n_steps));
	g[2].fingers = 2;
	g[2].dir = SF_GESTURE_DIR_PINCH_IN;
	g[2].mods = SF_SUPER;
	g[2].kind = SF_GESTURE_KIND_ACTION;
	SF_TEST_TRUE(sf_action_steps_one(SF_ACTION_UNMAXIMIZE, NULL, &g[2].steps, &g[2].n_steps));
	cfg.gestures = g;
	cfg.n_gestures = 3;

	/* No Super → bare pinch bind (close), not Super+pinchout. */
	const struct sf_gesture *m = sf_gesture_match_pinch(&cfg, 2, 0, 1.2);
	SF_TEST_TRUE(m != NULL);
	SF_TEST_EQ_INT((int)m->dir, (int)SF_GESTURE_DIR_PINCH);

	m = sf_gesture_match_pinch(&cfg, 2, SF_SUPER, 1.2);
	SF_TEST_TRUE(m != NULL);
	SF_TEST_EQ_INT((int)m->dir, (int)SF_GESTURE_DIR_PINCH_OUT);

	m = sf_gesture_match_pinch(&cfg, 2, SF_SUPER, 0.8);
	SF_TEST_TRUE(m != NULL);
	SF_TEST_EQ_INT((int)m->dir, (int)SF_GESTURE_DIR_PINCH_IN);

	/* Swipe matcher ignores pinch binds. */
	SF_TEST_TRUE(sf_gesture_match(&cfg, 2, 0, SF_GESTURE_DIR_HORIZONTAL, 10.0) == NULL);

	for (size_t i = 0; i < cfg.n_gestures; i++) {
		sf_action_steps_free(cfg.gestures[i].steps, cfg.gestures[i].n_steps);
	}
	free(cfg.gestures);
}

static const struct sf_test tests[] = {
	{"sf_gesture_dir_parse", test_dir_parse},
	{"sf_gesture_kind_parse", test_kind_parse},
	{"sf_gesture_match", test_match_specificity},
	{"sf_gesture_match_pinch", test_match_pinch},
};

int main(void) {
	return sf_test_run(tests, sizeof tests / sizeof tests[0]) == 0 ? 0 : 1;
}
