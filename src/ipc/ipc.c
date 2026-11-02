#define _POSIX_C_SOURCE 200809L

#include "ipc/ipc.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <wayland-server-core.h>

#include "ipc/layers.h"
#include "ipc/windows.h"
#include "log/log.h"
#include "server/server.h"
#include "util/json.h"

#define STONEFLY_IPC_ENV "STONEFLY_SOCKET"
#define STONEFLY_IPC_REPLY_CAP (64 * 1024)
#define STONEFLY_IPC_REQ_CAP 4096
#define STONEFLY_IPC_MAX_WINDOWS 256
#define STONEFLY_IPC_MAX_LAYERS 256

struct stonefly_ipc {
	struct stonefly_server *server;
	int fd;
	char *path;
	struct wl_event_source *source;
};

struct stonefly_ipc_client {
	struct stonefly_ipc *ipc;
	int fd;
	struct wl_event_source *source;
	char buf[STONEFLY_IPC_REQ_CAP];
	size_t len;
};

static void fd_set_cloexec(int fd) {
	int flags = fcntl(fd, F_GETFD);
	if (flags >= 0) {
		(void)fcntl(fd, F_SETFD, flags | FD_CLOEXEC);
	}
}

static char *socket_path(void) {
	const char *override = getenv(STONEFLY_IPC_ENV);
	if (override != NULL && override[0] != '\0') {
		return strdup(override);
	}

	const char *runtime = getenv("XDG_RUNTIME_DIR");
	const char *display = getenv("WAYLAND_DISPLAY");
	if (runtime == NULL || runtime[0] == '\0' ||
			display == NULL || display[0] == '\0') {
		return NULL;
	}

	size_t n = strlen(runtime) + strlen(display) + sizeof("/stonefly-.sock");
	char *path = malloc(n);
	if (path == NULL) {
		return NULL;
	}
	snprintf(path, n, "%s/stonefly-%s.sock", runtime, display);
	return path;
}

/* Parse {"cmd":"<name>"} (optional whitespace, string values only). */
static bool parse_cmd(const char *line, char *cmd, size_t cmd_cap) {
	struct json_scan s;
	json_scan_init(&s, line);
	if (!json_scan_expect(&s, '{')) {
		return false;
	}

	json_scan_skip_ws(&s);
	if (s.p < s.end && *s.p == '}') {
		return false;
	}

	bool found = false;
	for (;;) {
		char key[64];
		if (!json_scan_string(&s, key, sizeof(key)) ||
				!json_scan_expect(&s, ':')) {
			return false;
		}

		char value[128];
		if (!json_scan_string(&s, value, sizeof(value))) {
			return false;
		}
		if (strcmp(key, "cmd") == 0) {
			snprintf(cmd, cmd_cap, "%s", value);
			found = true;
		}

		json_scan_skip_ws(&s);
		if (s.p < s.end && *s.p == ',') {
			s.p++;
			continue;
		}
		if (!json_scan_expect(&s, '}')) {
			return false;
		}
		return found && cmd[0] != '\0';
	}
}

static void ipc_client_destroy(struct stonefly_ipc_client *client) {
	if (client->source != NULL) {
		wl_event_source_remove(client->source);
		client->source = NULL;
	}
	if (client->fd >= 0) {
		close(client->fd);
		client->fd = -1;
	}
	free(client);
}

static void ipc_write_line(int fd, const char *payload) {
	size_t n = strlen(payload);
	char *line = malloc(n + 2);
	if (line == NULL) {
		return;
	}
	memcpy(line, payload, n);
	line[n] = '\n';
	line[n + 1] = '\0';
	if (write(fd, line, n + 1) < 0) {
		SF_LOGE("ipc write", sf_str("err", strerror(errno)));
	}
	free(line);
}

