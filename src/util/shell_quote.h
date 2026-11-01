#ifndef SF_UTIL_SHELL_QUOTE_H
#define SF_UTIL_SHELL_QUOTE_H

/* Single-quote for sh -c argv join. Caller frees. NULL on OOM. */
char *sf_shell_quote_dup(const char *s);

#endif
