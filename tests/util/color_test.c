#include "sf_test.h"
#include "util/color.h"

static void test_rgba_set_copy(void) {
	struct sf_rgba a, b;

	sf_rgba_set(&a, 0.25f, 0.5f, 0.75f, 1.0f);
	SF_TEST_EQ_DOUBLE(a.r, 0.25, 1e-6);
	SF_TEST_EQ_DOUBLE(a.g, 0.5, 1e-6);
	SF_TEST_EQ_DOUBLE(a.b, 0.75, 1e-6);
	SF_TEST_EQ_DOUBLE(a.a, 1.0, 1e-6);
	SF_TEST_EQ_DOUBLE(a.v[0], 0.25, 1e-6);
	SF_TEST_TRUE(sf_rgba_vec(&a) == a.v);
	SF_TEST_TRUE(sf_rgba_vec_mut(&a) == a.v);

	sf_rgba_copy(&b, &a);
	SF_TEST_EQ_DOUBLE(b.r, 0.25, 1e-6);
	SF_TEST_EQ_DOUBLE(b.g, 0.5, 1e-6);
	SF_TEST_EQ_DOUBLE(b.b, 0.75, 1e-6);
	SF_TEST_EQ_DOUBLE(b.a, 1.0, 1e-6);
}

static void test_rgba_premul(void) {
	struct sf_rgba src, out;

	sf_rgba_set(&src, 1.0f, 0.5f, 0.25f, 0.5f);
	sf_rgba_premul(&src, 1.0f, &out);
	SF_TEST_EQ_DOUBLE(out.r, 0.5, 1e-6);
	SF_TEST_EQ_DOUBLE(out.g, 0.25, 1e-6);
	SF_TEST_EQ_DOUBLE(out.b, 0.125, 1e-6);
	SF_TEST_EQ_DOUBLE(out.a, 0.5, 1e-6);

	sf_rgba_premul(&src, 0.5f, &out);
	SF_TEST_EQ_DOUBLE(out.r, 0.25, 1e-6);
	SF_TEST_EQ_DOUBLE(out.g, 0.125, 1e-6);
	SF_TEST_EQ_DOUBLE(out.b, 0.0625, 1e-6);
	SF_TEST_EQ_DOUBLE(out.a, 0.25, 1e-6);

	sf_rgba_premul(&src, 0.0f, &out);
	SF_TEST_EQ_DOUBLE(out.r, 0.0, 1e-6);
	SF_TEST_EQ_DOUBLE(out.g, 0.0, 1e-6);
	SF_TEST_EQ_DOUBLE(out.b, 0.0, 1e-6);
	SF_TEST_EQ_DOUBLE(out.a, 0.0, 1e-6);
}

static const struct sf_test tests[] = {
	{ "sf_rgba_set_copy", test_rgba_set_copy },
	{ "sf_rgba_premul", test_rgba_premul },
};

int main(void) {
	return sf_test_run(tests, sizeof(tests) / sizeof(tests[0])) == 0 ? 0 : 1;
}
