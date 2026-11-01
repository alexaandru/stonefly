#ifndef STONEFLY_UTIL_JSON_H
#define STONEFLY_UTIL_JSON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Fixed-buffer JSON writer with a sticky error flag.
 * After any overflow or encode failure, further appends are no-ops
 * and json_ok() is false.
 */

struct json_buf {
	char *data;
	size_t len;
	size_t cap;
	bool err;
};

void json_buf_init(struct json_buf *b, char *storage, size_t cap);

void json_raw(struct json_buf *b, const char *s);
void json_byte(struct json_buf *b, char c);
void json_str(struct json_buf *b, const char *s);
void json_i64(struct json_buf *b, int64_t v);
void json_bool(struct json_buf *b, bool v);

/* Emit "key": */
void json_key(struct json_buf *b, const char *key);

static inline bool json_ok(const struct json_buf *b) {
	return b != NULL && !b->err && b->len < b->cap;
}

/* NUL-terminate if there is room; sets err on failure. */
void json_finish(struct json_buf *b);

/*
 * Minimal scanner for whitespace, punctuation, strings, bools, and
 * integers. Enough for stonefly IPC replies and {"cmd":"..."} requests.
 */

struct json_scan {
	const char *p;
	const char *end;
	bool err;
};

void json_scan_init(struct json_scan *s, const char *text);
void json_scan_skip_ws(struct json_scan *s);
bool json_scan_expect(struct json_scan *s, char c);

/* Copy unescaped string into out (NUL-terminated). */
bool json_scan_string(struct json_scan *s, char *out, size_t out_cap);
bool json_scan_bool(struct json_scan *s, bool *out);
bool json_scan_i64(struct json_scan *s, int64_t *out);

static inline bool json_scan_ok(const struct json_scan *s) {
	return s != NULL && !s->err;
}

#endif
