#ifndef STONEFLY_UTIL_MATH_H
#define STONEFLY_UTIL_MATH_H

#include <math.h>

static inline double stonefly_clamp01(double t) {
	if (t < 0.0) {
		return 0.0;
	}
	if (t > 1.0) {
		return 1.0;
	}
	return t;
}

/* Peeble EaseOutCubic. */
static inline double stonefly_ease_out_cubic(double t) {
	t = stonefly_clamp01(t);
	double u = 1.0 - t;
	return 1.0 - u * u * u;
}

static inline int stonefly_lerp_i(int a, int b, double t) {
	return (int)lround((double)a + (double)(b - a) * stonefly_clamp01(t));
}

#endif
