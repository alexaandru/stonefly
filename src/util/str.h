#ifndef STONEFLY_UTIL_STR_H
#define STONEFLY_UTIL_STR_H

/* NULL-safe strdup; logs and returns NULL on OOM. */
char *stonefly_xstrdup(const char *s);

/* free *dst, then strdup src (or NULL). No-op on OOM (leaves *dst). */
void stonefly_replace_str(char **dst, const char *src);

#endif
