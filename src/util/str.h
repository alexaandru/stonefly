#ifndef SF_UTIL_STR_H
#define SF_UTIL_STR_H

#include <stdbool.h>

/* NULL-safe strdup; logs and returns NULL on OOM. */
char *sf_xstrdup(const char *s);

/* free *dst, then strdup src (or NULL). No-op on OOM (leaves *dst). */
void sf_replace_str(char **dst, const char *src);

/*
 * True when haystack contains any comma-separated token from list as a
 * substring. Tokens are trimmed of spaces/tabs. Empty list or haystack → false.
 */
bool sf_str_csv_has_substr(const char *list, const char *haystack);

#endif