static void ipc_handle_windows(struct stonefly_ipc_client *client) {
	struct stonefly_window_info infos[STONEFLY_IPC_MAX_WINDOWS];
	size_t n = stonefly_windows_collect(client->ipc->server, infos,
		STONEFLY_IPC_MAX_WINDOWS);

	char *out = malloc(STONEFLY_IPC_REPLY_CAP);
	if (out == NULL) {
		ipc_write_line(client->fd, "{\"error\":\"oom\"}");
		return;
	}
	if (stonefly_windows_to_json(infos, n, out, STONEFLY_IPC_REPLY_CAP) < 0) {
		ipc_write_line(client->fd, "{\"error\":\"windows snapshot overflow\"}");
	} else {
		ipc_write_line(client->fd, out);
	}
	free(out);
}

static void ipc_handle_layers(struct stonefly_ipc_client *client) {
	struct stonefly_layer_info infos[STONEFLY_IPC_MAX_LAYERS];
	size_t n = stonefly_layers_collect(client->ipc->server, infos,
		STONEFLY_IPC_MAX_LAYERS);

	char *out = malloc(STONEFLY_IPC_REPLY_CAP);
	if (out == NULL) {
		ipc_write_line(client->fd, "{\"error\":\"oom\"}");
		return;
	}
	if (stonefly_layers_to_json(infos, n, out, STONEFLY_IPC_REPLY_CAP) < 0) {
		ipc_write_line(client->fd, "{\"error\":\"layers snapshot overflow\"}");
	} else {
		ipc_write_line(client->fd, out);
	}
	free(out);
}

static void ipc_handle_request(struct stonefly_ipc_client *client, const char *line) {
	char cmd[64];
	if (!parse_cmd(line, cmd, sizeof(cmd))) {
		ipc_write_line(client->fd, "{\"error\":\"bad request\"}");
		return;
	}

	if (strcmp(cmd, "windows") == 0) {
		ipc_handle_windows(client);
		return;
	}
	if (strcmp(cmd, "layers") == 0) {
		ipc_handle_layers(client);
		return;
	}

	char err[160];
	struct json_buf b;
	json_buf_init(&b, err, sizeof(err));
	json_byte(&b, '{');
	json_key(&b, "error");
	char msg[96];
	snprintf(msg, sizeof(msg), "unknown command \"%s\"", cmd);
	json_str(&b, msg);
	json_byte(&b, '}');
	json_finish(&b);
	ipc_write_line(client->fd, json_ok(&b) ? err : "{\"error\":\"unknown command\"}");
}

static int ipc_client_readable(int fd, uint32_t mask, void *data) {
	struct stonefly_ipc_client *client = data;
	(void)fd;

	if (mask & (WL_EVENT_ERROR | WL_EVENT_HANGUP)) {
		ipc_client_destroy(client);
		return 0;
	}
	if (!(mask & WL_EVENT_READABLE)) {
		return 0;
	}

	if (client->len + 1 >= sizeof(client->buf)) {
		ipc_write_line(client->fd, "{\"error\":\"request too large\"}");
		ipc_client_destroy(client);
		return 0;
	}

	ssize_t n = read(client->fd, client->buf + client->len,
		sizeof(client->buf) - client->len - 1);
	if (n <= 0) {
		ipc_client_destroy(client);
		return 0;
	}
	client->len += (size_t)n;
	client->buf[client->len] = '\0';

	char *nl = memchr(client->buf, '\n', client->len);
	if (nl == NULL) {
		return 0;
	}
	*nl = '\0';
	ipc_handle_request(client, client->buf);
	ipc_client_destroy(client);
	return 0;
}

static int ipc_listen_readable(int fd, uint32_t mask, void *data) {
	struct stonefly_ipc *ipc = data;
	(void)fd;

	if (mask & (WL_EVENT_ERROR | WL_EVENT_HANGUP)) {
		SF_LOGE("ipc listen", sf_str("err", "hangup/error"));
		return 0;
	}
	if (!(mask & WL_EVENT_READABLE)) {
		return 0;
	}

	int cfd = accept(ipc->fd, NULL, NULL);
	if (cfd < 0) {
		SF_LOGE("ipc accept", sf_str("err", strerror(errno)));
		return 0;
	}
	fd_set_cloexec(cfd);

	struct stonefly_ipc_client *client = calloc(1, sizeof(*client));
	if (client == NULL) {
		close(cfd);
		return 0;
	}
	client->ipc = ipc;
	client->fd = cfd;

	struct wl_event_loop *loop =
		wl_display_get_event_loop(ipc->server->wl_display);
	client->source = wl_event_loop_add_fd(loop, cfd,
		WL_EVENT_READABLE | WL_EVENT_HANGUP | WL_EVENT_ERROR,
		ipc_client_readable, client);
	if (client->source == NULL) {
		close(cfd);
		free(client);
	}
	return 0;
}

