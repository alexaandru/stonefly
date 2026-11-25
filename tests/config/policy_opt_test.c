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

static void test_focus_cycle_parse(void) {
	SF_TEST_EQ_INT((int)sf_focus_cycle_parse(NULL), (int)SF_FOCUS_CYCLE_SPATIAL);
	SF_TEST_EQ_INT((int)sf_focus_cycle_parse("spatial"), (int)SF_FOCUS_CYCLE_SPATIAL);
	SF_TEST_EQ_INT((int)sf_focus_cycle_parse("mru"), (int)SF_FOCUS_CYCLE_MRU);
	SF_TEST_EQ_INT((int)sf_focus_cycle_parse("strip"), (int)SF_FOCUS_CYCLE_STRIP);
	SF_TEST_EQ_INT((int)sf_focus_cycle_parse("nope"), (int)SF_FOCUS_CYCLE_SPATIAL);
	SF_TEST_STREQ(sf_focus_cycle_name(SF_FOCUS_CYCLE_SPATIAL), "spatial");
	SF_TEST_STREQ(sf_focus_cycle_name(SF_FOCUS_CYCLE_MRU), "mru");
	SF_TEST_STREQ(sf_focus_cycle_name(SF_FOCUS_CYCLE_STRIP), "strip");
}

static void test_pan_focus_parse(void) {
	SF_TEST_EQ_INT((int)sf_pan_focus_parse(NULL), (int)SF_PAN_FOCUS_CENTER);
	SF_TEST_EQ_INT((int)sf_pan_focus_parse("center"), (int)SF_PAN_FOCUS_CENTER);
	SF_TEST_EQ_INT((int)sf_pan_focus_parse("centre"), (int)SF_PAN_FOCUS_CENTER);
	SF_TEST_EQ_INT((int)sf_pan_focus_parse("keep"), (int)SF_PAN_FOCUS_KEEP);
	SF_TEST_EQ_INT((int)sf_pan_focus_parse("warp"), (int)SF_PAN_FOCUS_WARP);
	SF_TEST_EQ_INT((int)sf_pan_focus_parse("under_pointer"), (int)SF_PAN_FOCUS_UNDER_POINTER);
	SF_TEST_EQ_INT((int)sf_pan_focus_parse("pointer"), (int)SF_PAN_FOCUS_UNDER_POINTER);
	SF_TEST_EQ_INT((int)sf_pan_focus_parse("nope"), (int)SF_PAN_FOCUS_CENTER);
	SF_TEST_STREQ(sf_pan_focus_name(SF_PAN_FOCUS_CENTER), "center");
	SF_TEST_STREQ(sf_pan_focus_name(SF_PAN_FOCUS_KEEP), "keep");
	SF_TEST_STREQ(sf_pan_focus_name(SF_PAN_FOCUS_WARP), "warp");
	SF_TEST_STREQ(sf_pan_focus_name(SF_PAN_FOCUS_UNDER_POINTER), "under_pointer");
}

static void test_tile_insert_parse(void) {
	SF_TEST_EQ_INT((int)sf_tile_insert_parse(NULL), (int)SF_TILE_INSERT_AFTER_FOCUS);
	SF_TEST_EQ_INT((int)sf_tile_insert_parse("after_focus"), (int)SF_TILE_INSERT_AFTER_FOCUS);
	SF_TEST_EQ_INT((int)sf_tile_insert_parse("end"), (int)SF_TILE_INSERT_END);
	SF_TEST_EQ_INT((int)sf_tile_insert_parse("begin"), (int)SF_TILE_INSERT_BEGIN);
	SF_TEST_EQ_INT((int)sf_tile_insert_parse("nope"), (int)SF_TILE_INSERT_AFTER_FOCUS);
	SF_TEST_STREQ(sf_tile_insert_name(SF_TILE_INSERT_AFTER_FOCUS), "after_focus");
	SF_TEST_STREQ(sf_tile_insert_name(SF_TILE_INSERT_END), "end");
	SF_TEST_STREQ(sf_tile_insert_name(SF_TILE_INSERT_BEGIN), "begin");
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
	{"sf_focus_cycle_parse", test_focus_cycle_parse},
	{"sf_pan_focus_parse", test_pan_focus_parse},
	{"sf_tile_insert_parse", test_tile_insert_parse},
	{"sf_anim_policy", test_anim_policy},
};

int main(void) {
	return sf_test_run(tests, sizeof(tests) / sizeof(tests[0])) == 0 ? 0 : 1;
}
