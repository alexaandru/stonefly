#include "util/shell_quote.h"

#include <stdlib.h>

char *sf_shell_quote_dup(const char *s) {
	if (s == NULL) {
		s = "";
	}
	size_t cap = 3; /* '' + NUL */
	for (const char *p = s; *p != '\0'; p++) {
		cap += (*p == '\'') ? 4 : 1;
	}
	char *dst = malloc(cap);
	if (dst == NULL) {
		return NULL;
	}
	size_t o = 0;
	dst[o++] = '\'';
	for (const char *p = s; *p != '\0'; p++) {
		if (*p == '\'') {
			dst[o++] = '\'';
			dst[o++] = '\\';
			dst[o++] = '\'';
			dst[o++] = '\'';
		} else {
			dst[o++] = *p;
		}
	}
	dst[o++] = '\'';
	dst[o] = '\0';
	return dst;
}
