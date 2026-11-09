#ifndef STONEFLY_UTIL_TIME_H
#define STONEFLY_UTIL_TIME_H

#include <stdint.h>
#include <time.h>

static inline int64_t stonefly_timespec_ns(const struct timespec *ts) {
	return (int64_t)ts->tv_sec * 1000000000LL + (int64_t)ts->tv_nsec;
}

static inline int64_t stonefly_timespec_ms(const struct timespec *ts) {
	return (int64_t)ts->tv_sec * 1000LL + ts->tv_nsec / 1000000LL;
}

static inline int64_t stonefly_timespec_ms_since(const struct timespec *then) {
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	return (int64_t)(now.tv_sec - then->tv_sec) * 1000 +
	       (now.tv_nsec - then->tv_nsec) / 1000000;
}

#endif
