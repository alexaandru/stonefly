/* sf-suite cover=wm/arrange_math.c src=wm/arrange_math.c */
#include "sf_test.h"
#include "wm/arrange_math.h"

static void expect_box(const struct wlr_box *got, int x, int y, int w, int h) {
	SF_TEST_EQ_INT(got->x, x);
	SF_TEST_EQ_INT(got->y, y);
	SF_TEST_EQ_INT(got->width, w);
	SF_TEST_EQ_INT(got->height, h);
}

static void test_strip_lone_fills(void) {
	const struct wlr_box usable = {.x = 10, .y = 20, .width = 800, .height = 600};
	const float fracs[] = {0.5f};
	const bool fulls[] = {false};
	int left[1], width[1], offset = -1;

	SF_TEST_TRUE(sf_arrange_math_column_strip(&usable, 1, fracs, fulls, 10, 0, left, width,
						  &offset));
	SF_TEST_EQ_INT(width[0], 800);
	SF_TEST_EQ_INT(left[0], 0);
	SF_TEST_EQ_INT(offset, 0);
}

static void test_strip_two_equal(void) {
	const struct wlr_box usable = {.x = 0, .y = 0, .width = 1000, .height = 800};
	const float fracs[] = {0.5f, 0.5f};
	const bool fulls[] = {false, false};
	int left[2], width[2], offset = -1;

	SF_TEST_TRUE(
		sf_arrange_math_column_strip(&usable, 2, fracs, fulls, 0, 0, left, width, &offset));
	SF_TEST_EQ_INT(width[0], 500);
	SF_TEST_EQ_INT(width[1], 500);
	SF_TEST_EQ_INT(left[0], 0);
	SF_TEST_EQ_INT(left[1], 500);
	SF_TEST_EQ_INT(offset, 0);
}

static void test_strip_full_width_and_scroll(void) {
	const struct wlr_box usable = {.x = 0, .y = 0, .width = 800, .height = 600};
	const float fracs[] = {0.5f, 0.5f, 0.5f};
	const bool fulls[] = {false, true, false};
	int left[3], width[3], offset = -1;

	/* col widths: 400, 800, 400; gaps 10 → total = 400+10+800+10+400 = 1620
	 * focus col 1 center = 410 + 400 = 810; offset = 810 - 400 = 410 */
	SF_TEST_TRUE(sf_arrange_math_column_strip(&usable, 3, fracs, fulls, 10, 1, left, width,
						  &offset));
	SF_TEST_EQ_INT(width[0], 400);
	SF_TEST_EQ_INT(width[1], 800);
	SF_TEST_EQ_INT(width[2], 400);
	SF_TEST_EQ_INT(left[0], 0);
	SF_TEST_EQ_INT(left[1], 410);
	SF_TEST_EQ_INT(left[2], 1220);
	SF_TEST_EQ_INT(offset, 410);
}

static void test_rows_equal_weights(void) {
	const struct wlr_box col = {.x = 100, .y = 50, .width = 400, .height = 600};
	const float weights[] = {1.0f, 1.0f};
	struct wlr_box out[2];

	SF_TEST_TRUE(sf_arrange_math_column_rows(&col, 2, weights, 0, out));
	expect_box(&out[0], 100, 50, 400, 300);
	expect_box(&out[1], 100, 350, 400, 300);
}

static void test_rows_unequal_with_gap(void) {
	const struct wlr_box col = {.x = 0, .y = 0, .width = 200, .height = 310};
	const float weights[] = {1.0f, 2.0f};
	struct wlr_box out[2];

	/* content = 310 - 10 = 300; h0 = 100, h1 = 200 */
	SF_TEST_TRUE(sf_arrange_math_column_rows(&col, 2, weights, 10, out));
	expect_box(&out[0], 0, 0, 200, 100);
	expect_box(&out[1], 0, 110, 200, 200);
}

static void test_rows_three_last_remainder(void) {
	const struct wlr_box col = {.x = 0, .y = 0, .width = 100, .height = 100};
	const float weights[] = {1.0f, 1.0f, 1.0f};
	struct wlr_box out[3];

	SF_TEST_TRUE(sf_arrange_math_column_rows(&col, 3, weights, 0, out));
	SF_TEST_EQ_INT(out[0].height + out[1].height + out[2].height, 100);
	SF_TEST_EQ_INT(out[2].y + out[2].height, 100);
}

static const struct sf_test tests[] = {
	{"strip_lone_fills", test_strip_lone_fills},
	{"strip_two_equal", test_strip_two_equal},
	{"strip_full_width_and_scroll", test_strip_full_width_and_scroll},
	{"rows_equal_weights", test_rows_equal_weights},
	{"rows_unequal_with_gap", test_rows_unequal_with_gap},
	{"rows_three_last_remainder", test_rows_three_last_remainder},
};

int main(void) {
	return sf_test_run(tests, sizeof tests / sizeof tests[0]);
}
