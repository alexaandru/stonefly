/* sf-suite cover=config/action.c src=config/action.c,util/str.c stub=log */
#include "sf_test.h"
#include "config/action.h"

#include <stdlib.h>
#include <string.h>

static void test_parse_name_roundtrip(void) {
	enum sf_action a = SF_ACTION_NONE;

	SF_TEST_TRUE(!sf_action_parse_ok(NULL, &a));
	SF_TEST_TRUE(!sf_action_parse_ok("close", NULL));
	SF_TEST_TRUE(!sf_action_parse_ok("not-an-action", &a));

	SF_TEST_TRUE(sf_action_parse_ok("none", &a));
	SF_TEST_EQ_INT((int)a, (int)SF_ACTION_NONE);
	SF_TEST_TRUE(sf_action_parse_ok("close", &a));
	SF_TEST_EQ_INT((int)a, (int)SF_ACTION_CLOSE);
	SF_TEST_TRUE(sf_action_parse_ok("exec", &a));
	SF_TEST_EQ_INT((int)a, (int)SF_ACTION_EXEC);
	SF_TEST_TRUE(sf_action_parse_ok("workspace_switch", &a));
	SF_TEST_EQ_INT((int)a, (int)SF_ACTION_WORKSPACE_SWITCH);

	SF_TEST_EQ_INT((int)sf_action_parse("maximize"), (int)SF_ACTION_MAXIMIZE);
	SF_TEST_EQ_INT((int)sf_action_parse("bogus"), (int)SF_ACTION_NONE);
	SF_TEST_EQ_INT((int)sf_action_parse(NULL), (int)SF_ACTION_NONE);

	SF_TEST_TRUE(sf_action_parse_ok("snap_left", &a));
	SF_TEST_EQ_INT((int)a, (int)SF_ACTION_SNAP_LEFT);
	SF_TEST_TRUE(sf_action_parse_ok("move_to_workspace", &a));
	SF_TEST_EQ_INT((int)a, (int)SF_ACTION_MOVE_TO_WORKSPACE);
	SF_TEST_TRUE(sf_action_parse_ok("scratch_claim", &a));
	SF_TEST_EQ_INT((int)a, (int)SF_ACTION_SCRATCH_CLAIM);

	SF_TEST_STREQ(sf_action_name(SF_ACTION_QUIT), "quit");
	SF_TEST_STREQ(sf_action_name(SF_ACTION_SCRATCH_TOGGLE), "scratch_toggle");
	SF_TEST_STREQ(sf_action_name(SF_ACTION_FOCUS_NEXT_ALL), "focus_next_all");
	SF_TEST_STREQ(sf_action_name(SF_ACTION_WORKSPACE_NEXT), "workspace_next");
	SF_TEST_STREQ(sf_action_name((enum sf_action)9999), "unknown");
}

static void test_steps_one_dup_free(void) {
	struct sf_action_step *steps = NULL;
	size_t n = 99;

	SF_TEST_TRUE(sf_action_steps_one(SF_ACTION_EXEC, "echo hi", &steps, &n));
	SF_TEST_EQ_INT((int)n, 1);
	SF_TEST_TRUE(steps != NULL);
	SF_TEST_EQ_INT((int)steps[0].action, (int)SF_ACTION_EXEC);
	SF_TEST_STREQ(steps[0].cmd, "echo hi");

	struct sf_action_step *copy = NULL;
	size_t cn = 0;
	SF_TEST_TRUE(sf_action_steps_dup(steps, n, &copy, &cn));
	SF_TEST_EQ_INT((int)cn, 1);
	SF_TEST_STREQ(copy[0].cmd, "echo hi");
	SF_TEST_TRUE(copy[0].cmd != steps[0].cmd);

	sf_action_steps_free(copy, cn);
	sf_action_steps_free(steps, n);
	sf_action_steps_free(NULL, 0);

	steps = (struct sf_action_step *)0x1;
	n = 99;
	SF_TEST_TRUE(sf_action_steps_dup(NULL, 0, &steps, &n));
	SF_TEST_TRUE(steps == NULL);
	SF_TEST_EQ_INT((int)n, 0);

	SF_TEST_TRUE(!sf_action_steps_dup(NULL, 1, NULL, &n));
	SF_TEST_TRUE(!sf_action_steps_one(SF_ACTION_CLOSE, NULL, NULL, &n));

	SF_TEST_TRUE(sf_action_steps_one(SF_ACTION_CLOSE, NULL, &steps, &n));
	SF_TEST_EQ_INT((int)n, 1);
	SF_TEST_TRUE(steps[0].cmd == NULL);
	sf_action_steps_free(steps, n);
}

static const struct sf_test tests[] = {
	{"sf_action_parse_name", test_parse_name_roundtrip},
	{"sf_action_steps", test_steps_one_dup_free},
};

int main(void) {
	return sf_test_run(tests, sizeof(tests) / sizeof(tests[0])) == 0 ? 0 : 1;
}
