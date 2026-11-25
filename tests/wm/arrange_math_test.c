/* sf-suite cover=wm/arrange_math.c src=wm/arrange_math.c libs=m */
#include "sf_test.h"
#include "wm/arrange_math.h"

static void expect_box(const struct wlr_box *got, int x, int y, int w, int h) {
	SF_TEST_EQ_INT(got->x, x);
	SF_TEST_EQ_INT(got->y, y);
	SF_TEST_EQ_INT(got->width, w);
	SF_TEST_EQ_INT(got->height, h);
}

static void test_columns_three_offset(void) {
	const struct wlr_box usable = {.x = 100, .y = 50, .width = 800, .height = 600};
	struct wlr_box out[3];
	/* col_w = round(0.5*(800+10))-10 = 395; gap = 10; view_offset = 100 */
	SF_TEST_TRUE(sf_arrange_math_columns(&usable, 3, 0.5, 10, 100, out));
	expect_box(&out[0], 100 + 0 * 405 - 100, 50, 395, 600);
	expect_box(&out[1], 100 + 1 * 405 - 100, 50, 395, 600);
	expect_box(&out[2], 100 + 2 * 405 - 100, 50, 395, 600);
	SF_TEST_EQ_INT(out[0].x, 0);
	SF_TEST_EQ_INT(out[1].x, 405);
	SF_TEST_EQ_INT(out[2].x, 810);
}

static void test_invalid_usable(void) {
	struct wlr_box out[1];
	struct wlr_box bad = {.x = 0, .y = 0, .width = 0, .height = 100};
	struct wlr_box ok = {.x = 0, .y = 0, .width = 10, .height = 10};

	SF_TEST_TRUE(!sf_arrange_math_columns(&bad, 1, 0.5, 0, 0, out));
	SF_TEST_TRUE(sf_arrange_math_columns(&ok, 0, 0.5, 0, 0, out));
}

static void test_arrange_parse_aliases(void) {
	SF_TEST_EQ_INT((int)sf_arrange_parse("stacking"), (int)SF_ARRANGE_STACKING);
	SF_TEST_EQ_INT((int)sf_arrange_parse("columns"), (int)SF_ARRANGE_COLUMNS);
	SF_TEST_EQ_INT((int)sf_arrange_parse("master"), (int)SF_ARRANGE_COLUMNS);
	SF_TEST_EQ_INT((int)sf_arrange_parse("dwindle"), (int)SF_ARRANGE_COLUMNS);
	SF_TEST_EQ_INT((int)sf_arrange_parse("monocle"), (int)SF_ARRANGE_COLUMNS);
	SF_TEST_EQ_INT((int)sf_arrange_parse("tile"), (int)SF_ARRANGE_COLUMNS);
	SF_TEST_STREQ(sf_arrange_name(SF_ARRANGE_STACKING), "stacking");
	SF_TEST_STREQ(sf_arrange_name(SF_ARRANGE_COLUMNS), "columns");
	SF_TEST_TRUE(sf_arrange_is_tiling(SF_ARRANGE_COLUMNS));
	SF_TEST_TRUE(!sf_arrange_is_tiling(SF_ARRANGE_STACKING));
}

static void test_columns_one_slot_width(void) {
	const struct wlr_box usable = {.x = 0, .y = 0, .width = 500, .height = 400};
	struct wlr_box out[1];
	/* Legacy equal-column helper uses slot math (lone fill is column_strip). */
	SF_TEST_TRUE(sf_arrange_math_columns(&usable, 1, 0.5, 10, 0, out));
	expect_box(&out[0], 0, 0, 245, 400);
}

static const struct sf_test tests[] = {
	{"columns_three_offset", test_columns_three_offset},
	{"columns_one_slot_width", test_columns_one_slot_width},
	{"arrange_parse_aliases", test_arrange_parse_aliases},
	{"invalid_usable", test_invalid_usable},
};

int main(void) {
	return sf_test_run(tests, sizeof(tests) / sizeof(tests[0])) == 0 ? 0 : 1;
}
