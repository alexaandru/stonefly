/* sf-suite cover=wm/snap.h */
#include "sf_test.h"
#include "wm/snap.h"

static void test_snap_name(void) {
	SF_TEST_STREQ(sf_snap_name(SF_SNAP_NONE), "");
	SF_TEST_STREQ(sf_snap_name(SF_SNAP_LEFT), "left");
	SF_TEST_STREQ(sf_snap_name(SF_SNAP_RIGHT), "right");
	SF_TEST_STREQ(sf_snap_name(SF_SNAP_TOP), "top");
	SF_TEST_STREQ(sf_snap_name(SF_SNAP_BOTTOM), "bottom");
	SF_TEST_STREQ(sf_snap_name((enum sf_snap)99), "");
}

static const struct sf_test tests[] = {
	{"sf_snap_name", test_snap_name},
};

int main(void) {
	return sf_test_run(tests, sizeof(tests) / sizeof(tests[0])) == 0 ? 0 : 1;
}
