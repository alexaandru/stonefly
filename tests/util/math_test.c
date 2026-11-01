/* sf-suite cover=util/math.h libs=m */
#include "sf_test.h"
#include "util/math.h"

static void test_clamp01(void) {
	SF_TEST_EQ_DOUBLE(sf_clamp01(0.0), 0.0, 0.0);
	SF_TEST_EQ_DOUBLE(sf_clamp01(1.0), 1.0, 0.0);
	SF_TEST_EQ_DOUBLE(sf_clamp01(0.5), 0.5, 0.0);
	SF_TEST_EQ_DOUBLE(sf_clamp01(-0.1), 0.0, 0.0);
	SF_TEST_EQ_DOUBLE(sf_clamp01(1.1), 1.0, 0.0);
	SF_TEST_EQ_DOUBLE(sf_clamp01(-100.0), 0.0, 0.0);
	SF_TEST_EQ_DOUBLE(sf_clamp01(100.0), 1.0, 0.0);
}

static void test_ease_out_cubic(void) {
	SF_TEST_EQ_DOUBLE(sf_ease_out_cubic(0.0), 0.0, 1e-12);
	SF_TEST_EQ_DOUBLE(sf_ease_out_cubic(1.0), 1.0, 1e-12);
	SF_TEST_EQ_DOUBLE(sf_ease_out_cubic(0.5), 0.875, 1e-12);
	SF_TEST_EQ_DOUBLE(sf_ease_out_cubic(-1.0), 0.0, 1e-12);
	SF_TEST_EQ_DOUBLE(sf_ease_out_cubic(2.0), 1.0, 1e-12);
}

static void test_lerp_i(void) {
	SF_TEST_EQ_INT(sf_lerp_i(0, 10, 0.0), 0);
	SF_TEST_EQ_INT(sf_lerp_i(0, 10, 1.0), 10);
	SF_TEST_EQ_INT(sf_lerp_i(0, 10, 0.5), 5);
	SF_TEST_EQ_INT(sf_lerp_i(0, 10, -1.0), 0);
	SF_TEST_EQ_INT(sf_lerp_i(0, 10, 2.0), 10);
	SF_TEST_EQ_INT(sf_lerp_i(10, 0, 0.5), 5);
}

/* Umbriel nestedRadiusStaysRounded / border_ring unit values. */
static void test_nested_radius(void) {
	SF_TEST_EQ_INT(sf_nested_radius(0, 4), 0);
	SF_TEST_EQ_INT(sf_nested_radius(4, 4), 2);
	SF_TEST_EQ_INT(sf_nested_radius(1, 9), 1);
	SF_TEST_EQ_INT(sf_nested_radius(10, 4), 7);
	SF_TEST_EQ_INT(sf_nested_radius(10, 0), 10);
	SF_TEST_EQ_INT(sf_outer_corner_radius(0, 3), 0);
	SF_TEST_EQ_INT(sf_outer_corner_radius(12, 3), 15);
	SF_TEST_EQ_INT(sf_outer_corner_radius(12, 0), 12);
}

static const struct sf_test tests[] = {
	{"sf_clamp01", test_clamp01},
	{"sf_ease_out_cubic", test_ease_out_cubic},
	{"sf_lerp_i", test_lerp_i},
	{"sf_nested_radius", test_nested_radius},
};

int main(void) {
	return sf_test_run(tests, sizeof(tests) / sizeof(tests[0])) == 0 ? 0 : 1;
}
