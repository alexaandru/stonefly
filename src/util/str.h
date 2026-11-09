#ifndef SF_UTIL_STR_H
#define SF_UTIL_STR_H

/* NULL-safe strdup; logs and returns NULL on OOM. */
char *sf_xstrdup(const char *s);

/* free *dst, then strdup src (or NULL). No-op on OOM (leaves *dst). */
void sf_replace_str(char **dst, const char *src);

#endif
