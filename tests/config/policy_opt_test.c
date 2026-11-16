/* sf-suite cover=config/policy.c src=config/policy.c */
#include "sf_test.h"
#include "config/config.h"

static void test_client_maximize_parse(void) {
	SF_TEST_EQ_INT((int)sf_client_maximize_parse(NULL), (int)SF_CLIENT_MAXIMIZE_GAPS);
	SF_TEST_EQ_INT((int)sf_client_maximize_parse("gaps"), (int)SF_CLIENT_MAXIMIZE_GAPS);
	SF_TEST_EQ_INT((int)sf_client_maximize_parse("column"), (int)SF_CLIENT_MAXIMIZE_GAPS);
	SF_TEST_EQ_INT((int)sf_client_maximize_parse("edges"), (int)SF_CLIENT_MAXIMIZE_EDGES);
	SF_TEST_EQ_INT((int)sf_client_maximize_parse("edge"), (int)SF_CLIENT_MAXIMIZE_EDGES);
	SF_TEST_EQ_INT((int)sf_client_maximize_parse("nope"), (int)SF_CLIENT_MAXIMIZE_GAPS);
	SF_TEST_STREQ(sf_client_maximize_name(SF_CLIENT_MAXIMIZE_GAPS), "gaps");
	SF_TEST_STREQ(sf_client_maximize_name(SF_CLIENT_MAXIMIZE_EDGES), "edges");
}

static void test_unminimize_workspace_parse(void) {
	SF_TEST_EQ_INT((int)sf_unminimize_workspace_parse(NULL), (int)SF_UNMINIMIZE_WS_ACTIVE);
	SF_TEST_EQ_INT((int)sf_unminimize_workspace_parse("active"), (int)SF_UNMINIMIZE_WS_ACTIVE);
	SF_TEST_EQ_INT((int)sf_unminimize_workspace_parse("home"), (int)SF_UNMINIMIZE_WS_HOME);
	SF_TEST_EQ_INT((int)sf_unminimize_workspace_parse("nope"), (int)SF_UNMINIMIZE_WS_ACTIVE);
	SF_TEST_STREQ(sf_unminimize_workspace_name(SF_UNMINIMIZE_WS_ACTIVE), "active");
	SF_TEST_STREQ(sf_unminimize_workspace_name(SF_UNMINIMIZE_WS_HOME), "home");
}

static const struct sf_test tests[] = {
	{"sf_client_maximize_parse", test_client_maximize_parse},
	{"sf_unminimize_workspace_parse", test_unminimize_workspace_parse},
};

int main(void) {
	return sf_test_run(tests, sizeof(tests) / sizeof(tests[0])) == 0 ? 0 : 1;
}
