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

static void test_anim_policy(void) {
	SF_TEST_EQ_INT((int)sf_anim_style_parse(NULL), (int)SF_ANIM_STYLE_DEFAULT);
	SF_TEST_EQ_INT((int)sf_anim_style_parse("auto"), (int)SF_ANIM_STYLE_DEFAULT);
	SF_TEST_EQ_INT((int)sf_anim_style_parse("popin"), (int)SF_ANIM_STYLE_POPIN);
	SF_TEST_EQ_INT((int)sf_anim_style_parse("zoom"), (int)SF_ANIM_STYLE_POPIN);
	SF_TEST_EQ_INT((int)sf_anim_style_parse("slide"), (int)SF_ANIM_STYLE_SLIDE);
	SF_TEST_EQ_INT((int)sf_anim_style_parse("fade"), (int)SF_ANIM_STYLE_FADE);
	SF_TEST_EQ_INT((int)sf_anim_style_parse("none"), (int)SF_ANIM_STYLE_NONE);
	SF_TEST_EQ_INT((int)sf_anim_style_parse("off"), (int)SF_ANIM_STYLE_NONE);
	SF_TEST_EQ_INT((int)sf_anim_style_parse("unknown"), (int)SF_ANIM_STYLE_DEFAULT);

	SF_TEST_STREQ(sf_anim_style_name(SF_ANIM_STYLE_DEFAULT), "default");
	SF_TEST_STREQ(sf_anim_style_name(SF_ANIM_STYLE_POPIN), "popin");
	SF_TEST_STREQ(sf_anim_style_name(SF_ANIM_STYLE_SLIDE), "slide");
	SF_TEST_STREQ(sf_anim_style_name(SF_ANIM_STYLE_FADE), "fade");
	SF_TEST_STREQ(sf_anim_style_name(SF_ANIM_STYLE_NONE), "none");

	struct sf_config cfg = {0};
	cfg.anim.enabled = true;
	struct sf_anim_op op = {.enabled = true, .duration_ms = -1, .style = SF_ANIM_STYLE_DEFAULT};
	SF_TEST_EQ_INT(sf_anim_op_duration_ms(&cfg, &op, 250), 250);
	op.duration_ms = 120;
	SF_TEST_EQ_INT(sf_anim_op_duration_ms(&cfg, &op, 250), 120);
	op.style = SF_ANIM_STYLE_NONE;
	SF_TEST_EQ_INT(sf_anim_op_duration_ms(&cfg, &op, 250), 0);
	op.style = SF_ANIM_STYLE_POPIN;
	op.enabled = false;
	SF_TEST_EQ_INT(sf_anim_op_duration_ms(&cfg, &op, 250), 0);
	op.enabled = true;
	cfg.anim.enabled = false;
	SF_TEST_EQ_INT(sf_anim_op_duration_ms(&cfg, &op, 250), 0);
}

static const struct sf_test tests[] = {
	{"sf_client_maximize_parse", test_client_maximize_parse},
	{"sf_unminimize_workspace_parse", test_unminimize_workspace_parse},
	{"sf_anim_policy", test_anim_policy},
};

int main(void) {
	return sf_test_run(tests, sizeof(tests) / sizeof(tests[0])) == 0 ? 0 : 1;
}
