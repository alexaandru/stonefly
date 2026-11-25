/* sf-suite cover=wm/arrange_math.c src=wm/arrange_math.c libs=m */
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

	SF_TEST_TRUE(sf_arrange_math_column_strip(&usable, 1, fracs, fulls, 10, 0, 0, 0, false,
						  SF_CENTER_FOCUSED_NEVER, left, width, &offset));
	SF_TEST_EQ_INT(width[0], 800);
	SF_TEST_EQ_INT(left[0], 0);
	SF_TEST_EQ_INT(offset, 0);
}

static void test_strip_two_equal_fit(void) {
	const struct wlr_box usable = {.x = 0, .y = 0, .width = 1000, .height = 800};
	const float fracs[] = {0.5f, 0.5f};
	const bool fulls[] = {false, false};
	int left[2], width[2], offset = -1;

	SF_TEST_TRUE(sf_arrange_math_column_strip(&usable, 2, fracs, fulls, 0, 0, 0, 0, false,
						  SF_CENTER_FOCUSED_NEVER, left, width, &offset));
	SF_TEST_EQ_INT(width[0], 500);
	SF_TEST_EQ_INT(width[1], 500);
	SF_TEST_EQ_INT(left[0], 0);
	SF_TEST_EQ_INT(left[1], 500);
	SF_TEST_EQ_INT(offset, 0);

	SF_TEST_TRUE(sf_arrange_math_column_strip(&usable, 2, fracs, fulls, 0, 1, 1, 0, false,
						  SF_CENTER_FOCUSED_NEVER, left, width, &offset));
	SF_TEST_EQ_INT(offset, 0);
}

static void test_strip_two_equal_with_gap_fit(void) {
	const struct wlr_box usable = {.x = 0, .y = 0, .width = 1000, .height = 800};
	const float fracs[] = {0.5f, 0.5f};
	const bool fulls[] = {false, false};
	int left[2], width[2], offset = -1;

	SF_TEST_TRUE(sf_arrange_math_column_strip(&usable, 2, fracs, fulls, 10, 0, 0, 0, false,
						  SF_CENTER_FOCUSED_NEVER, left, width, &offset));
	SF_TEST_EQ_INT(width[0], 495);
	SF_TEST_EQ_INT(width[1], 495);
	SF_TEST_EQ_INT(left[0], 0);
	SF_TEST_EQ_INT(left[1], 505);
	SF_TEST_EQ_INT(offset, 0);

	SF_TEST_TRUE(sf_arrange_math_column_strip(&usable, 2, fracs, fulls, 10, 1, 1, 0, false,
						  SF_CENTER_FOCUSED_NEVER, left, width, &offset));
	SF_TEST_EQ_INT(offset, 0);
}

static void test_strip_just_enough_scroll(void) {
	const struct wlr_box usable = {.x = 0, .y = 0, .width = 800, .height = 600};
	const float fracs[] = {0.5f, 0.5f, 0.5f};
	const bool fulls[] = {false, false, false};
	int left[3], width[3], offset = -1;

	SF_TEST_TRUE(sf_arrange_math_column_strip(&usable, 3, fracs, fulls, 10, 0, 0, 0, false,
						  SF_CENTER_FOCUSED_NEVER, left, width, &offset));
	SF_TEST_EQ_INT(width[0], 395);
	SF_TEST_EQ_INT(width[1], 395);
	SF_TEST_EQ_INT(width[2], 395);
	SF_TEST_EQ_INT(offset, 0);

	SF_TEST_TRUE(sf_arrange_math_column_strip(&usable, 3, fracs, fulls, 10, 2, 2, 0, false,
						  SF_CENTER_FOCUSED_NEVER, left, width, &offset));
	SF_TEST_EQ_INT(left[2] + width[2] - 800, offset);

	int keep = offset;
	SF_TEST_TRUE(sf_arrange_math_column_strip(&usable, 3, fracs, fulls, 10, 1, 1, keep, false,
						  SF_CENTER_FOCUSED_NEVER, left, width, &offset));
	SF_TEST_EQ_INT(offset, keep);

	/* lock_offset keeps a pan that leaves focus partially off-screen. */
	SF_TEST_TRUE(sf_arrange_math_column_strip(&usable, 3, fracs, fulls, 10, 0, 0, keep, true,
						  SF_CENTER_FOCUSED_NEVER, left, width, &offset));
	SF_TEST_EQ_INT(offset, keep);
}

