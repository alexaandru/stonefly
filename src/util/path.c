#include "util/path.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

int sf_mkdir_p(const char *path) {
	char tmp[512];
	if (path == NULL || snprintf(tmp, sizeof tmp, "%s", path) >= (int)sizeof tmp) {
		return -1;
	}
	size_t len = strlen(tmp);
	if (len == 0) {
		return -1;
	}
	if (tmp[len - 1] == '/') {
		tmp[len - 1] = '\0';
	}
	for (char *p = tmp + 1; *p; p++) {
		if (*p != '/') {
			continue;
		}
		*p = '\0';
		if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
			return -1;
		}
		*p = '/';
	}
	if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
		return -1;
	}
	return 0;
}