bool stonefly_ipc_start(struct stonefly_server *server) {
	char *path = socket_path();
	if (path == NULL) {
		SF_LOGE("ipc", sf_str("err",
			"neither STONEFLY_SOCKET nor XDG_RUNTIME_DIR+WAYLAND_DISPLAY are set"));
		return false;
	}

	(void)unlink(path);

	int fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (fd < 0) {
		SF_LOGE("ipc socket", sf_str("err", strerror(errno)));
		free(path);
		return false;
	}
	fd_set_cloexec(fd);

	struct sockaddr_un addr;
	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	if (strlen(path) >= sizeof(addr.sun_path)) {
		SF_LOGE("ipc", sf_str("err", "socket path too long"), sf_str("path", path));
		close(fd);
		free(path);
		return false;
	}
	snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", path);

	if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		SF_LOGE("ipc bind", sf_str("path", path), sf_str("err", strerror(errno)));
		close(fd);
		free(path);
		return false;
	}
	if (listen(fd, 8) < 0) {
		SF_LOGE("ipc listen", sf_str("err", strerror(errno)));
		close(fd);
		(void)unlink(path);
		free(path);
		return false;
	}

	struct stonefly_ipc *ipc = calloc(1, sizeof(*ipc));
	if (ipc == NULL) {
		close(fd);
		(void)unlink(path);
		free(path);
		return false;
	}
	ipc->server = server;
	ipc->fd = fd;
	ipc->path = path;

	struct wl_event_loop *loop = wl_display_get_event_loop(server->wl_display);
	ipc->source = wl_event_loop_add_fd(loop, fd,
		WL_EVENT_READABLE | WL_EVENT_HANGUP | WL_EVENT_ERROR,
		ipc_listen_readable, ipc);
	if (ipc->source == NULL) {
		close(fd);
		(void)unlink(path);
		free(path);
		free(ipc);
		return false;
	}

	server->ipc = ipc;
	SF_LOGI("ipc listening", sf_str("path", path));
	return true;
}

void stonefly_ipc_stop(struct stonefly_server *server) {
	struct stonefly_ipc *ipc = server->ipc;
	if (ipc == NULL) {
		return;
	}
	if (ipc->source != NULL) {
		wl_event_source_remove(ipc->source);
	}
	if (ipc->fd >= 0) {
		close(ipc->fd);
	}
	if (ipc->path != NULL) {
		(void)unlink(ipc->path);
		free(ipc->path);
	}
	free(ipc);
	server->ipc = NULL;
}

/* ---- client ----------------------------------------------------------- */

