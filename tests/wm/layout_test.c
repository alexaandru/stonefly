/* sf-suite cover=wm/arrange_math.c src=wm/arrange_math.c */
#include "sf_test.h"
#include "wm/arrange_math.h"

static void test_arrange_parse(void) {
	SF_TEST_EQ_INT((int)sf_arrange_parse(NULL), (int)SF_ARRANGE_STACKING);
	SF_TEST_EQ_INT((int)sf_arrange_parse(""), (int)SF_ARRANGE_STACKING);
	SF_TEST_EQ_INT((int)sf_arrange_parse("stacking"), (int)SF_ARRANGE_STACKING);
	SF_TEST_EQ_INT((int)sf_arrange_parse("columns"), (int)SF_ARRANGE_COLUMNS);
	SF_TEST_EQ_INT((int)sf_arrange_parse("scroller"), (int)SF_ARRANGE_COLUMNS);
	SF_TEST_EQ_INT((int)sf_arrange_parse("tile"), (int)SF_ARRANGE_MASTER);
	SF_TEST_EQ_INT((int)sf_arrange_parse("master"), (int)SF_ARRANGE_MASTER);
	SF_TEST_EQ_INT((int)sf_arrange_parse("dwindle"), (int)SF_ARRANGE_DWINDLE);
	SF_TEST_EQ_INT((int)sf_arrange_parse("monocle"), (int)SF_ARRANGE_MONOCLE);
	SF_TEST_EQ_INT((int)sf_arrange_parse("unset"), (int)SF_ARRANGE_UNSET);
	SF_TEST_EQ_INT((int)sf_arrange_parse("inherit"), (int)SF_ARRANGE_UNSET);
	SF_TEST_EQ_INT((int)sf_arrange_parse("bogus"), (int)SF_ARRANGE_STACKING);
}

static void test_arrange_name(void) {
	SF_TEST_STREQ(sf_arrange_name(SF_ARRANGE_UNSET), "inherit");
	SF_TEST_STREQ(sf_arrange_name(SF_ARRANGE_STACKING), "stacking");
	SF_TEST_STREQ(sf_arrange_name(SF_ARRANGE_COLUMNS), "columns");
	SF_TEST_STREQ(sf_arrange_name(SF_ARRANGE_MASTER), "master");
	SF_TEST_STREQ(sf_arrange_name(SF_ARRANGE_DWINDLE), "dwindle");
	SF_TEST_STREQ(sf_arrange_name(SF_ARRANGE_MONOCLE), "monocle");
	SF_TEST_STREQ(sf_arrange_name((enum sf_arrange)99), "stacking");
}

static void test_arrange_is_tiling(void) {
	SF_TEST_TRUE(!sf_arrange_is_tiling(SF_ARRANGE_STACKING));
	SF_TEST_TRUE(sf_arrange_is_tiling(SF_ARRANGE_COLUMNS));
	SF_TEST_TRUE(sf_arrange_is_tiling(SF_ARRANGE_MASTER));
	SF_TEST_TRUE(sf_arrange_is_tiling(SF_ARRANGE_DWINDLE));
	SF_TEST_TRUE(sf_arrange_is_tiling(SF_ARRANGE_MONOCLE));
}

static const struct sf_test tests[] = {
	{"sf_arrange_parse", test_arrange_parse},
	{"sf_arrange_name", test_arrange_name},
	{"sf_arrange_is_tiling", test_arrange_is_tiling},
};

int main(void) {
	return sf_test_run(tests, sizeof(tests) / sizeof(tests[0])) == 0 ? 0 : 1;
}
