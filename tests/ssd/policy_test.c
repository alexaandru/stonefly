/* sf-suite cover=ssd/ssd_policy.c src=ssd/ssd_policy.c */
#include "sf_test.h"
#include "ssd/ssd_policy.h"

#include <wlr/types/wlr_xdg_decoration_v1.h>

static void test_mode_parse_name(void) {
	SF_TEST_EQ_INT((int)sf_ssd_mode_parse(NULL), (int)SF_SSD_AUTO);
	SF_TEST_EQ_INT((int)sf_ssd_mode_parse("never"), (int)SF_SSD_NEVER);
	SF_TEST_EQ_INT((int)sf_ssd_mode_parse("auto"), (int)SF_SSD_AUTO);
	SF_TEST_EQ_INT((int)sf_ssd_mode_parse("prefer_ssd"), (int)SF_SSD_PREFER);
	SF_TEST_EQ_INT((int)sf_ssd_mode_parse("always"), (int)SF_SSD_ALWAYS);
	SF_TEST_EQ_INT((int)sf_ssd_mode_parse("nope"), (int)SF_SSD_AUTO);

	SF_TEST_STREQ(sf_ssd_mode_name(SF_SSD_NEVER), "never");
	SF_TEST_STREQ(sf_ssd_mode_name(SF_SSD_AUTO), "auto");
	SF_TEST_STREQ(sf_ssd_mode_name(SF_SSD_PREFER), "prefer_ssd");
	SF_TEST_STREQ(sf_ssd_mode_name(SF_SSD_ALWAYS), "always");
}

static void test_ssd_want(void) {
	int client = WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE;
	int server = WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE;
	int none = WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_NONE;

	SF_TEST_TRUE(!sf_ssd_want(SF_SSD_NEVER, client));
	SF_TEST_TRUE(!sf_ssd_want(SF_SSD_NEVER, server));
	SF_TEST_TRUE(sf_ssd_want(SF_SSD_ALWAYS, client));
	SF_TEST_TRUE(sf_ssd_want(SF_SSD_ALWAYS, none));

	SF_TEST_TRUE(!sf_ssd_want(SF_SSD_PREFER, client));
	SF_TEST_TRUE(sf_ssd_want(SF_SSD_PREFER, server));
	SF_TEST_TRUE(sf_ssd_want(SF_SSD_PREFER, none));

	SF_TEST_TRUE(!sf_ssd_want(SF_SSD_AUTO, client));
	SF_TEST_TRUE(sf_ssd_want(SF_SSD_AUTO, server));
	SF_TEST_TRUE(sf_ssd_want(SF_SSD_AUTO, none));
}

static const struct sf_test tests[] = {
	{"sf_ssd_mode_parse_name", test_mode_parse_name},
	{"sf_ssd_want", test_ssd_want},
};

int main(void) {
	return sf_test_run(tests, sizeof(tests) / sizeof(tests[0])) == 0 ? 0 : 1;
}
