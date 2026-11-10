#include "sf_test.h"

#include <math.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int sf_test_failures;

static const char *sf_test_case;

static bool sf_test_verbose(void) {
	const char *v = getenv("SF_TEST_VERBOSE");

	return v != NULL && v[0] != '\0' && strcmp(v, "0") != 0;
}

void sf_test_failf(const char *file, int line, const char *fmt, ...) {
	va_list ap;
	const char *case_name = sf_test_case != NULL ? sf_test_case : "?";

	sf_test_failures++;
	/* Align with PATH column of print_row: STATUS(4) + "  " (2) → col 6. */
	fprintf(stderr, "      %s:%d: %s: ", file, line, case_name);
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fputc('\n', stderr);
}

bool sf_test_eq_int(const char *file, int line, int got, int want) {
	if (got == want) {
		return true;
	}
	sf_test_failf(file, line, "got %d, want %d", got, want);
	return false;
}

bool sf_test_eq_i64(const char *file, int line, int64_t got, int64_t want) {
	if (got == want) {
		return true;
	}
	sf_test_failf(file, line, "got %lld, want %lld", (long long)got, (long long)want);
	return false;
}

bool sf_test_eq_double(const char *file, int line, double got, double want, double eps) {
	if (fabs(got - want) <= eps) {
		return true;
	}
	sf_test_failf(file, line, "got %g, want %g (eps %g)", got, want, eps);
	return false;
}

bool sf_test_streq(const char *file, int line, const char *got, const char *want) {
	if (got == NULL && want == NULL) {
		return true;
	}
	if (got != NULL && want != NULL && strcmp(got, want) == 0) {
		return true;
	}
	sf_test_failf(file, line, "got %s, want %s", got != NULL ? got : "(null)",
		      want != NULL ? want : "(null)");
	return false;
}

bool sf_test_true(const char *file, int line, bool cond, const char *expr) {
	if (cond) {
		return true;
	}
	sf_test_failf(file, line, "want true: %s", expr);
	return false;
}

int sf_test_run(const struct sf_test *tests, size_t n) {
	int failed_cases = 0;
	bool verbose = sf_test_verbose();

	for (size_t i = 0; i < n; i++) {
		sf_test_failures = 0;
		sf_test_case = tests[i].name;
		if (verbose) {
			fprintf(stderr, "=== RUN   %s\n", tests[i].name);
		}
		tests[i].fn();
		if (sf_test_failures > 0) {
			failed_cases++;
		} else if (verbose) {
			fprintf(stderr, "--- PASS: %s\n", tests[i].name);
		}
	}
	sf_test_case = NULL;

	/* Machine line for tests/drive.py (stdout). Human chatter stays on stderr. */
	fprintf(stdout, "sf_test_result cases=%zu pass=%d fail=%d\n", n, (int)n - failed_cases,
		failed_cases);
	fflush(stdout);
	return failed_cases;
}
