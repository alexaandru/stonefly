#include "config/config_watch.h"

#include <errno.h>
#include <limits.h>
#include <stdalign.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <unistd.h>
#include <wayland-server-core.h>

#include "config/config.h"
#include "log/log.h"
#include "server/server.h"

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

/* Editors often emit several events per save; coalesce like Umbriel (150ms). */
#define SF_CONFIG_WATCH_DEBOUNCE_MS 150

/* Cast each flag: musl inotify macros are signed ints (bugprone-signed-bitwise). */
static const uint32_t sf_inotify_mask =
	(uint32_t)IN_CREATE | (uint32_t)IN_CLOSE_WRITE | (uint32_t)IN_ATTRIB |
	(uint32_t)IN_MOVED_FROM | (uint32_t)IN_MOVED_TO | (uint32_t)IN_DELETE |
	(uint32_t)IN_MODIFY | (uint32_t)IN_DELETE_SELF | (uint32_t)IN_MOVE_SELF;

struct sf_config_watch {
	struct sf_server *server;
	int fd;
	int wd;
	struct wl_event_source *fd_source;
	struct wl_event_source *timer;
	/* Desired user config directory (sf_config_dir). */
	char target[PATH_MAX];
	/* Directory currently watched: target, or nearest existing ancestor. */
	char watched[PATH_MAX];
};

