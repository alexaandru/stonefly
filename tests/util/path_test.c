#include "sf_test.h"
#include "util/path.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void rm_tree(const char *root) {
	char path[512];
	const char *parts[] = {"a/b/c", "a/b", "a", "d/e", "d"};
	for (size_t i = 0; i < sizeof(parts) / sizeof(parts[0]); i++) {
		snprintf(path, sizeof(path), "%s/%s", root, parts[i]);
		(void)rmdir(path);
	}
	(void)rmdir(root);
}

static void test_mkdir_p(void) {
	char tmpl[] = "/tmp/sf-path-XXXXXX";
	char *root = mkdtemp(tmpl);
	SF_TEST_TRUE(root != NULL);

	char nested[512];
	snprintf(nested, sizeof(nested), "%s/a/b/c", root);
	SF_TEST_EQ_INT(sf_mkdir_p(nested), 0);

	struct stat st;
	SF_TEST_EQ_INT(stat(nested, &st), 0);
	SF_TEST_TRUE(S_ISDIR(st.st_mode));

	/* Idempotent. */
	SF_TEST_EQ_INT(sf_mkdir_p(nested), 0);

	char with_slash[512];
	snprintf(with_slash, sizeof(with_slash), "%s/d/e/", root);
	SF_TEST_EQ_INT(sf_mkdir_p(with_slash), 0);

	SF_TEST_EQ_INT(sf_mkdir_p(NULL), -1);
	SF_TEST_EQ_INT(sf_mkdir_p(""), -1);

	rm_tree(root);
}

static const struct sf_test tests[] = {
	{"sf_mkdir_p", test_mkdir_p},
};

int main(void) {
	return sf_test_run(tests, sizeof(tests) / sizeof(tests[0])) == 0 ? 0 : 1;
}
