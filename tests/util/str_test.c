/* sf-suite cover=util/str.c src=util/str.c stub=log */
#include "sf_test.h"
#include "util/str.h"

#include <stdlib.h>
#include <string.h>

static void test_xstrdup(void) {
	char *p;

	SF_TEST_TRUE(sf_xstrdup(NULL) == NULL);

	p = sf_xstrdup("");
	SF_TEST_TRUE(p != NULL);
	SF_TEST_STREQ(p, "");
	free(p);

	p = sf_xstrdup("stonefly");
	SF_TEST_TRUE(p != NULL);
	SF_TEST_STREQ(p, "stonefly");
	free(p);
}

static void test_replace_str(void) {
	char *dst = NULL;

	sf_replace_str(&dst, "one");
	SF_TEST_STREQ(dst, "one");

	sf_replace_str(&dst, "two");
	SF_TEST_STREQ(dst, "two");

	sf_replace_str(&dst, NULL);
	SF_TEST_TRUE(dst == NULL);

	sf_replace_str(&dst, "again");
	SF_TEST_STREQ(dst, "again");
	free(dst);
}

static const struct sf_test tests[] = {
	{"sf_xstrdup", test_xstrdup},
	{"sf_replace_str", test_replace_str},
};

int main(void) {
	return sf_test_run(tests, sizeof(tests) / sizeof(tests[0])) == 0 ? 0 : 1;
}
