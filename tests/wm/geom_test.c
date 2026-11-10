#include "sf_test.h"
#include "wm/geom.h"

static void test_box_inset_gap(void) {
	struct wlr_box box = {.x = 10, .y = 20, .width = 100, .height = 80};

	sf_box_inset_gap(&box, 8);
	SF_TEST_EQ_INT(box.x, 18);
	SF_TEST_EQ_INT(box.y, 28);
	SF_TEST_EQ_INT(box.width, 84);
	SF_TEST_EQ_INT(box.height, 64);

	/* Too narrow/short for a full inset: leave that axis alone. */
	box = (struct wlr_box){.x = 0, .y = 0, .width = 10, .height = 100};
	sf_box_inset_gap(&box, 8);
	SF_TEST_EQ_INT(box.x, 0);
	SF_TEST_EQ_INT(box.width, 10);
	SF_TEST_EQ_INT(box.y, 8);
	SF_TEST_EQ_INT(box.height, 84);

	box = (struct wlr_box){.x = 0, .y = 0, .width = 100, .height = 10};
	sf_box_inset_gap(&box, 8);
	SF_TEST_EQ_INT(box.x, 8);
	SF_TEST_EQ_INT(box.width, 84);
	SF_TEST_EQ_INT(box.y, 0);
	SF_TEST_EQ_INT(box.height, 10);

	box = (struct wlr_box){.x = 5, .y = 5, .width = 40, .height = 40};
	sf_box_inset_gap(&box, 0);
	SF_TEST_EQ_INT(box.x, 5);
	SF_TEST_EQ_INT(box.width, 40);
}

static void test_snap_box(void) {
	const struct wlr_box usable = {.x = 100, .y = 50, .width = 800, .height = 600};
	const int gap = 10;
	/* half = (800 - 30) / 2 = 385; full = 800 - 20 = 780 / 580 */
	struct wlr_box left = sf_wm_snap_box(SF_SNAP_LEFT, &usable, gap);
	SF_TEST_EQ_INT(left.x, 110);
	SF_TEST_EQ_INT(left.y, 60);
	SF_TEST_EQ_INT(left.width, 385);
	SF_TEST_EQ_INT(left.height, 580);

	struct wlr_box right = sf_wm_snap_box(SF_SNAP_RIGHT, &usable, gap);
	SF_TEST_EQ_INT(right.x, 100 + 20 + 385);
	SF_TEST_EQ_INT(right.y, 60);
	SF_TEST_EQ_INT(right.width, 385);
	SF_TEST_EQ_INT(right.height, 580);

	struct wlr_box top = sf_wm_snap_box(SF_SNAP_TOP, &usable, gap);
	SF_TEST_EQ_INT(top.x, 110);
	SF_TEST_EQ_INT(top.y, 60);
	SF_TEST_EQ_INT(top.width, 780);
	SF_TEST_EQ_INT(top.height, (600 - 30) / 2);

	struct wlr_box bottom = sf_wm_snap_box(SF_SNAP_BOTTOM, &usable, gap);
	SF_TEST_EQ_INT(bottom.x, 110);
	SF_TEST_EQ_INT(bottom.y, 50 + 20 + (600 - 30) / 2);
	SF_TEST_EQ_INT(bottom.width, 780);
	SF_TEST_EQ_INT(bottom.height, (600 - 30) / 2);

	struct wlr_box none = sf_wm_snap_box(SF_SNAP_NONE, &usable, gap);
	SF_TEST_EQ_INT(none.x, 0);
	SF_TEST_EQ_INT(none.y, 0);
	SF_TEST_EQ_INT(none.width, 0);
	SF_TEST_EQ_INT(none.height, 0);
}

static const struct sf_test tests[] = {
	{"sf_box_inset_gap", test_box_inset_gap},
	{"sf_wm_snap_box", test_snap_box},
};

int main(void) {
	return sf_test_run(tests, sizeof(tests) / sizeof(tests[0])) == 0 ? 0 : 1;
}
