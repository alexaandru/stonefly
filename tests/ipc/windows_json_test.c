/* sf-suite cover=ipc/windows_json.c src=ipc/windows_json.c,util/json.c */
#include "sf_test.h"
#include "ipc/windows.h"

#include <stdio.h>
#include <string.h>

static void test_roundtrip(void) {
	struct sf_window_info in = {0}, out[2];
	char buf[4096];
	size_t n = 0;

	in.id = 42;
	snprintf(in.app_id, sizeof(in.app_id), "%s", "foot");
	snprintf(in.title, sizeof(in.title), "%s", "hello");
	snprintf(in.tag, sizeof(in.tag), "%s", "browser");
	snprintf(in.description, sizeof(in.description), "%s", "desc");
	snprintf(in.icon_name, sizeof(in.icon_name), "%s", "org.foo");
	in.x = 10;
	in.y = 20;
	in.w = 800;
	in.h = 600;
	in.focused = true;
	in.floating = true;
	in.maximized_to_edges = true;
	in.minimized = true;
	in.snapped = true;
	in.urgent = true;
	in.capture_blocked = true;
	snprintf(in.snap_dir, sizeof(in.snap_dir), "%s", "left");
	in.workspace = 2;
	snprintf(in.output, sizeof(in.output), "%s", "eDP-1");

	SF_TEST_TRUE(sf_windows_to_json(&in, 1, buf, sizeof(buf)) > 0);
	SF_TEST_TRUE(sf_windows_from_json(buf, out, 2, &n));
	SF_TEST_EQ_INT((int)n, 1);
	SF_TEST_EQ_I64((int64_t)out[0].id, 42);
	SF_TEST_STREQ(out[0].app_id, "foot");
	SF_TEST_STREQ(out[0].title, "hello");
	SF_TEST_STREQ(out[0].tag, "browser");
	SF_TEST_STREQ(out[0].description, "desc");
	SF_TEST_STREQ(out[0].icon_name, "org.foo");
	SF_TEST_EQ_INT(out[0].x, 10);
	SF_TEST_EQ_INT(out[0].y, 20);
	SF_TEST_EQ_INT(out[0].w, 800);
	SF_TEST_EQ_INT(out[0].h, 600);
	SF_TEST_TRUE(out[0].focused);
	SF_TEST_TRUE(out[0].floating);
	SF_TEST_TRUE(out[0].maximized_to_edges);
	SF_TEST_TRUE(out[0].minimized);
	SF_TEST_TRUE(out[0].snapped);
	SF_TEST_TRUE(out[0].urgent);
	SF_TEST_TRUE(out[0].capture_blocked);
	SF_TEST_STREQ(out[0].snap_dir, "left");
	SF_TEST_EQ_INT(out[0].workspace, 2);
	SF_TEST_STREQ(out[0].output, "eDP-1");
}

static void test_empty_and_errors(void) {
	struct sf_window_info out[1];
	char buf[64];
	size_t n = 99;

	SF_TEST_TRUE(sf_windows_to_json(NULL, 0, buf, sizeof(buf)) > 0);
	SF_TEST_STREQ(buf, "[]");
	SF_TEST_TRUE(sf_windows_from_json("[]", out, 1, &n));
	SF_TEST_EQ_INT((int)n, 0);
	SF_TEST_TRUE(!sf_windows_from_json(NULL, out, 1, &n));
	SF_TEST_TRUE(!sf_windows_from_json("{}", out, 1, &n));
	SF_TEST_TRUE(sf_windows_to_json(NULL, 0, buf, 2) < 0);

	/* Unknown keys are skipped; empty object is valid. */
	SF_TEST_TRUE(sf_windows_from_json("[{\"extra\":\"x\",\"n\":1,\"ok\":true}]", out, 1, &n));
	SF_TEST_EQ_INT((int)n, 1);
	SF_TEST_TRUE(!sf_windows_from_json("[{},{}]", out, 1, &n));
}

static const struct sf_test tests[] = {
	{"sf_windows_json_roundtrip", test_roundtrip},
	{"sf_windows_json_empty_errors", test_empty_and_errors},
};

int main(void) {
	return sf_test_run(tests, sizeof(tests) / sizeof(tests[0])) == 0 ? 0 : 1;
}
