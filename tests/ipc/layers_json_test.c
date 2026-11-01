/* sf-suite cover=ipc/layers_json.c src=ipc/layers_json.c,util/json.c */
#include "sf_test.h"
#include "ipc/layers.h"

#include <stdio.h>
#include <string.h>

static void test_roundtrip(void) {
	struct sf_layer_info in = {0}, out[2];
	char buf[1024];
	size_t n = 0;

	snprintf(in.namespace, sizeof(in.namespace), "%s", "noctalia");
	snprintf(in.output, sizeof(in.output), "%s", "eDP-1");
	snprintf(in.layer, sizeof(in.layer), "%s", "top");
	snprintf(in.keyboard, sizeof(in.keyboard), "%s", "on_demand");
	in.x = 0;
	in.y = 0;
	in.w = 1920;
	in.h = 40;

	SF_TEST_TRUE(sf_layers_to_json(&in, 1, buf, sizeof(buf)) > 0);
	SF_TEST_TRUE(sf_layers_from_json(buf, out, 2, &n));
	SF_TEST_EQ_INT((int)n, 1);
	SF_TEST_STREQ(out[0].namespace, "noctalia");
	SF_TEST_STREQ(out[0].layer, "top");
	SF_TEST_EQ_INT(out[0].w, 1920);
	SF_TEST_EQ_INT(out[0].h, 40);
}

static void test_empty_errors(void) {
	struct sf_layer_info out[1];
	char buf[32];
	size_t n = 99;

	SF_TEST_TRUE(sf_layers_to_json(NULL, 0, buf, sizeof(buf)) > 0);
	SF_TEST_STREQ(buf, "[]");
	SF_TEST_TRUE(sf_layers_from_json("[]", out, 1, &n));
	SF_TEST_EQ_INT((int)n, 0);
	SF_TEST_TRUE(!sf_layers_from_json(NULL, out, 1, &n));
	SF_TEST_TRUE(!sf_layers_from_json("{}", out, 1, &n));
}

static const struct sf_test tests[] = {
	{"sf_layers_json_roundtrip", test_roundtrip},
	{"sf_layers_json_empty_errors", test_empty_errors},
};

int main(void) {
	return sf_test_run(tests, sizeof(tests) / sizeof(tests[0])) == 0 ? 0 : 1;
}
