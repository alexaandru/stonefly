/* sf-suite cover=session/lock_frame.h */
#include "sf_test.h"
#include "session/lock_frame.h"

static void test_feedback(void) {
	struct sf_lock_frame a = {.submitted = true, .seq = 7};
	struct sf_lock_frame b = {.submitted = true, .seq = 12};
	SF_TEST_TRUE(sf_lock_frame_feedback(&a, 7, true));
	SF_TEST_TRUE(!sf_lock_frame_feedback(&b, 11, true));
	SF_TEST_TRUE(b.submitted && !b.presented);
	SF_TEST_TRUE(sf_lock_frame_feedback(&b, 12, false));
	SF_TEST_TRUE(!b.submitted && !b.presented);
	b = (struct sf_lock_frame){.submitted = true, .seq = 13};
	SF_TEST_TRUE(sf_lock_frame_feedback(&b, 13, true));
	SF_TEST_TRUE(a.presented && b.presented);
	b = (struct sf_lock_frame){0};
	SF_TEST_TRUE(!sf_lock_frame_feedback(&b, 13, true));
	SF_TEST_TRUE(!b.presented);
}

static const struct sf_test tests[] = {{"sf_lock_frame_feedback", test_feedback}};

int main(void) {
	return sf_test_run(tests, sizeof(tests) / sizeof(tests[0])) == 0 ? 0 : 1;
}
