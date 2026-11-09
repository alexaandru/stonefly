/* Embedded Fennel-first default tree (C23 #embed). */
#ifndef SF_CONFIG_EMBED_H
#define SF_CONFIG_EMBED_H

#include <stddef.h>

struct sf_embed_file {
	const char *name; /* relative path under config dir */
	const unsigned char *data;
	size_t len;
};

extern const struct sf_embed_file sf_default_files[];
extern const size_t sf_default_files_len;

#endif