static int ipc_request(const char *cmd, char **reply_out) {
	*reply_out = NULL;

	char *path = socket_path();
	if (path == NULL) {
		fprintf(stderr,
			"stonefly: neither %s nor XDG_RUNTIME_DIR+WAYLAND_DISPLAY are set\n",
			STONEFLY_IPC_ENV);
		return 1;
	}

	int fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (fd < 0) {
		fprintf(stderr, "stonefly: socket: %s\n", strerror(errno));
		free(path);
		return 1;
	}
	fd_set_cloexec(fd);

	struct sockaddr_un addr;
	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	if (strlen(path) >= sizeof(addr.sun_path)) {
		fprintf(stderr, "stonefly: socket path too long: %s\n", path);
		close(fd);
		free(path);
		return 1;
	}
	snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", path);

	if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		fprintf(stderr, "stonefly: connect %s: %s (is stonefly running?)\n",
			path, strerror(errno));
		close(fd);
		free(path);
		return 1;
	}
	free(path);

	char req[128];
	struct json_buf b;
	json_buf_init(&b, req, sizeof(req));
	json_byte(&b, '{');
	json_key(&b, "cmd");
	json_str(&b, cmd);
	json_byte(&b, '}');
	json_byte(&b, '\n');
	json_finish(&b);
	if (!json_ok(&b) || write(fd, req, b.len) != (ssize_t)b.len) {
		fprintf(stderr, "stonefly: write: %s\n", strerror(errno));
		close(fd);
		return 1;
	}

	char *reply = malloc(STONEFLY_IPC_REPLY_CAP);
	if (reply == NULL) {
		close(fd);
		return 1;
	}

	size_t len = 0;
	for (;;) {
		if (len + 1 >= STONEFLY_IPC_REPLY_CAP) {
			break;
		}
		ssize_t r = read(fd, reply + len, STONEFLY_IPC_REPLY_CAP - len - 1);
		if (r < 0) {
			fprintf(stderr, "stonefly: read: %s\n", strerror(errno));
			free(reply);
			close(fd);
			return 1;
		}
		if (r == 0) {
			break;
		}
		len += (size_t)r;
		if (memchr(reply, '\n', len) != NULL) {
			break;
		}
	}
	close(fd);

	reply[len] = '\0';
	char *nl = strchr(reply, '\n');
	if (nl != NULL) {
		*nl = '\0';
	}
	*reply_out = reply;
	return 0;
}

int stonefly_ipc_cmd_windows(int argc, char **argv) {
	bool as_json = false;

	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--json") == 0) {
			as_json = true;
		} else if (strcmp(argv[i], "-h") == 0 ||
				strcmp(argv[i], "--help") == 0) {
			printf("Usage: stonefly windows [--json]\n");
			return 0;
		} else {
			fprintf(stderr, "Usage: stonefly windows [--json]\n");
			return 1;
		}
	}

	char *reply = NULL;
	int rc = ipc_request("windows", &reply);
	if (rc != 0) {
		return rc;
	}

	if (reply[0] == '{' && strncmp(reply, "{\"error\":", 9) == 0) {
		fprintf(stderr, "stonefly: %s\n", reply);
		free(reply);
		return 1;
	}

	if (as_json) {
		printf("%s\n", reply);
		free(reply);
		return 0;
	}

	struct stonefly_window_info infos[STONEFLY_IPC_MAX_WINDOWS];
	size_t n = 0;
	if (!stonefly_windows_from_json(reply, infos, STONEFLY_IPC_MAX_WINDOWS, &n)) {
		fprintf(stderr, "stonefly: bad windows reply\n");
		free(reply);
		return 1;
	}
	free(reply);
	stonefly_windows_print(infos, n, stdout);
	return 0;
}

int stonefly_ipc_cmd_layers(int argc, char **argv) {
	bool as_json = false;

	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--json") == 0) {
			as_json = true;
		} else if (strcmp(argv[i], "-h") == 0 ||
				strcmp(argv[i], "--help") == 0) {
			printf("Usage: stonefly layers [--json]\n");
			return 0;
		} else {
			fprintf(stderr, "Usage: stonefly layers [--json]\n");
			return 1;
		}
	}

	char *reply = NULL;
	int rc = ipc_request("layers", &reply);
	if (rc != 0) {
		return rc;
	}

	if (reply[0] == '{' && strncmp(reply, "{\"error\":", 9) == 0) {
		fprintf(stderr, "stonefly: %s\n", reply);
		free(reply);
		return 1;
	}

	if (as_json) {
		printf("%s\n", reply);
		free(reply);
		return 0;
	}

	struct stonefly_layer_info infos[STONEFLY_IPC_MAX_LAYERS];
	size_t n = 0;
	if (!stonefly_layers_from_json(reply, infos, STONEFLY_IPC_MAX_LAYERS, &n)) {
		fprintf(stderr, "stonefly: bad layers reply\n");
		free(reply);
		return 1;
	}
	free(reply);
	stonefly_layers_print(infos, n, stdout);
	return 0;
}
