/* sf-suite cover=wm/layout.c src=wm/layout.c */
#include "sf_test.h"
#include "wm/workspace.h"

static void test_layout_parse(void) {
	SF_TEST_EQ_INT((int)sf_layout_parse(NULL), (int)SF_LAYOUT_STACKING);
	SF_TEST_EQ_INT((int)sf_layout_parse(""), (int)SF_LAYOUT_STACKING);
	SF_TEST_EQ_INT((int)sf_layout_parse("stacking"), (int)SF_LAYOUT_STACKING);
	SF_TEST_EQ_INT((int)sf_layout_parse("columns"), (int)SF_LAYOUT_COLUMNS);
	SF_TEST_EQ_INT((int)sf_layout_parse("tile"), (int)SF_LAYOUT_TILE);
	SF_TEST_EQ_INT((int)sf_layout_parse("unset"), (int)SF_LAYOUT_UNSET);
	SF_TEST_EQ_INT((int)sf_layout_parse("inherit"), (int)SF_LAYOUT_UNSET);
	SF_TEST_EQ_INT((int)sf_layout_parse("bogus"), (int)SF_LAYOUT_STACKING);
}

static void test_layout_name(void) {
	SF_TEST_STREQ(sf_layout_name(SF_LAYOUT_UNSET), "inherit");
	SF_TEST_STREQ(sf_layout_name(SF_LAYOUT_STACKING), "stacking");
	SF_TEST_STREQ(sf_layout_name(SF_LAYOUT_COLUMNS), "columns");
	SF_TEST_STREQ(sf_layout_name(SF_LAYOUT_TILE), "tile");
	SF_TEST_STREQ(sf_layout_name((enum sf_layout)99), "stacking");
}

static const struct sf_test tests[] = {
	{"sf_layout_parse", test_layout_parse},
	{"sf_layout_name", test_layout_name},
};

int main(void) {
	return sf_test_run(tests, sizeof(tests) / sizeof(tests[0])) == 0 ? 0 : 1;
}
