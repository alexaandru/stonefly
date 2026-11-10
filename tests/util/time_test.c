/* sf-suite cover=util/time.h */
#include "sf_test.h"
#include "util/time.h"

static void test_timespec_ns(void) {
	struct timespec ts = {.tv_sec = 2, .tv_nsec = 5};

	SF_TEST_EQ_I64(sf_timespec_ns(&ts), 2000000005LL);

	ts.tv_sec = 0;
	ts.tv_nsec = 999999999;
	SF_TEST_EQ_I64(sf_timespec_ns(&ts), 999999999LL);
}

static void test_timespec_ms(void) {
	struct timespec ts = {.tv_sec = 3, .tv_nsec = 2500000};

	SF_TEST_EQ_I64(sf_timespec_ms(&ts), 3002LL);

	ts.tv_sec = 0;
	ts.tv_nsec = 999999;
	SF_TEST_EQ_I64(sf_timespec_ms(&ts), 0LL);

	ts.tv_nsec = 1000000;
	SF_TEST_EQ_I64(sf_timespec_ms(&ts), 1LL);
}

static void test_timespec_ms_since(void) {
	struct timespec then;

	SF_TEST_TRUE(clock_gettime(CLOCK_MONOTONIC, &then) == 0);
	int64_t dt = sf_timespec_ms_since(&then);
	/* Same tick or a few ms later on a loaded host. */
	SF_TEST_TRUE(dt >= 0);
	SF_TEST_TRUE(dt < 5000);

	then.tv_sec -= 2;
	dt = sf_timespec_ms_since(&then);
	SF_TEST_TRUE(dt >= 2000);
	SF_TEST_TRUE(dt < 7000);
}

static const struct sf_test tests[] = {
	{"sf_timespec_ns", test_timespec_ns},
	{"sf_timespec_ms", test_timespec_ms},
	{"sf_timespec_ms_since", test_timespec_ms_since},
};

int main(void) {
	return sf_test_run(tests, sizeof(tests) / sizeof(tests[0])) == 0 ? 0 : 1;
}
