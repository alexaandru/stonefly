#include "sf_test.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

int sf_test_failures;

void sf_test_failf(const char *file, int line, const char *fmt, ...) {
	va_list ap;

	sf_test_failures++;
	fprintf(stderr, "  FAIL %s:%d: ", file, line);
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

int sf_test_run(const struct sf_test *tests, size_t n) {
	int failed_cases = 0;

	for (size_t i = 0; i < n; i++) {
		sf_test_failures = 0;
		fprintf(stderr, "=== RUN   %s\n", tests[i].name);
		tests[i].fn();
		if (sf_test_failures > 0) {
			failed_cases++;
			fprintf(stderr, "--- FAIL: %s (%d)\n", tests[i].name, sf_test_failures);
		} else {
			fprintf(stderr, "--- PASS: %s\n", tests[i].name);
		}
	}

	fprintf(stderr, "%zu cases, %d failed\n", n, failed_cases);
	return failed_cases;
}
