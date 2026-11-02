/* Embedded Fennel-first default tree (C23 #embed). */
#ifndef STONEFLY_CONFIG_EMBED_H
#define STONEFLY_CONFIG_EMBED_H

#include <stddef.h>

struct stonefly_embed_file {
	const char *name; /* relative path under config dir */
	const unsigned char *data;
	size_t len;
};

extern const struct stonefly_embed_file stonefly_default_files[];
extern const size_t stonefly_default_files_len;

#endif
