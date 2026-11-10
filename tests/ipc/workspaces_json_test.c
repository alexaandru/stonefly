#include "sf_test.h"
#include "ipc/workspaces.h"

#include <stdio.h>
#include <string.h>

static void test_roundtrip(void) {
	struct sf_workspace_info in = {0}, out[2];
	char buf[1024];
	size_t n = 0;

	snprintf(in.output, sizeof(in.output), "%s", "eDP-1");
	in.index = 1;
	snprintf(in.name, sizeof(in.name), "%s", "1");
	in.active = true;
	in.empty = false;
	in.windows = 3;
	snprintf(in.layout, sizeof(in.layout), "%s", "stacking");

	SF_TEST_TRUE(sf_workspaces_to_json(&in, 1, buf, sizeof(buf)) > 0);
	SF_TEST_TRUE(sf_workspaces_from_json(buf, out, 2, &n));
	SF_TEST_EQ_INT((int)n, 1);
	SF_TEST_STREQ(out[0].output, "eDP-1");
	SF_TEST_EQ_INT(out[0].index, 1);
	SF_TEST_STREQ(out[0].name, "1");
	SF_TEST_TRUE(out[0].active);
	SF_TEST_TRUE(!out[0].empty);
	SF_TEST_EQ_INT(out[0].windows, 3);
	SF_TEST_STREQ(out[0].layout, "stacking");
}

static void test_empty_errors(void) {
	struct sf_workspace_info out[1];
	char buf[32];
	size_t n = 99;

	SF_TEST_TRUE(sf_workspaces_to_json(NULL, 0, buf, sizeof(buf)) > 0);
	SF_TEST_STREQ(buf, "[]");
	SF_TEST_TRUE(sf_workspaces_from_json("[]", out, 1, &n));
	SF_TEST_EQ_INT((int)n, 0);
	SF_TEST_TRUE(!sf_workspaces_from_json(NULL, out, 1, &n));
	SF_TEST_TRUE(!sf_workspaces_from_json("{}", out, 1, &n));
}

static const struct sf_test tests[] = {
	{ "sf_workspaces_json_roundtrip", test_roundtrip },
	{ "sf_workspaces_json_empty_errors", test_empty_errors },
};

int main(void) {
	return sf_test_run(tests, sizeof(tests) / sizeof(tests[0])) == 0 ? 0 : 1;
}