static bool path_is_dir(const char *path) {
	struct stat st;
	return path != NULL && path[0] != '\0' && stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

/*
 * Nearest existing directory on the path to target (inclusive). Walks parents
 * when target is missing so --write-config creating the tree is observable.
 */
static bool nearest_existing_dir(const char *target, char *out, size_t out_n) {
	if (target == NULL || target[0] == '\0' || out == NULL || out_n == 0) {
		return false;
	}
	char cur[PATH_MAX];
	if (snprintf(cur, sizeof cur, "%s", target) >= (int)sizeof cur) {
		return false;
	}
	for (;;) {
		if (path_is_dir(cur)) {
			if (snprintf(out, out_n, "%s", cur) >= (int)out_n) {
				return false;
			}
			return true;
		}
		char *slash = strrchr(cur, '/');
		if (slash == NULL) {
			return false;
		}
		if (slash == cur) {
			/* Root "/". */
			if (path_is_dir("/")) {
				snprintf(out, out_n, "/");
				return true;
			}
			return false;
		}
		*slash = '\0';
	}
}

/* Basename of the path component under `watched` that leads toward `target`. */
static bool next_child_name(const char *target, const char *watched, char *name, size_t name_n) {
	if (target == NULL || watched == NULL || name == NULL || name_n == 0) {
		return false;
	}
	size_t wlen = strlen(watched);
	if (wlen == 0) {
		return false;
	}
	if (strcmp(watched, "/") == 0) {
		if (target[0] != '/') {
			return false;
		}
		const char *rest = target + 1;
		const char *slash = strchr(rest, '/');
		size_t n = slash != NULL ? (size_t)(slash - rest) : strlen(rest);
		if (n == 0 || n >= name_n) {
			return false;
		}
		memcpy(name, rest, n);
		name[n] = '\0';
		return true;
	}
	if (strncmp(target, watched, wlen) != 0) {
		return false;
	}
	if (target[wlen] != '/' && target[wlen] != '\0') {
		return false;
	}
	if (target[wlen] == '\0') {
		return false; /* already on target */
	}
	const char *rest = target + wlen + 1;
	const char *slash = strchr(rest, '/');
	size_t n = slash != NULL ? (size_t)(slash - rest) : strlen(rest);
	if (n == 0 || n >= name_n) {
		return false;
	}
	memcpy(name, rest, n);
	name[n] = '\0';
	return true;
}

static bool name_interesting_file(const char *name) {
	if (name == NULL || name[0] == '\0') {
		return true; /* directory-level event */
	}
	if (name[0] == '.') {
		return false;
	}
	size_t n = strlen(name);
	if (n > 0 && name[n - 1] == '~') {
		return false;
	}
	return true;
}

static bool watch_add(struct sf_config_watch *w, const char *dir) {
	if (w == NULL || w->fd < 0 || dir == NULL || dir[0] == '\0') {
		return false;
	}
	if (w->wd >= 0) {
		inotify_rm_watch(w->fd, w->wd);
		w->wd = -1;
	}
	int wd = inotify_add_watch(w->fd, dir, sf_inotify_mask);
	if (wd < 0) {
		SF_LOGW("config watch: cannot watch", sf_str("dir", dir),
			sf_str("err", strerror(errno)));
		w->watched[0] = '\0';
		return false;
	}
	w->wd = wd;
	snprintf(w->watched, sizeof w->watched, "%s", dir);
	return true;
}

/* Point the watch at target or its nearest existing ancestor. */
static bool watch_point(struct sf_config_watch *w) {
	char target[PATH_MAX];
	if (sf_config_dir(target, sizeof target) != 0) {
		SF_LOGW("config watch: config dir unresolved");
		return false;
	}
	snprintf(w->target, sizeof w->target, "%s", target);

	char dir[PATH_MAX];
	if (!nearest_existing_dir(target, dir, sizeof dir)) {
		SF_LOGW("config watch: no existing ancestor", sf_str("target", target));
		return false;
	}
	if (!watch_add(w, dir)) {
		return false;
	}
	bool on_target = strcmp(dir, target) == 0;
	SF_LOGI("config watch", sf_str("dir", dir), sf_str("target", target),
		sf_i64("on_target", on_target ? 1 : 0));
	return true;
}

static bool init_readable(const char *config_dir) {
	char path[PATH_MAX];
	if (snprintf(path, sizeof path, "%s/init.lua", config_dir) >= (int)sizeof path) {
		return false;
	}
	return access(path, R_OK) == 0;
}

static int on_timer(void *data) {
	struct sf_config_watch *w = data;
	if (w == NULL || w->server == NULL) {
		return 0;
	}

	/* Path components may have appeared (--write-config); walk deeper. */
	char before[PATH_MAX];
	snprintf(before, sizeof before, "%s", w->watched);
	(void)watch_point(w);

	/*
	 * Reload when the user tree is present. Covers: edit of an existing
	 * tree, and the first appearance of init.lua after --write-config.
	 */
	if (init_readable(w->target)) {
		if (!sf_config_reload(w->server)) {
			SF_LOGW("config watch: reload failed; keeping previous");
		}
		/* Reload may have toggled config_watch; sync is inside reload. */
	} else if (strcmp(before, w->watched) != 0) {
		SF_LOGI("config watch: armed deeper; waiting for init.lua",
			sf_str("dir", w->watched));
	}
	return 0;
}

static int on_inotify(int fd, uint32_t mask, void *data) {
	struct sf_config_watch *w = data;
	(void)mask;
	if (w == NULL || w->timer == NULL) {
		return 0;
	}

	bool on_target = strcmp(w->watched, w->target) == 0;
	char expect_child[256];
	bool have_child = !on_target &&
			  next_child_name(w->target, w->watched, expect_child, sizeof expect_child);

	alignas(struct inotify_event) char buf[4096];
	bool changed = false;

	while (true) {
		ssize_t n = read(fd, buf, sizeof buf);
		if (n < 0) {
			if (errno == EAGAIN || errno == EWOULDBLOCK) {
				break;
			}
			if (errno == EINTR) {
				continue;
			}
			break;
		}
		if (n == 0) {
			break;
		}

		size_t off = 0;
		while (off < (size_t)n) {
			const struct inotify_event *ev = (const struct inotify_event *)(buf + off);
			uint32_t emask = ev->mask;
			bool dir_gone =
				ev->len == 0 &&
				(emask & ((uint32_t)IN_DELETE_SELF | (uint32_t)IN_MOVE_SELF)) != 0;
			if ((emask & (uint32_t)IN_Q_OVERFLOW) != 0 || dir_gone) {
				changed = true;
			} else if (ev->len > 0) {
				if (on_target) {
					if (name_interesting_file(ev->name)) {
						changed = true;
					}
				} else if (have_child && strcmp(ev->name, expect_child) == 0) {
					/* Config dir (or next path hop) appeared. */
					changed = true;
				}
			}
			off += sizeof(struct inotify_event) + ev->len;
		}
	}

	if (changed) {
		wl_event_source_timer_update(w->timer, SF_CONFIG_WATCH_DEBOUNCE_MS);
	}
	return 0;
}

static void watch_clear(struct sf_config_watch *w) {
	if (w == NULL) {
		return;
	}
	if (w->fd_source != NULL) {
		wl_event_source_remove(w->fd_source);
		w->fd_source = NULL;
	}
	if (w->timer != NULL) {
		wl_event_source_remove(w->timer);
		w->timer = NULL;
	}
	if (w->fd >= 0) {
		if (w->wd >= 0) {
			inotify_rm_watch(w->fd, w->wd);
			w->wd = -1;
		}
		close(w->fd);
		w->fd = -1;
	}
	w->watched[0] = '\0';
	w->target[0] = '\0';
}

static bool watch_arm(struct sf_config_watch *w) {
	int fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
	if (fd < 0) {
		SF_LOGW("config watch: inotify unavailable; use reload action / IPC");
		return false;
	}

	struct wl_event_loop *loop = wl_display_get_event_loop(w->server->wl_display);
	struct wl_event_source *fd_source =
		wl_event_loop_add_fd(loop, fd, WL_EVENT_READABLE, on_inotify, w);
	struct wl_event_source *timer = wl_event_loop_add_timer(loop, on_timer, w);
	if (fd_source == NULL || timer == NULL) {
		SF_LOGW("config watch: event sources failed");
		if (fd_source != NULL) {
			wl_event_source_remove(fd_source);
		}
		if (timer != NULL) {
			wl_event_source_remove(timer);
		}
		close(fd);
		return false;
	}

	w->fd = fd;
	w->wd = -1;
	w->fd_source = fd_source;
	w->timer = timer;
	if (!watch_point(w)) {
		watch_clear(w);
		return false;
	}
	return true;
}

void sf_config_watch_sync(struct sf_server *server) {
	if (server == NULL) {
		return;
	}

	const struct sf_config *cfg = sf_cfg(server);
	bool want = cfg->config_watch;

	if (!want) {
		if (server->config_watch != NULL) {
			watch_clear(server->config_watch);
			free(server->config_watch);
			server->config_watch = NULL;
		}
		return;
	}

	if (server->config_watch == NULL) {
		struct sf_config_watch *w = calloc(1, sizeof *w);
		if (w == NULL) {
			return;
		}
		w->server = server;
		w->fd = -1;
		w->wd = -1;
		server->config_watch = w;
		if (!watch_arm(w)) {
			free(w);
			server->config_watch = NULL;
		}
		return;
	}

	struct sf_config_watch *w = server->config_watch;
	if (w->fd < 0) {
		if (!watch_arm(w)) {
			free(w);
			server->config_watch = NULL;
		}
		return;
	}
	(void)watch_point(w);
}

void sf_config_watch_finish(struct sf_server *server) {
	if (server == NULL || server->config_watch == NULL) {
		return;
	}
	watch_clear(server->config_watch);
	free(server->config_watch);
	server->config_watch = NULL;
}
