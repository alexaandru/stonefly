#ifndef STONEFLY_LOG_H
#define STONEFLY_LOG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Structured JSON logging (peeble slog shape).
 *
 * Each record is one JSON object on stderr and ~/.var/log/stonefly/wm.log:
 *   {"time":"...","level":"INFO","msg":"...","display":"wayland-1"}
 *
 * wlroots messages share the same sink via wlr_log_init.
 *
 * Usage:
 *   SF_LOGI("Running Wayland compositor", sf_str("display", socket));
 *   SF_LOGE("failed to create wlr_backend");
 *   SF_LOGE("ipc bind", sf_str("path", path), sf_str("err", strerror(errno)));
 */

enum sf_log_level {
	SF_LOG_DEBUG = 0,
	SF_LOG_INFO,
	SF_LOG_WARN,
	SF_LOG_ERROR,
};

enum sf_field_type {
	SF_FIELD_STR = 0,
	SF_FIELD_I64,
	SF_FIELD_BOOL,
};

struct sf_field {
	const char *key;
	enum sf_field_type type;
	union {
		const char *str;
		int64_t i64;
		bool boolean;
	};
};

static inline struct sf_field sf_str(const char *key, const char *val) {
	return (struct sf_field){
		.key = key,
		.type = SF_FIELD_STR,
		.str = val != NULL ? val : "",
	};
}

static inline struct sf_field sf_i64(const char *key, int64_t val) {
	return (struct sf_field){
		.key = key,
		.type = SF_FIELD_I64,
		.i64 = val,
	};
}

static inline struct sf_field sf_bool(const char *key, bool val) {
	return (struct sf_field){
		.key = key,
		.type = SF_FIELD_BOOL,
		.boolean = val,
	};
}

void sf_log_init(enum sf_log_level min_level);
void sf_log_finish(void);
void sf_log_set_level(enum sf_log_level min_level);

void sf_log_emit(enum sf_log_level level, const char *msg,
		const struct sf_field *fields, size_t nfields);

/* Printf-style message body (no structured fields). */
void sf_logf(enum sf_log_level level, const char *fmt, ...)
	__attribute__((format(printf, 2, 3)));

/*
 * Dummy leading element + __VA_OPT__ so message-only calls work under C23
 * (abuild: -std=c23) without a trailing-comma / zero-length-array footgun.
 */
#define SF_LOG_AT(level, msg, ...) \
	sf_log_emit((level), (msg), \
		&(const struct sf_field[]){ \
			(struct sf_field){0} __VA_OPT__(,) __VA_ARGS__ \
		}[1], \
		(sizeof((const struct sf_field[]){ \
			(struct sf_field){0} __VA_OPT__(,) __VA_ARGS__ \
		}) / sizeof(struct sf_field)) - 1)

#define SF_LOGD(...) SF_LOG_AT(SF_LOG_DEBUG, __VA_ARGS__)
#define SF_LOGI(...) SF_LOG_AT(SF_LOG_INFO, __VA_ARGS__)
#define SF_LOGW(...) SF_LOG_AT(SF_LOG_WARN, __VA_ARGS__)
#define SF_LOGE(...) SF_LOG_AT(SF_LOG_ERROR, __VA_ARGS__)

#endif