static void test_strip_close_middle(void) {
	const struct wlr_box usable = {.width = 800, .height = 600};
	const float fracs[] = {0.5f, 0.5f, 0.5f};
	const bool fulls[] = {false, false, false};
	int left[3], width[3], offset;
	SF_TEST_TRUE(sf_arrange_math_column_strip(&usable, 3, fracs, fulls, 10, 2, 2, 0, false,
						  SF_CENTER_FOCUSED_NEVER, left, width, &offset));
	SF_TEST_EQ_INT(offset, 405);
	int previous = offset;
	for (size_t focus = 0; focus < 2; focus++) {
		SF_TEST_TRUE(sf_arrange_math_column_strip(
			&usable, 2, fracs, fulls, 10, focus, focus, previous, false,
			SF_CENTER_FOCUSED_NEVER, left, width, &offset));
		SF_TEST_EQ_INT(offset, 0);
		SF_TEST_EQ_INT(left[0], 0);
		SF_TEST_EQ_INT(left[1], width[0] + 10);
		SF_TEST_EQ_INT(left[1] + width[1], usable.width);
	}
}

static void test_strip_full_width_and_scroll(void) {
	const struct wlr_box usable = {.x = 0, .y = 0, .width = 800, .height = 600};
	const float fracs[] = {0.5f, 0.5f, 0.5f};
	const bool fulls[] = {false, true, false};
	int left[3], width[3], offset = -1;

	SF_TEST_TRUE(sf_arrange_math_column_strip(&usable, 3, fracs, fulls, 10, 1, 1, 0, false,
						  SF_CENTER_FOCUSED_NEVER, left, width, &offset));
	SF_TEST_EQ_INT(width[0], 395);
	SF_TEST_EQ_INT(width[1], 800);
	SF_TEST_EQ_INT(width[2], 395);
	SF_TEST_EQ_INT(left[0], 0);
	SF_TEST_EQ_INT(left[1], 405);
	SF_TEST_EQ_INT(left[2], 1215);
	SF_TEST_EQ_INT(offset, 405);
}

static void test_center_focused_always_centers(void) {
	const struct wlr_box usable = {.width = 800, .height = 600};
	const float fracs[] = {0.5f, 0.5f, 0.5f};
	const bool fulls[] = {false, false, false};
	int left[3], width[3], offset = -1;

	/* Focus middle column: center it in the viewport. */
	SF_TEST_TRUE(sf_arrange_math_column_strip(&usable, 3, fracs, fulls, 10, 1, 1, 0, false,
						  SF_CENTER_FOCUSED_ALWAYS, left, width, &offset));
	int want = left[1] - (usable.width - width[1]) / 2;
	SF_TEST_EQ_INT(offset, want);

	/* Underfull strip: always still clamps — offset stays 0. */
	SF_TEST_TRUE(sf_arrange_math_column_strip(&usable, 2, fracs, fulls, 10, 1, 1, 0, false,
						  SF_CENTER_FOCUSED_ALWAYS, left, width, &offset));
	SF_TEST_EQ_INT(offset, 0);
}

