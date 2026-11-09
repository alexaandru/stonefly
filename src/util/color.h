/* Premultiplied RGBA helpers (SceneFX / wlroots float[4] contract). */
#ifndef SF_UTIL_COLOR_H
#define SF_UTIL_COLOR_H

/* Named channels + contiguous float[4] view (clang-analyzer ArrayBound needs
 * a real array object for indexed access — &r alone looks like one float). */
struct sf_rgba {
	union {
		float v[4];
		struct {
			float r, g, b, a;
		};
	};
};

/* Contiguous float[4] view for wlr_scene_*_set_color. */
static inline const float *sf_rgba_vec(const struct sf_rgba *c) {
	return c->v;
}

static inline float *sf_rgba_vec_mut(struct sf_rgba *c) {
	return c->v;
}

static inline void sf_rgba_set(struct sf_rgba *dst, float r, float g, float b, float a) {
	dst->r = r;
	dst->g = g;
	dst->b = b;
	dst->a = a;
}

static inline void sf_rgba_copy(struct sf_rgba *dst, const struct sf_rgba *src) {
	*dst = *src;
}

/* SceneFX rect/shadow colors are premultiplied. */
static inline void sf_rgba_premul(const struct sf_rgba *src, float opacity, struct sf_rgba *out) {
	float a = src->a * opacity;
	out->r = src->r * a;
	out->g = src->g * a;
	out->b = src->b * a;
	out->a = a;
}

#endif
