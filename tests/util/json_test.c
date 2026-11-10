#include "sf_test.h"
#include "util/json.h"

#include <string.h>

static void test_writer_basics(void) {
	char buf[128];
	struct json_buf b;

	json_buf_init(&b, buf, sizeof(buf));
	SF_TEST_TRUE(json_ok(&b));
	json_byte(&b, '{');
	json_key(&b, "cmd");
	json_str(&b, "ping");
	json_byte(&b, ',');
	json_key(&b, "n");
	json_i64(&b, 42);
	json_byte(&b, ',');
	json_key(&b, "ok");
	json_bool(&b, true);
	json_byte(&b, ',');
	json_key(&b, "x");
	json_f64(&b, 1.5);
	json_byte(&b, '}');
	json_finish(&b);
	SF_TEST_TRUE(json_ok(&b));
	SF_TEST_STREQ(buf, "{\"cmd\":\"ping\",\"n\":42,\"ok\":true,\"x\":1.5}");
}

static void test_writer_escape(void) {
	char buf[128];
	struct json_buf b;

	json_buf_init(&b, buf, sizeof(buf));
	json_str(&b, "a\"b\\c\n\t\r\b\f");
	json_finish(&b);
	SF_TEST_TRUE(json_ok(&b));
	SF_TEST_STREQ(buf, "\"a\\\"b\\\\c\\n\\t\\r\\b\\f\"");

	json_buf_init(&b, buf, sizeof(buf));
	json_str(&b, NULL);
	json_finish(&b);
	SF_TEST_STREQ(buf, "\"\"");

	json_buf_init(&b, buf, sizeof(buf));
	json_str(&b, "");
	json_finish(&b);
	SF_TEST_STREQ(buf, "\"\"");

	json_buf_init(&b, buf, sizeof(buf));
	json_str(&b, "\x01");
	json_finish(&b);
	SF_TEST_TRUE(json_ok(&b));
	SF_TEST_STREQ(buf, "\"\\u0001\"");
}

static void test_writer_overflow(void) {
	char buf[8];
	struct json_buf b;

	json_buf_init(&b, buf, sizeof(buf));
	json_raw(&b, "abcdefgh");
	SF_TEST_TRUE(!json_ok(&b));
	SF_TEST_TRUE(b.err);

	json_buf_init(&b, NULL, 0);
	SF_TEST_TRUE(!json_ok(&b));
	json_byte(&b, 'x');
	SF_TEST_TRUE(b.err);
}

static void test_scan_string_bool_i64(void) {
	struct json_scan s;
	char out[32];
	bool flag = false;
	int64_t n = 0;

	json_scan_init(&s, "  \"hi\\n\" ");
	SF_TEST_TRUE(json_scan_string(&s, out, sizeof(out)));
	SF_TEST_STREQ(out, "hi\n");
	SF_TEST_TRUE(json_scan_ok(&s));

	json_scan_init(&s, "\"a\\u0041b\"");
	SF_TEST_TRUE(json_scan_string(&s, out, sizeof(out)));
	SF_TEST_STREQ(out, "a?b");

	json_scan_init(&s, "\"\\\\\\\"\\/\\b\\f\\r\\t\"");
	SF_TEST_TRUE(json_scan_string(&s, out, sizeof(out)));
	SF_TEST_STREQ(out, "\\\"/\b\f\r\t");

	json_scan_init(&s, " true ");
	SF_TEST_TRUE(json_scan_bool(&s, &flag));
	SF_TEST_TRUE(flag);
	json_scan_init(&s, "false");
	SF_TEST_TRUE(json_scan_bool(&s, &flag));
	SF_TEST_TRUE(!flag);

	json_scan_init(&s, " -42 ");
	SF_TEST_TRUE(json_scan_i64(&s, &n));
	SF_TEST_EQ_I64(n, -42);

	json_scan_init(&s, "0");
	SF_TEST_TRUE(json_scan_i64(&s, &n));
	SF_TEST_EQ_I64(n, 0);
}

static void test_scan_object(void) {
	struct json_scan s;
	char cmd[32];
	int64_t n = 0;

	json_scan_init(&s, "{ \"cmd\" : \"focus\" , \"id\" : 7 }");
	SF_TEST_TRUE(json_scan_expect(&s, '{'));
	SF_TEST_TRUE(json_scan_string(&s, cmd, sizeof(cmd)));
	SF_TEST_STREQ(cmd, "cmd");
	SF_TEST_TRUE(json_scan_expect(&s, ':'));
	SF_TEST_TRUE(json_scan_string(&s, cmd, sizeof(cmd)));
	SF_TEST_STREQ(cmd, "focus");
	SF_TEST_TRUE(json_scan_expect(&s, ','));
	SF_TEST_TRUE(json_scan_string(&s, cmd, sizeof(cmd)));
	SF_TEST_STREQ(cmd, "id");
	SF_TEST_TRUE(json_scan_expect(&s, ':'));
	SF_TEST_TRUE(json_scan_i64(&s, &n));
	SF_TEST_EQ_I64(n, 7);
	SF_TEST_TRUE(json_scan_expect(&s, '}'));
	SF_TEST_TRUE(json_scan_ok(&s));
}

static void test_scan_errors(void) {
	struct json_scan s;
	char out[4];
	bool flag = true;
	int64_t n = 0;

	json_scan_init(&s, "nope");
	SF_TEST_TRUE(!json_scan_expect(&s, '{'));
	SF_TEST_TRUE(!json_scan_ok(&s));

	json_scan_init(&s, "\"too-long\"");
	SF_TEST_TRUE(!json_scan_string(&s, out, sizeof(out)));

	json_scan_init(&s, "tru");
	SF_TEST_TRUE(!json_scan_bool(&s, &flag));

	json_scan_init(&s, "x");
	SF_TEST_TRUE(!json_scan_i64(&s, &n));

	json_scan_init(&s, "\"\\u12\"");
	SF_TEST_TRUE(!json_scan_string(&s, out, sizeof(out)));

	json_scan_init(&s, "\"\\q\"");
	SF_TEST_TRUE(!json_scan_string(&s, out, sizeof(out)));

	json_scan_init(&s, "\"unterminated");
	SF_TEST_TRUE(!json_scan_string(&s, out, sizeof(out)));

	json_scan_init(&s, NULL);
	SF_TEST_TRUE(json_scan_ok(&s));
	SF_TEST_TRUE(!json_scan_expect(&s, '{'));
}

static const struct sf_test tests[] = {
	{"json_writer_basics", test_writer_basics},
	{"json_writer_escape", test_writer_escape},
	{"json_writer_overflow", test_writer_overflow},
	{"json_scan_string_bool_i64", test_scan_string_bool_i64},
	{"json_scan_object", test_scan_object},
	{"json_scan_errors", test_scan_errors},
};

int main(void) {
	return sf_test_run(tests, sizeof(tests) / sizeof(tests[0])) == 0 ? 0 : 1;
}
