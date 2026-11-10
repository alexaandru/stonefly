#include "sf_test.h"
#include "ipc/parse.h"

#include <string.h>

static void test_parse_cmd_arg(void) {
	char cmd[64];
	char arg[64];

	SF_TEST_TRUE(sf_ipc_parse_cmd_arg("{\"cmd\":\"ping\"}", cmd, sizeof(cmd), arg, sizeof(arg)));
	SF_TEST_STREQ(cmd, "ping");
	SF_TEST_STREQ(arg, "");

	SF_TEST_TRUE(sf_ipc_parse_cmd_arg("{\"cmd\":\"focus\",\"arg\":\"42\"}", cmd, sizeof(cmd),
					  arg, sizeof(arg)));
	SF_TEST_STREQ(cmd, "focus");
	SF_TEST_STREQ(arg, "42");

	SF_TEST_TRUE(sf_ipc_parse_cmd_arg("{\"arg\":\"7\",\"cmd\":\"close\"}", cmd, sizeof(cmd),
					  arg, sizeof(arg)));
	SF_TEST_STREQ(cmd, "close");
	SF_TEST_STREQ(arg, "7");

	SF_TEST_TRUE(!sf_ipc_parse_cmd_arg("{}", cmd, sizeof(cmd), arg, sizeof(arg)));
	SF_TEST_TRUE(!sf_ipc_parse_cmd_arg("[]", cmd, sizeof(cmd), arg, sizeof(arg)));
	SF_TEST_TRUE(!sf_ipc_parse_cmd_arg("{\"cmd\":\"\"}", cmd, sizeof(cmd), arg, sizeof(arg)));
	SF_TEST_TRUE(!sf_ipc_parse_cmd_arg(NULL, cmd, sizeof(cmd), arg, sizeof(arg)));

	SF_TEST_TRUE(sf_ipc_parse_cmd_arg("{\"cmd\":\"x\"}", cmd, sizeof(cmd), NULL, 0));
	SF_TEST_STREQ(cmd, "x");
}

static void test_parse_id_arg(void) {
	uint64_t id = 0;

	SF_TEST_TRUE(sf_ipc_parse_id_arg("0", &id));
	SF_TEST_EQ_I64((int64_t)id, 0);
	SF_TEST_TRUE(sf_ipc_parse_id_arg("42", &id));
	SF_TEST_EQ_I64((int64_t)id, 42);
	SF_TEST_TRUE(!sf_ipc_parse_id_arg("", &id));
	SF_TEST_TRUE(!sf_ipc_parse_id_arg(NULL, &id));
	SF_TEST_TRUE(!sf_ipc_parse_id_arg("42", NULL));
	SF_TEST_TRUE(!sf_ipc_parse_id_arg("12x", &id));
	SF_TEST_TRUE(!sf_ipc_parse_id_arg("42 ", &id));
}

static void test_parse_workspace_index(void) {
	int idx = 0;

	SF_TEST_TRUE(sf_ipc_parse_workspace_index("1", &idx));
	SF_TEST_EQ_INT(idx, 1);
	SF_TEST_TRUE(sf_ipc_parse_workspace_index("12", &idx));
	SF_TEST_EQ_INT(idx, 12);
	SF_TEST_TRUE(!sf_ipc_parse_workspace_index("0", &idx));
	SF_TEST_TRUE(!sf_ipc_parse_workspace_index("-3", &idx));
	SF_TEST_TRUE(!sf_ipc_parse_workspace_index("", &idx));
	SF_TEST_TRUE(!sf_ipc_parse_workspace_index(NULL, &idx));
	SF_TEST_TRUE(!sf_ipc_parse_workspace_index("1", NULL));
	SF_TEST_TRUE(!sf_ipc_parse_workspace_index("2a", &idx));
}

static const struct sf_test tests[] = {
	{"sf_ipc_parse_cmd_arg", test_parse_cmd_arg},
	{"sf_ipc_parse_id_arg", test_parse_id_arg},
	{"sf_ipc_parse_workspace_index", test_parse_workspace_index},
};

int main(void) {
	return sf_test_run(tests, sizeof(tests) / sizeof(tests[0])) == 0 ? 0 : 1;
}
