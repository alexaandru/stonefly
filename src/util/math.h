#ifndef SF_UTIL_MATH_H
#define SF_UTIL_MATH_H

#include <math.h>

static inline double sf_clamp01(double t) {
	if (t < 0.0) {
		return 0.0;
	}
	if (t > 1.0) {
		return 1.0;
	}
	return t;
}

/* Peeble EaseOutCubic. */
static inline double sf_ease_out_cubic(double t) {
	t = sf_clamp01(t);
	double u = 1.0 - t;
	return 1.0 - u * u * u;
}

static inline int sf_lerp_i(int a, int b, double t) {
	return (int)lround((double)a + (double)(b - a) * sf_clamp01(t));
}

#endif
