#include "util/str.h"

#include <stdlib.h>
#include <string.h>

#include "log/log.h"

char *stonefly_xstrdup(const char *s) {
	if (s == NULL) {
		return NULL;
	}
	char *d = strdup(s);
	if (d == NULL) {
		SF_LOGE("strdup OOM");
	}
	return d;
}

void stonefly_replace_str(char **dst, const char *src) {
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
