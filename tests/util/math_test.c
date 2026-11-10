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

static const struct sf_test tests[] = {
	{"sf_clamp01", test_clamp01},
};

int main(void) {
	return sf_test_run(tests, sizeof(tests) / sizeof(tests[0])) == 0 ? 0 : 1;
}
