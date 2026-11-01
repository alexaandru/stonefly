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

/*
 * Umbriel nestedRadius — smooth inset of an outer corner radius.
 * N(R,d) = 0 when R==0; else max(1, round(R²/(R+d))).
 * Umbriel derives content/seam from an outer outline (border.frag). SceneFX
 * solid-rect borders stay concentric instead: hole_r = content - overlap.
 */
static inline int sf_nested_radius(int radius, int inset) {
	if (radius <= 0) {
		return 0;
	}
	if (inset <= 0) {
		return radius;
	}
	int denom = radius + inset;
	int rounded = (radius * radius + denom / 2) / denom;
	return rounded > 0 ? rounded : 1;
}

/*
 * Stonefly corner_radius is the content arc (CSD match). Outer rim of a
 * SceneFX border rect is content + border so outer/hole centers coincide
 * when the hole sits at (border,border) with radius content.
 */
static inline int sf_outer_corner_radius(int content_radius, int border) {
	if (content_radius <= 0) {
		return 0;
	}
	return content_radius + (border > 0 ? border : 0);
}

#endif
