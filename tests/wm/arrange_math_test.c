/* sf-suite cover=wm/arrange_math.c src=wm/arrange_math.c */
#include "sf_test.h"
#include "wm/arrange_math.h"

static void expect_box(const struct wlr_box *got, int x, int y, int w, int h) {
	SF_TEST_EQ_INT(got->x, x);
	SF_TEST_EQ_INT(got->y, y);
	SF_TEST_EQ_INT(got->width, w);
	SF_TEST_EQ_INT(got->height, h);
}

static void test_master_left_two(void) {
	const struct wlr_box usable = {.x = 10, .y = 20, .width = 1000, .height = 800};
	struct wlr_box out[2];

	SF_TEST_TRUE(sf_arrange_math_master(&usable, 2, SF_MASTER_LEFT, 0.55, 1, out));
	/* master_w = (int)(1000 * 0.55) = 550; stack = 450 */
	expect_box(&out[0], 10, 20, 550, 800);
	expect_box(&out[1], 560, 20, 450, 800);
}

static void test_master_center_three(void) {
	const struct wlr_box usable = {.x = 0, .y = 0, .width = 1000, .height = 600};
	struct wlr_box out[3];

	SF_TEST_TRUE(sf_arrange_math_master(&usable, 3, SF_MASTER_CENTER, 0.5, 1, out));
	/* center_w = 500; side_total = 500; left_w = 250; right_w = 250
	 * stack_n = 2 → left_n = 1, right_n = 1 */
	expect_box(&out[0], 250, 0, 500, 600);
	expect_box(&out[1], 0, 0, 250, 600);
	expect_box(&out[2], 750, 0, 250, 600);
}

static void test_columns_three_offset(void) {
	const struct wlr_box usable = {.x = 100, .y = 50, .width = 800, .height = 600};
	struct wlr_box out[3];
	/* col_w = (int)(800 * 0.5) = 400; gap = 10; view_offset = 100 */
	SF_TEST_TRUE(sf_arrange_math_columns(&usable, 3, 0.5, 10, 100, out));
	expect_box(&out[0], 100 + 0 * 410 - 100, 50, 400, 600);
	expect_box(&out[1], 100 + 1 * 410 - 100, 50, 400, 600);
	expect_box(&out[2], 100 + 2 * 410 - 100, 50, 400, 600);
	SF_TEST_EQ_INT(out[0].x, 0);
	SF_TEST_EQ_INT(out[1].x, 410);
	SF_TEST_EQ_INT(out[2].x, 820);
}

static void test_dwindle_four(void) {
	const struct wlr_box usable = {.x = 0, .y = 0, .width = 800, .height = 600};
	struct wlr_box out[4];

	SF_TEST_TRUE(sf_arrange_math_dwindle(&usable, 4, out));
	/* 0: left half vertical → 400×600
	 * rest 400×600 horizontal:
	 *   1: top half → 400×300 at (400,0)
	 *   rest 400×300 at (400,300) vertical:
	 *     2: left → 200×300 at (400,300)
	 *     3: right → 200×300 at (600,300) */
	expect_box(&out[0], 0, 0, 400, 600);
	expect_box(&out[1], 400, 0, 400, 300);
	expect_box(&out[2], 400, 300, 200, 300);
	expect_box(&out[3], 600, 300, 200, 300);
}

static void test_monocle_focus(void) {
	const struct wlr_box usable = {.x = 5, .y = 10, .width = 900, .height = 700};
	struct wlr_box out[3];

	SF_TEST_TRUE(sf_arrange_math_monocle(&usable, 3, 1, out));
	expect_box(&out[0], 0, 0, 0, 0);
	expect_box(&out[1], 5, 10, 900, 700);
	expect_box(&out[2], 0, 0, 0, 0);

	SF_TEST_TRUE(sf_arrange_math_monocle(&usable, 3, 99, out));
	expect_box(&out[0], 5, 10, 900, 700);
	expect_box(&out[1], 0, 0, 0, 0);
	expect_box(&out[2], 0, 0, 0, 0);
}

static void test_master_pos_parse(void) {
	SF_TEST_EQ_INT((int)sf_master_pos_parse(NULL), (int)SF_MASTER_LEFT);
	SF_TEST_EQ_INT((int)sf_master_pos_parse(""), (int)SF_MASTER_LEFT);
	SF_TEST_EQ_INT((int)sf_master_pos_parse("left"), (int)SF_MASTER_LEFT);
	SF_TEST_EQ_INT((int)sf_master_pos_parse("right"), (int)SF_MASTER_RIGHT);
	SF_TEST_EQ_INT((int)sf_master_pos_parse("top"), (int)SF_MASTER_TOP);
	SF_TEST_EQ_INT((int)sf_master_pos_parse("bottom"), (int)SF_MASTER_BOTTOM);
	SF_TEST_EQ_INT((int)sf_master_pos_parse("center"), (int)SF_MASTER_CENTER);
	SF_TEST_EQ_INT((int)sf_master_pos_parse("bogus"), (int)SF_MASTER_LEFT);

	SF_TEST_STREQ(sf_master_pos_name(SF_MASTER_LEFT), "left");
	SF_TEST_STREQ(sf_master_pos_name(SF_MASTER_RIGHT), "right");
	SF_TEST_STREQ(sf_master_pos_name(SF_MASTER_TOP), "top");
	SF_TEST_STREQ(sf_master_pos_name(SF_MASTER_BOTTOM), "bottom");
	SF_TEST_STREQ(sf_master_pos_name(SF_MASTER_CENTER), "center");
	SF_TEST_STREQ(sf_master_pos_name((enum sf_master_pos)99), "left");
}

static void test_invalid_usable(void) {
	struct wlr_box out[1];
	struct wlr_box bad = {.x = 0, .y = 0, .width = 0, .height = 100};
	struct wlr_box ok = {.x = 0, .y = 0, .width = 10, .height = 10};

	SF_TEST_TRUE(!sf_arrange_math_master(&bad, 1, SF_MASTER_LEFT, 0.5, 1, out));
	SF_TEST_TRUE(!sf_arrange_math_columns(&bad, 1, 0.5, 0, 0, out));
	SF_TEST_TRUE(!sf_arrange_math_dwindle(&bad, 1, out));
	SF_TEST_TRUE(!sf_arrange_math_monocle(&bad, 1, 0, out));
	SF_TEST_TRUE(sf_arrange_math_master(&ok, 0, SF_MASTER_LEFT, 0.5, 1, out));
}

static const struct sf_test tests[] = {
	{"master_left_two", test_master_left_two},
	{"master_center_three", test_master_center_three},
	{"columns_three_offset", test_columns_three_offset},
	{"dwindle_four", test_dwindle_four},
	{"monocle_focus", test_monocle_focus},
	{"master_pos_parse", test_master_pos_parse},
	{"invalid_usable", test_invalid_usable},
};

int main(void) {
	return sf_test_run(tests, sizeof(tests) / sizeof(tests[0])) == 0 ? 0 : 1;
}
