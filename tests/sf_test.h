#ifndef SF_TEST_H
#define SF_TEST_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Tiny owned unit-test harness (peeble/Go-testing spirit).
 * Register named cases, run them, exit non-zero on any failure.
 */

struct sf_test {
	const char *name;
	void (*fn)(void);
};

/* Failures in the current case; the runner resets this per case. */
extern int sf_test_failures;

void sf_test_failf(const char *file, int line, const char *fmt, ...);

bool sf_test_eq_int(const char *file, int line, int got, int want);
bool sf_test_eq_i64(const char *file, int line, int64_t got, int64_t want);
bool sf_test_eq_double(const char *file, int line, double got, double want, double eps);
bool sf_test_streq(const char *file, int line, const char *got, const char *want);
bool sf_test_true(const char *file, int line, bool cond, const char *expr);

#define SF_TEST_EQ_INT(got, want) sf_test_eq_int(__FILE__, __LINE__, (got), (want))
#define SF_TEST_EQ_I64(got, want) sf_test_eq_i64(__FILE__, __LINE__, (got), (want))
#define SF_TEST_EQ_DOUBLE(got, want, eps)                                                          \
	sf_test_eq_double(__FILE__, __LINE__, (got), (want), (eps))
#define SF_TEST_STREQ(got, want) sf_test_streq(__FILE__, __LINE__, (got), (want))
#define SF_TEST_TRUE(cond) sf_test_true(__FILE__, __LINE__, !!(cond), #cond)
#define SF_TEST_FAILF(...) sf_test_failf(__FILE__, __LINE__, __VA_ARGS__)

/* Run all cases; returns number of failed cases (0 = success). */
int sf_test_run(const struct sf_test *tests, size_t n);

#endif
