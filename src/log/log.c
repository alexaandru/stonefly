#include "log/log.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <wlr/util/log.h>

#include "util/json.h"

static enum sf_log_level g_level = SF_LOG_INFO;
static FILE *g_file;

static const char *level_name(enum sf_log_level level) {
	switch (level) {
	case SF_LOG_DEBUG:
		return "DEBUG";
	case SF_LOG_INFO:
		return "INFO";
	case SF_LOG_WARN:
		return "WARN";
	case SF_LOG_ERROR:
		return "ERROR";
	}
	return "INFO";
}

static enum sf_log_level level_from_wlr(enum wlr_log_importance importance) {
	switch (importance) {
	case WLR_ERROR:
		return SF_LOG_ERROR;
	case WLR_INFO:
		return SF_LOG_INFO;
	case WLR_DEBUG:
	case WLR_SILENT:
	default:
		return SF_LOG_DEBUG;
	}
}

static void format_time(char *buf, size_t cap) {
	struct timespec ts;
	if (clock_gettime(CLOCK_REALTIME, &ts) != 0) {
		snprintf(buf, cap, "1970-01-01T00:00:00.000000000Z");
		return;
	}

	struct tm tm;
	if (gmtime_r(&ts.tv_sec, &tm) == NULL) {
		snprintf(buf, cap, "1970-01-01T00:00:00.000000000Z");
		return;
	}

	size_t n = strftime(buf, cap, "%Y-%m-%dT%H:%M:%S", &tm);
	if (n == 0 || n + 12 >= cap) {
		snprintf(buf, cap, "1970-01-01T00:00:00.000000000Z");
		return;
	}
	snprintf(buf + n, cap - n, ".%09ldZ", (long)ts.tv_nsec);
}

static void emit_line(const char *line, size_t len) {
	fwrite(line, 1, len, stderr);
	fputc('\n', stderr);
	fflush(stderr);
	if (g_file != NULL) {
		fwrite(line, 1, len, g_file);
		fputc('\n', g_file);
		fflush(g_file);
	}
}

void sf_log_emit(enum sf_log_level level, const char *msg, const struct sf_field *fields,
		 size_t nfields) {
	if (level < g_level) {
		return;
	}
	if (msg == NULL) {
		msg = "";
	}

	char line[4096];
	struct json_buf b;
	json_buf_init(&b, line, sizeof(line));

	char tbuf[64];
	format_time(tbuf, sizeof(tbuf));

	json_byte(&b, '{');
	json_key(&b, "time");
	json_str(&b, tbuf);
	json_byte(&b, ',');
	json_key(&b, "level");
	json_str(&b, level_name(level));
	json_byte(&b, ',');
	json_key(&b, "msg");
	json_str(&b, msg);

	for (size_t i = 0; i < nfields; i++) {
		const struct sf_field *f = &fields[i];
		if (f->key == NULL || f->key[0] == '\0') {
			continue;
		}
		json_byte(&b, ',');
		json_key(&b, f->key);
		switch (f->type) {
		case SF_FIELD_STR:
			json_str(&b, f->str);
			break;
		case SF_FIELD_I64:
			json_i64(&b, f->i64);
			break;
		case SF_FIELD_BOOL:
			json_bool(&b, f->boolean);
			break;
		}
	}

	json_byte(&b, '}');
	json_finish(&b);
	if (!json_ok(&b)) {
		return;
	}
	emit_line(b.data, b.len);
}

void sf_logf(enum sf_log_level level, const char *fmt, ...) {
	if (level < g_level) {
		return;
	}
	char msg[2048];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(msg, sizeof(msg), fmt, ap);
	va_end(ap);
	sf_log_emit(level, msg, NULL, 0);
}

static void wlr_log_handler(enum wlr_log_importance importance, const char *fmt, va_list args) {
	if (importance == WLR_SILENT) {
		return;
	}
	enum sf_log_level level = level_from_wlr(importance);
	if (level < g_level) {
		return;
	}
	char msg[2048];
	vsnprintf(msg, sizeof(msg), fmt, args);
	sf_log_emit(level, msg, NULL, 0);
}

static char *log_dir_path(void) {
	const char *home = getenv("HOME");
	if (home == NULL || home[0] == '\0') {
		return NULL;
	}
	size_t n = strlen(home) + sizeof("/.var/log/stonefly");
	char *dir = malloc(n);
	if (dir == NULL) {
		return NULL;
	}
	snprintf(dir, n, "%s/.var/log/stonefly", home);
	return dir;
}

/*
 * Keep only current + previous session on disk:
 *   wm.log → wm.last.json (replacing any older last), then open wm.log truncated.
 */
static void rotate_session_log(const char *dir) {
	char cur[512];
	char last[512];
	int ncur = snprintf(cur, sizeof(cur), "%s/wm.log", dir);
	int nlast = snprintf(last, sizeof(last), "%s/wm.last.json", dir);
	if (ncur < 0 || (size_t)ncur >= sizeof(cur) || nlast < 0 || (size_t)nlast >= sizeof(last)) {
		return;
	}

	struct stat st;
	if (stat(cur, &st) != 0) {
		return; /* no previous session file */
	}
	unlink(last); /* ignore ENOENT */
	if (rename(cur, last) != 0) {
		/* Fall through: fopen("w") still truncates a stuck wm.log. */
		return;
	}
}

void sf_log_init(enum sf_log_level min_level) {
	g_level = min_level;
	g_file = NULL;

	char *dir = log_dir_path();
	if (dir == NULL) {
		SF_LOGW("log dir unavailable, logging to stderr only");
	} else if (mkdir(dir, 0755) != 0 && errno != EEXIST) {
		SF_LOGW("cannot create log dir, logging to stderr only", sf_str("dir", dir),
			sf_str("err", strerror(errno)));
	} else {
		rotate_session_log(dir);

		size_t n = strlen(dir) + sizeof("/wm.log");
		char *path = malloc(n);
		if (path == NULL) {
			SF_LOGW("cannot open log file, logging to stderr only", sf_str("dir", dir));
		} else {
			snprintf(path, n, "%s/wm.log", dir);
			/* Truncate: fresh session after rotate moved the old file. */
			g_file = fopen(path, "w");
			if (g_file == NULL) {
				SF_LOGW("cannot open log file, logging to stderr only",
					sf_str("path", path), sf_str("err", strerror(errno)));
			}
			free(path);
		}
	}
	free(dir);

	wlr_log_init(WLR_DEBUG, wlr_log_handler);
}

void sf_log_finish(void) {
	if (g_file != NULL) {
		fclose(g_file);
		g_file = NULL;
	}
}

void sf_log_set_level(enum sf_log_level min_level) {
	g_level = min_level;
}
