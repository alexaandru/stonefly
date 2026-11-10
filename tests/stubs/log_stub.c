#include "log/log.h"

/* Unit tests link util/str without the wlroots-backed log sink. */
void sf_log_emit(enum sf_log_level level, const char *msg, const struct sf_field *fields,
		size_t nfields) {
	(void)level;
	(void)msg;
	(void)fields;
	(void)nfields;
}
