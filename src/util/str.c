#include "util/str.h"

#include <stdlib.h>
#include <string.h>

#include "log/log.h"

char *sf_xstrdup(const char *s) {
	if (s == NULL) {
		return NULL;
	}
	char *d = strdup(s);
	if (d == NULL) {
		SF_LOGE("strdup OOM");
	}
	return d;
}

void sf_replace_str(char **dst, const char *src) {
	char *copy = NULL;
	if (src != NULL) {
		copy = strdup(src);
		if (copy == NULL) {
			return;
		}
	}
	free(*dst);
	*dst = copy;
}

bool sf_str_csv_has_substr(const char *list, const char *haystack) {
	if (list == NULL || list[0] == '\0' || haystack == NULL || haystack[0] == '\0') {
		return false;
	}

	const char *p = list;
	while (*p != '\0') {
		while (*p == ',' || *p == ' ' || *p == '\t') {
			p++;
		}
		if (*p == '\0') {
			break;
		}
		const char *start = p;
		while (*p != '\0' && *p != ',') {
			p++;
		}
		size_t n = (size_t)(p - start);
		while (n > 0 && (start[n - 1] == ' ' || start[n - 1] == '\t')) {
			n--;
		}
		if (n == 0) {
			continue;
		}
		for (const char *hay = haystack; *hay != '\0'; hay++) {
			if (strncmp(hay, start, n) == 0) {
				return true;
			}
		}
	}
	return false;
}
