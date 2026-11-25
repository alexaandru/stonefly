/* sf-suite cover=effect/blur_region.c src=effect/blur_region.c libs=pixman-1 */
#include "sf_test.h"
#include "effect/blur_region.h"

#include <pixman.h>
#include <wlr/util/box.h>

static void test_empty_and_invalid(void) {
	struct wlr_box out[4];
	pixman_region32_t empty;
	pixman_region32_init(&empty);

	SF_TEST_EQ_INT((int)sf_blur_region_boxes(NULL, 100, 100, out, 4), 0);
	SF_TEST_EQ_INT((int)sf_blur_region_boxes(&empty, 100, 100, out, 4), 0);
	SF_TEST_EQ_INT((int)sf_blur_region_boxes(&empty, 0, 100, out, 4), 0);
	SF_TEST_EQ_INT((int)sf_blur_region_boxes(&empty, 100, 100, NULL, 4), 0);
	SF_TEST_EQ_INT((int)sf_blur_region_boxes(&empty, 100, 100, out, 0), 0);

	pixman_region32_fini(&empty);
}

static void test_single_rect_clamped(void) {
	struct wlr_box out[4];
	pixman_region32_t region;
	pixman_region32_init(&region);
	pixman_region32_union_rect(&region, &region, -10, -5, 50, 40);

	SF_TEST_EQ_INT((int)sf_blur_region_boxes(&region, 100, 80, out, 4), 1);
	SF_TEST_EQ_INT(out[0].x, 0);
	SF_TEST_EQ_INT(out[0].y, 0);
	SF_TEST_EQ_INT(out[0].width, 40);
	SF_TEST_EQ_INT(out[0].height, 35);

	pixman_region32_fini(&region);
}

static void test_rect_clipped_to_surface(void) {
	struct wlr_box out[4];
	pixman_region32_t region;
	pixman_region32_init(&region);
	pixman_region32_union_rect(&region, &region, 80, 70, 50, 50);

	SF_TEST_EQ_INT((int)sf_blur_region_boxes(&region, 100, 100, out, 4), 1);
	SF_TEST_EQ_INT(out[0].x, 80);
	SF_TEST_EQ_INT(out[0].y, 70);
	SF_TEST_EQ_INT(out[0].width, 20);
	SF_TEST_EQ_INT(out[0].height, 30);

	pixman_region32_fini(&region);
}

static void test_cap_and_fully_outside(void) {
	struct wlr_box out[1];
	pixman_region32_t region;
	pixman_region32_init(&region);
	pixman_region32_union_rect(&region, &region, 0, 0, 10, 10);
	pixman_region32_union_rect(&region, &region, 20, 20, 10, 10);

	SF_TEST_EQ_INT((int)sf_blur_region_boxes(&region, 100, 100, out, 1), 1);
	SF_TEST_EQ_INT(out[0].x, 0);
	SF_TEST_EQ_INT(out[0].y, 0);

	pixman_region32_clear(&region);
	pixman_region32_union_rect(&region, &region, 200, 200, 10, 10);
	SF_TEST_EQ_INT((int)sf_blur_region_boxes(&region, 100, 100, out, 1), 0);

	pixman_region32_fini(&region);
}

static const struct sf_test tests[] = {
	{"empty_and_invalid", test_empty_and_invalid},
	{"single_rect_clamped", test_single_rect_clamped},
	{"rect_clipped_to_surface", test_rect_clipped_to_surface},
	{"cap_and_fully_outside", test_cap_and_fully_outside},
};

int main(void) {
	return sf_test_run(tests, sizeof(tests) / sizeof(tests[0])) == 0 ? 0 : 1;
}
