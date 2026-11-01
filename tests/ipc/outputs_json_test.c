/* sf-suite cover=ipc/outputs_json.c src=ipc/outputs_json.c,util/json.c libs=m */
#include "sf_test.h"
#include "ipc/outputs.h"

#include <stdio.h>
#include <string.h>

static void test_roundtrip(void) {
	struct sf_output_info in = {0}, out[2];
	char buf[2048];
	size_t n = 0;

	snprintf(in.name, sizeof(in.name), "%s", "DP-1");
	in.enabled = true;
	in.x = 0;
	in.y = 0;
	in.w = 1920;
	in.h = 1080;
	in.logical_w = 1920;
	in.logical_h = 1080;
	in.scale = 1.25;
	in.usable_x = 0;
	in.usable_y = 32;
	in.usable_w = 1920;
	in.usable_h = 1048;
	in.focused = true;

	SF_TEST_TRUE(sf_outputs_to_json(&in, 1, buf, sizeof(buf)) > 0);
	SF_TEST_TRUE(sf_outputs_from_json(buf, out, 2, &n));
	SF_TEST_EQ_INT((int)n, 1);
	SF_TEST_STREQ(out[0].name, "DP-1");
	SF_TEST_TRUE(out[0].enabled);
	SF_TEST_EQ_INT(out[0].w, 1920);
	SF_TEST_EQ_INT(out[0].h, 1080);
	SF_TEST_EQ_DOUBLE(out[0].scale, 1.25, 1e-6);
	SF_TEST_EQ_INT(out[0].usable_y, 32);
	SF_TEST_TRUE(out[0].focused);
}

static void test_empty(void) {
	char buf[32];
	struct sf_output_info out[1];
	size_t n = 1;

	SF_TEST_TRUE(sf_outputs_to_json(NULL, 0, buf, sizeof(buf)) > 0);
	SF_TEST_STREQ(buf, "[]");
	SF_TEST_TRUE(sf_outputs_from_json("[]", out, 1, &n));
	SF_TEST_EQ_INT((int)n, 0);
	SF_TEST_TRUE(!sf_outputs_from_json(NULL, out, 1, &n));
	SF_TEST_TRUE(!sf_outputs_from_json("{}", out, 1, &n));
	SF_TEST_TRUE(sf_outputs_to_json(NULL, 0, buf, 2) < 0);
}

static const struct sf_test tests[] = {
	{"sf_outputs_json_roundtrip", test_roundtrip},
	{"sf_outputs_json_empty", test_empty},
};

int main(void) {
	return sf_test_run(tests, sizeof(tests) / sizeof(tests[0])) == 0 ? 0 : 1;
}
