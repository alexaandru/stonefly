#ifndef SF_UTIL_PATH_H
#define SF_UTIL_PATH_H

/* Recursive mkdir (0755). Returns 0 on success / already-exists; -1 on error. */
int sf_mkdir_p(const char *path);

#endif
