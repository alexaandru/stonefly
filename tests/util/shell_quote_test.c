#include "sf_test.h"
#include "util/shell_quote.h"

#include <stdlib.h>

static void test_shell_quote(void) {
	char *q;

	q = sf_shell_quote_dup("hello");
	SF_TEST_TRUE(q != NULL);
	SF_TEST_STREQ(q, "'hello'");
	free(q);

	q = sf_shell_quote_dup("");
	SF_TEST_TRUE(q != NULL);
	SF_TEST_STREQ(q, "''");
	free(q);

	q = sf_shell_quote_dup(NULL);
	SF_TEST_TRUE(q != NULL);
	SF_TEST_STREQ(q, "''");
	free(q);

	q = sf_shell_quote_dup("a'b");
	SF_TEST_TRUE(q != NULL);
	SF_TEST_STREQ(q, "'a'\\''b'");
	free(q);

	q = sf_shell_quote_dup("it's");
	SF_TEST_TRUE(q != NULL);
	SF_TEST_STREQ(q, "'it'\\''s'");
	free(q);

	q = sf_shell_quote_dup("a b");
	SF_TEST_TRUE(q != NULL);
	SF_TEST_STREQ(q, "'a b'");
	free(q);
}

static const struct sf_test tests[] = {
	{"sf_shell_quote_dup", test_shell_quote},
};

int main(void) {
	return sf_test_run(tests, sizeof(tests) / sizeof(tests[0])) == 0 ? 0 : 1;
}
