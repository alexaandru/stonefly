#define _POSIX_C_SOURCE 200809L

#include "util/json.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void json_buf_init(struct json_buf *b, char *storage, size_t cap) {
	b->data = storage;
	b->len = 0;
	b->cap = cap;
	b->err = storage == NULL || cap == 0;
	if (!b->err) {
		b->data[0] = '\0';
	}
}

static void json_reserve(struct json_buf *b, size_t n) {
	if (b->err) {
		return;
	}
	if (b->len + n >= b->cap) {
		b->err = true;
	}
}

void json_byte(struct json_buf *b, char c) {
	json_reserve(b, 1);
	if (b->err) {
		return;
	}
	b->data[b->len++] = c;
}

void json_raw(struct json_buf *b, const char *s) {
	if (s == NULL) {
		s = "";
	}
	size_t n = strlen(s);
	json_reserve(b, n);
	if (b->err) {
		return;
	}
	memcpy(b->data + b->len, s, n);
	b->len += n;
}

void json_str(struct json_buf *b, const char *s) {
	if (s == NULL) {
		s = "";
	}
	json_byte(b, '"');
	for (const unsigned char *p = (const unsigned char *)s; *p != '\0'; p++) {
		switch (*p) {
		case '"':
			json_raw(b, "\\\"");
			break;
		case '\\':
			json_raw(b, "\\\\");
			break;
		case '\b':
			json_raw(b, "\\b");
			break;
		case '\f':
			json_raw(b, "\\f");
			break;
		case '\n':
			json_raw(b, "\\n");
			break;
		case '\r':
			json_raw(b, "\\r");
			break;
		case '\t':
			json_raw(b, "\\t");
			break;
		default:
			if (*p < 0x20) {
				char esc[8];
				snprintf(esc, sizeof(esc), "\\u%04x", *p);
				json_raw(b, esc);
			} else {
				json_byte(b, (char)*p);
			}
			break;
		}
		if (b->err) {
			return;
		}
	}
	json_byte(b, '"');
}

void json_i64(struct json_buf *b, int64_t v) {
	char tmp[32];
	snprintf(tmp, sizeof(tmp), "%lld", (long long)v);
	json_raw(b, tmp);
}

void json_f64(struct json_buf *b, double v) {
	char tmp[64];
	/* Enough digits for RGBA / scale knobs; avoid locale commas. */
	snprintf(tmp, sizeof(tmp), "%.6g", v);
	json_raw(b, tmp);
}

void json_bool(struct json_buf *b, bool v) {
	json_raw(b, v ? "true" : "false");
}

void json_key(struct json_buf *b, const char *key) {
	json_str(b, key);
	json_byte(b, ':');
}

void json_finish(struct json_buf *b) {
	json_reserve(b, 1);
	if (b->err) {
		return;
	}
	b->data[b->len] = '\0';
}

void json_scan_init(struct json_scan *s, const char *text) {
	s->p = text != NULL ? text : "";
	s->end = s->p + strlen(s->p);
	s->err = false;
}

void json_scan_skip_ws(struct json_scan *s) {
	while (s->p < s->end) {
		char c = *s->p;
		if (c != ' ' && c != '\t' && c != '\n' && c != '\r') {
			break;
		}
		s->p++;
	}
}

bool json_scan_expect(struct json_scan *s, char c) {
	json_scan_skip_ws(s);
	if (s->err || s->p >= s->end || *s->p != c) {
		s->err = true;
		return false;
	}
	s->p++;
	return true;
}

bool json_scan_string(struct json_scan *s, char *out, size_t out_cap) {
	json_scan_skip_ws(s);
	if (s->err || out == NULL || out_cap == 0 ||
			s->p >= s->end || *s->p != '"') {
		s->err = true;
		return false;
	}
	s->p++;

	size_t n = 0;
	while (s->p < s->end && *s->p != '"') {
		unsigned char c = (unsigned char)*s->p++;
		if (c == '\\') {
			if (s->p >= s->end) {
				s->err = true;
				return false;
			}
			char e = *s->p++;
			switch (e) {
			case '"':
			case '\\':
			case '/':
				c = (unsigned char)e;
				break;
			case 'b':
				c = '\b';
				break;
			case 'f':
				c = '\f';
				break;
			case 'n':
				c = '\n';
				break;
			case 'r':
				c = '\r';
				break;
			case 't':
				c = '\t';
				break;
			case 'u':
				/* Accept \uXXXX but only emit '?' — titles rarely need it. */
				if (s->p + 4 > s->end) {
					s->err = true;
					return false;
				}
				s->p += 4;
				c = '?';
				break;
			default:
				s->err = true;
				return false;
			}
		}
		if (n + 1 >= out_cap) {
			s->err = true;
			return false;
		}
		out[n++] = (char)c;
	}
	if (s->p >= s->end || *s->p != '"') {
		s->err = true;
		return false;
	}
	s->p++;
	out[n] = '\0';
	return true;
}

bool json_scan_bool(struct json_scan *s, bool *out) {
	json_scan_skip_ws(s);
	if (s->err || s->p >= s->end) {
		s->err = true;
		return false;
	}
	if (s->end - s->p >= 4 && strncmp(s->p, "true", 4) == 0) {
		s->p += 4;
		*out = true;
		return true;
	}
	if (s->end - s->p >= 5 && strncmp(s->p, "false", 5) == 0) {
		s->p += 5;
		*out = false;
		return true;
	}
	s->err = true;
	return false;
}

bool json_scan_i64(struct json_scan *s, int64_t *out) {
	json_scan_skip_ws(s);
	if (s->err || s->p >= s->end) {
		s->err = true;
		return false;
	}
	char *end = NULL;
	long long v = strtoll(s->p, &end, 10);
	if (end == s->p) {
		s->err = true;
		return false;
	}
	s->p = end;
	*out = (int64_t)v;
	return true;
}