static void test_center_focused_on_overflow(void) {
	const struct wlr_box usable = {.width = 800, .height = 600};
	const float fit[] = {0.5f, 0.5f};
	const bool fit_fulls[] = {false, false};
	const float wide[] = {0.7f, 0.7f, 0.7f};
	const bool wide_fulls[] = {false, false, false};
	int left[3], width[3], offset = -1;

	/* Fitting pair (2×0.5 + gap fills usable): underfull — left-aligned. */
	SF_TEST_TRUE(sf_arrange_math_column_strip(&usable, 2, fit, fit_fulls, 10, 1, 0, 0, false,
						  SF_CENTER_FOCUSED_ON_OVERFLOW, left, width,
						  &offset));
	SF_TEST_EQ_INT(offset, 0);

	/* Wide columns: neighbor+focus span > usable → center. */
	SF_TEST_TRUE(sf_arrange_math_column_strip(&usable, 3, wide, wide_fulls, 10, 1, 0, 0, false,
						  SF_CENTER_FOCUSED_ON_OVERFLOW, left, width,
						  &offset));
	SF_TEST_TRUE(left[1] + width[1] - left[0] > usable.width);
	int centered = left[1] - (usable.width - width[1]) / 2;
	SF_TEST_EQ_INT(offset, centered);

	/* Same focus again (no side): just-enough keeps a prior reveal offset. */
	int keep = left[2] + width[2] - usable.width;
	SF_TEST_TRUE(sf_arrange_math_column_strip(&usable, 3, wide, wide_fulls, 10, 2, 2, keep,
						  false, SF_CENTER_FOCUSED_ON_OVERFLOW, left, width,
						  &offset));
	SF_TEST_EQ_INT(offset, keep);

	/* Equal 0.5×3: pair span == usable → just-enough, not center. */
	const float half[] = {0.5f, 0.5f, 0.5f};
	const bool half_fulls[] = {false, false, false};
	SF_TEST_TRUE(sf_arrange_math_column_strip(&usable, 3, half, half_fulls, 10, 1, 0, 0, false,
						  SF_CENTER_FOCUSED_ON_OVERFLOW, left, width,
						  &offset));
	SF_TEST_EQ_INT(left[1] + width[1] - left[0], usable.width);
	SF_TEST_EQ_INT(offset, 0);
}

static void test_center_focused_parse(void) {
	SF_TEST_EQ_INT((int)sf_center_focused_parse(NULL), (int)SF_CENTER_FOCUSED_NEVER);
	SF_TEST_EQ_INT((int)sf_center_focused_parse("never"), (int)SF_CENTER_FOCUSED_NEVER);
	SF_TEST_EQ_INT((int)sf_center_focused_parse("always"), (int)SF_CENTER_FOCUSED_ALWAYS);
	SF_TEST_EQ_INT((int)sf_center_focused_parse("on_overflow"),
		       (int)SF_CENTER_FOCUSED_ON_OVERFLOW);
	SF_TEST_EQ_INT((int)sf_center_focused_parse("overflow"),
		       (int)SF_CENTER_FOCUSED_ON_OVERFLOW);
	SF_TEST_EQ_INT((int)sf_center_focused_parse("true"), (int)SF_CENTER_FOCUSED_ALWAYS);
	SF_TEST_EQ_INT((int)sf_center_focused_parse("nope"), (int)SF_CENTER_FOCUSED_NEVER);
	SF_TEST_STREQ(sf_center_focused_name(SF_CENTER_FOCUSED_NEVER), "never");
	SF_TEST_STREQ(sf_center_focused_name(SF_CENTER_FOCUSED_ALWAYS), "always");
	SF_TEST_STREQ(sf_center_focused_name(SF_CENTER_FOCUSED_ON_OVERFLOW), "on_overflow");
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

static void test_width_px_helper(void) {
	SF_TEST_EQ_INT(sf_arrange_math_column_width_px(1000, 0.5f, 10), 495);
	SF_TEST_EQ_INT(sf_arrange_math_column_width_px(1000, 0.5f, 0), 500);
}

static const struct sf_test tests[] = {
	{"strip_lone_fills", test_strip_lone_fills},
	{"strip_close_middle", test_strip_close_middle},
	{"strip_two_equal_fit", test_strip_two_equal_fit},
	{"strip_two_equal_with_gap_fit", test_strip_two_equal_with_gap_fit},
	{"strip_just_enough_scroll", test_strip_just_enough_scroll},
	{"strip_full_width_and_scroll", test_strip_full_width_and_scroll},
	{"center_focused_always_centers", test_center_focused_always_centers},
	{"center_focused_on_overflow", test_center_focused_on_overflow},
	{"center_focused_parse", test_center_focused_parse},
	{"rows_equal_weights", test_rows_equal_weights},
	{"rows_unequal_with_gap", test_rows_unequal_with_gap},
	{"rows_three_last_remainder", test_rows_three_last_remainder},
	{"width_px_helper", test_width_px_helper},
};

int main(void) {
	return sf_test_run(tests, sizeof tests / sizeof tests[0]);
}
