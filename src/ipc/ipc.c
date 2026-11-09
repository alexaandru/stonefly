#include "ipc/ipc.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <wayland-server-core.h>

#include "decor/decor.h"
#include "ipc/config.h"
#include "ipc/layers.h"
#include "ipc/outputs.h"
#include "ipc/windows.h"
#include "ipc/workspaces.h"
#include "log/log.h"
#include "server/server.h"
#include "util/json.h"
#include "wm/scratchpad.h"
#include "wm/wm.h"
#include "wm/workspace.h"
#include "toplevel/toplevel.h"

#include <wlr/types/wlr_xdg_shell.h>

#define STONEFLY_IPC_ENV "STONEFLY_SOCKET"
#define STONEFLY_IPC_REPLY_CAP ((size_t)64 * 1024)
#define STONEFLY_IPC_REQ_CAP 4096
#define STONEFLY_IPC_MAX_WINDOWS 256
#define STONEFLY_IPC_MAX_LAYERS 256
#define STONEFLY_IPC_MAX_OUTPUTS 32
#define STONEFLY_IPC_MAX_WORKSPACES 128
#define STONEFLY_IPC_QUICK_TERM "term"

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
		(void)fcntl(fd, F_SETFD, (uint32_t)flags | (uint32_t)FD_CLOEXEC);
	}
}

static char *socket_path(void) {
	const char *override = getenv(STONEFLY_IPC_ENV);
	if (override != NULL && override[0] != '\0') {
		return strdup(override);
	}

	const char *runtime = getenv("XDG_RUNTIME_DIR");
	const char *display = getenv("WAYLAND_DISPLAY");
	if (runtime == NULL || runtime[0] == '\0' || display == NULL || display[0] == '\0') {
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

/* Parse {"cmd":"<name>","arg":"..."} (arg optional; string values only). */
static bool parse_cmd_arg(const char *line, char *cmd, size_t cmd_cap, char *arg, size_t arg_cap) {
	if (arg != NULL && arg_cap > 0) {
		arg[0] = '\0';
	}

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
		if (!json_scan_string(&s, key, sizeof(key)) || !json_scan_expect(&s, ':')) {
			return false;
		}

		char value[128];
		if (!json_scan_string(&s, value, sizeof(value))) {
			return false;
		}
		if (strcmp(key, "cmd") == 0) {
			snprintf(cmd, cmd_cap, "%s", value);
			found = true;
		} else if (strcmp(key, "arg") == 0 && arg != NULL && arg_cap > 0) {
			snprintf(arg, arg_cap, "%s", value);
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

static struct stonefly_toplevel *toplevel_by_id(struct stonefly_server *server, uint64_t id) {
	struct stonefly_toplevel *toplevel;
	wl_list_for_each(toplevel, &server->toplevels, link) {
		if (toplevel->id == id) {
			return toplevel;
		}
	}
	wl_list_for_each(toplevel, &server->minimized, minimize_link) {
		if (toplevel->id == id) {
			return toplevel;
		}
	}
	return NULL;
}

static bool parse_id_arg(const char *arg, uint64_t *out) {
	if (arg == NULL || arg[0] == '\0' || out == NULL) {
		return false;
	}
	char *end = NULL;
	unsigned long long v = strtoull(arg, &end, 10);
	if (end == arg || (end != NULL && *end != '\0')) {
		return false;
	}
	*out = (uint64_t)v;
	return true;
}

static bool parse_workspace_index(const char *arg, int *out) {
	if (arg == NULL || arg[0] == '\0' || out == NULL) {
		return false;
	}
	char *end = NULL;
	errno = 0;
	long v = strtol(arg, &end, 10);
	if (errno != 0 || end == arg || *end != '\0' || v <= 0 || v > (long)INT_MAX) {
		return false;
	}
	*out = (int)v;
	return true;
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

static void ipc_write_ok(struct stonefly_ipc_client *client) {
	ipc_write_line(client->fd, "{\"ok\":true}");
}

static void ipc_write_err(struct stonefly_ipc_client *client, const char *msg) {
	char err[192];
	struct json_buf b;
	json_buf_init(&b, err, sizeof(err));
	json_byte(&b, '{');
	json_key(&b, "error");
	json_str(&b, msg);
	json_byte(&b, '}');
	json_finish(&b);
	ipc_write_line(client->fd, json_ok(&b) ? err : "{\"error\":\"oom\"}");
}

static void ipc_handle_windows(struct stonefly_ipc_client *client) {
	struct stonefly_window_info infos[STONEFLY_IPC_MAX_WINDOWS];
	size_t n = stonefly_windows_collect(client->ipc->server, infos, STONEFLY_IPC_MAX_WINDOWS);

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
	size_t n = stonefly_layers_collect(client->ipc->server, infos, STONEFLY_IPC_MAX_LAYERS);

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

static void ipc_handle_outputs(struct stonefly_ipc_client *client) {
	struct stonefly_output_info infos[STONEFLY_IPC_MAX_OUTPUTS];
	size_t n = stonefly_outputs_collect(client->ipc->server, infos, STONEFLY_IPC_MAX_OUTPUTS);

	char *out = malloc(STONEFLY_IPC_REPLY_CAP);
	if (out == NULL) {
		ipc_write_line(client->fd, "{\"error\":\"oom\"}");
		return;
	}
	if (stonefly_outputs_to_json(infos, n, out, STONEFLY_IPC_REPLY_CAP) < 0) {
		ipc_write_line(client->fd, "{\"error\":\"outputs snapshot overflow\"}");
	} else {
		ipc_write_line(client->fd, out);
	}
	free(out);
}

static void ipc_handle_workspaces(struct stonefly_ipc_client *client) {
	struct stonefly_workspace_info infos[STONEFLY_IPC_MAX_WORKSPACES];
	size_t n = stonefly_workspaces_collect(client->ipc->server, infos,
					       STONEFLY_IPC_MAX_WORKSPACES);

	char *out = malloc(STONEFLY_IPC_REPLY_CAP);
	if (out == NULL) {
		ipc_write_line(client->fd, "{\"error\":\"oom\"}");
		return;
	}
	if (stonefly_workspaces_to_json(infos, n, out, STONEFLY_IPC_REPLY_CAP) < 0) {
		ipc_write_line(client->fd, "{\"error\":\"workspaces snapshot overflow\"}");
	} else {
		ipc_write_line(client->fd, out);
	}
	free(out);
}

static void ipc_handle_workspace(struct stonefly_ipc_client *client, const char *arg) {
	struct stonefly_server *server = client->ipc->server;
	struct stonefly_output *output = stonefly_output_at_cursor(server);
	if (output == NULL) {
		ipc_write_err(client, "no output");
		return;
	}
	if (arg == NULL || arg[0] == '\0') {
		ipc_write_err(client, "workspace requires arg next|prev|N");
		return;
	}
	if (strcmp(arg, "next") == 0) {
		stonefly_workspace_switch_offset(output, 1);
		ipc_write_ok(client);
		return;
	}
	if (strcmp(arg, "prev") == 0) {
		stonefly_workspace_switch_offset(output, -1);
		ipc_write_ok(client);
		return;
	}
	int idx;
	if (!parse_workspace_index(arg, &idx) || !stonefly_workspace_switch_index(output, idx)) {
		ipc_write_err(client, "unknown workspace");
		return;
	}
	ipc_write_ok(client);
}

static void ipc_handle_move_to_workspace(struct stonefly_ipc_client *client, const char *arg) {
	struct stonefly_server *server = client->ipc->server;
	struct stonefly_toplevel *focused = stonefly_focused_toplevel(server);
	if (focused == NULL) {
		ipc_write_err(client, "no focused window");
		return;
	}
	if (arg == NULL || arg[0] == '\0') {
		ipc_write_err(client, "move-to-workspace requires arg next|prev|N");
		return;
	}
	if (strcmp(arg, "next") == 0) {
		stonefly_workspace_move_offset(focused, 1, true);
		ipc_write_ok(client);
		return;
	}
	if (strcmp(arg, "prev") == 0) {
		stonefly_workspace_move_offset(focused, -1, true);
		ipc_write_ok(client);
		return;
	}
	int idx;
	if (!parse_workspace_index(arg, &idx) ||
	    !stonefly_workspace_move_index(focused, idx, true)) {
		ipc_write_err(client, "unknown workspace");
		return;
	}
	ipc_write_ok(client);
}

static void ipc_handle_focus(struct stonefly_ipc_client *client, const char *arg) {
	uint64_t id;
	if (!parse_id_arg(arg, &id)) {
		ipc_write_err(client, "focus requires numeric id arg");
		return;
	}
	struct stonefly_toplevel *toplevel = toplevel_by_id(client->ipc->server, id);
	if (toplevel == NULL) {
		ipc_write_err(client, "no window with that id");
		return;
	}
	if (toplevel->minimized) {
		stonefly_toplevel_unminimize(toplevel);
	}
	stonefly_focus_toplevel(toplevel);
	ipc_write_ok(client);
}

static void ipc_handle_close(struct stonefly_ipc_client *client, const char *arg) {
	uint64_t id;
	if (!parse_id_arg(arg, &id)) {
		ipc_write_err(client, "close requires numeric id arg");
		return;
	}
	struct stonefly_toplevel *toplevel = toplevel_by_id(client->ipc->server, id);
	if (toplevel == NULL) {
		ipc_write_err(client, "no window with that id");
		return;
	}
	wlr_xdg_toplevel_send_close(toplevel->xdg_toplevel);
	ipc_write_ok(client);
}

static void ipc_handle_block_capture(struct stonefly_ipc_client *client, const char *arg,
				     bool block) {
	uint64_t id;
	if (!parse_id_arg(arg, &id)) {
		ipc_write_err(client, block ? "block-capture requires numeric id arg"
					    : "unblock-capture requires numeric id arg");
		return;
	}
	struct stonefly_toplevel *toplevel = toplevel_by_id(client->ipc->server, id);
	if (toplevel == NULL) {
		ipc_write_err(client, "no window with that id");
		return;
	}
	toplevel->capture_blocked = block;
	ipc_write_ok(client);
}

static void ipc_handle_urgent(struct stonefly_ipc_client *client, const char *arg, bool urgent) {
	uint64_t id;
	if (!parse_id_arg(arg, &id)) {
		ipc_write_err(client, urgent ? "set-urgent requires numeric id arg"
					     : "unset-urgent requires numeric id arg");
		return;
	}
	struct stonefly_toplevel *toplevel = toplevel_by_id(client->ipc->server, id);
	if (toplevel == NULL) {
		ipc_write_err(client, "no window with that id");
		return;
	}
	stonefly_decor_set_urgent(toplevel, urgent);
	ipc_write_ok(client);
}

static void ipc_handle_toggle_quick_terminal(struct stonefly_ipc_client *client) {
	stonefly_scratchpad_toggle(client->ipc->server, STONEFLY_IPC_QUICK_TERM);
	ipc_write_ok(client);
}

/* Stop this compositor only (socket is per WAYLAND_DISPLAY / STONEFLY_SOCKET). */
static void ipc_handle_exit(struct stonefly_ipc_client *client) {
	struct stonefly_server *server = client->ipc->server;
	ipc_write_ok(client);
	SF_LOGI("ipc exit");
	wl_display_terminate(server->wl_display);
}

static void ipc_handle_config(struct stonefly_ipc_client *client, const char *arg) {
	char *out = malloc(STONEFLY_IPC_REPLY_CAP);
	if (out == NULL) {
		ipc_write_line(client->fd, "{\"error\":\"oom\"}");
		return;
	}
	const char *section = (arg != NULL && arg[0] != '\0') ? arg : NULL;
	int n = stonefly_config_to_json(client->ipc->server, section, out, STONEFLY_IPC_REPLY_CAP);
	if (n < 0) {
		free(out);
		if (section != NULL) {
			ipc_write_err(client, "unknown config section "
					      "(options|binds|taps|scratchpads|window_rules|"
					      "inputs|outputs)");
		} else {
			ipc_write_line(client->fd, "{\"error\":\"config snapshot overflow\"}");
		}
		return;
	}
	ipc_write_line(client->fd, out);
	free(out);
}

static void ipc_handle_request(struct stonefly_ipc_client *client, const char *line) {
	char cmd[64];
	char arg[128];
	if (!parse_cmd_arg(line, cmd, sizeof(cmd), arg, sizeof(arg))) {
		ipc_write_err(client, "bad request");
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
	if (strcmp(cmd, "outputs") == 0) {
		ipc_handle_outputs(client);
		return;
	}
	if (strcmp(cmd, "workspaces") == 0) {
		ipc_handle_workspaces(client);
		return;
	}
	if (strcmp(cmd, "workspace") == 0) {
		ipc_handle_workspace(client, arg);
		return;
	}
	if (strcmp(cmd, "move-to-workspace") == 0) {
		ipc_handle_move_to_workspace(client, arg);
		return;
	}
	if (strcmp(cmd, "focus") == 0) {
		ipc_handle_focus(client, arg);
		return;
	}
	if (strcmp(cmd, "close") == 0) {
		ipc_handle_close(client, arg);
		return;
	}
	if (strcmp(cmd, "block-capture") == 0) {
		ipc_handle_block_capture(client, arg, true);
		return;
	}
	if (strcmp(cmd, "unblock-capture") == 0) {
		ipc_handle_block_capture(client, arg, false);
		return;
	}
	if (strcmp(cmd, "set-urgent") == 0) {
		ipc_handle_urgent(client, arg, true);
		return;
	}
	if (strcmp(cmd, "unset-urgent") == 0) {
		ipc_handle_urgent(client, arg, false);
		return;
	}
	if (strcmp(cmd, "toggle-quick-terminal") == 0) {
		ipc_handle_toggle_quick_terminal(client);
		return;
	}
	if (strcmp(cmd, "config") == 0) {
		ipc_handle_config(client, arg);
		return;
	}
	if (strcmp(cmd, "exit") == 0 || strcmp(cmd, "quit") == 0) {
		ipc_handle_exit(client);
		return;
	}

	char msg[96];
	snprintf(msg, sizeof(msg), "unknown command \"%s\"", cmd);
	ipc_write_err(client, msg);
}

static int ipc_client_readable(int fd, uint32_t mask, void *data) {
	struct stonefly_ipc_client *client = data;
	(void)fd;

	if (mask & ((uint32_t)WL_EVENT_ERROR | (uint32_t)WL_EVENT_HANGUP)) {
		ipc_client_destroy(client);
		return 0;
	}
	if (!(mask & (uint32_t)WL_EVENT_READABLE)) {
		return 0;
	}

	if (client->len + 1 >= sizeof(client->buf)) {
		ipc_write_line(client->fd, "{\"error\":\"request too large\"}");
		ipc_client_destroy(client);
		return 0;
	}

	ssize_t n =
		read(client->fd, client->buf + client->len, sizeof(client->buf) - client->len - 1);
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

	if (mask & ((uint32_t)WL_EVENT_ERROR | (uint32_t)WL_EVENT_HANGUP)) {
		SF_LOGE("ipc listen", sf_str("err", "hangup/error"));
		return 0;
	}
	if (!(mask & (uint32_t)WL_EVENT_READABLE)) {
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

	struct wl_event_loop *loop = wl_display_get_event_loop(ipc->server->wl_display);
	client->source = wl_event_loop_add_fd(
		loop, cfd,
		(uint32_t)WL_EVENT_READABLE | (uint32_t)WL_EVENT_HANGUP | (uint32_t)WL_EVENT_ERROR,
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
		SF_LOGE("ipc", sf_str("err", "neither STONEFLY_SOCKET nor "
					     "XDG_RUNTIME_DIR+WAYLAND_DISPLAY are set"));
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
					   (uint32_t)WL_EVENT_READABLE | (uint32_t)WL_EVENT_HANGUP |
						   (uint32_t)WL_EVENT_ERROR,
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

static int ipc_request_arg(const char *cmd, const char *arg, char **reply_out) {
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
		fprintf(stderr, "stonefly: connect %s: %s (is stonefly running?)\n", path,
			strerror(errno));
		close(fd);
		free(path);
		return 1;
	}
	free(path);

	char req[256];
	struct json_buf b;
	json_buf_init(&b, req, sizeof(req));
	json_byte(&b, '{');
	json_key(&b, "cmd");
	json_str(&b, cmd);
	if (arg != NULL && arg[0] != '\0') {
		json_byte(&b, ',');
		json_key(&b, "arg");
		json_str(&b, arg);
	}
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
		size_t space = STONEFLY_IPC_REPLY_CAP - len - 1;
		ssize_t r = read(fd, reply + len, space);
		if (r < 0) {
			fprintf(stderr, "stonefly: read: %s\n", strerror(errno));
			free(reply);
			close(fd);
			return 1;
		}
		if (r == 0) {
			break;
		}
		size_t nr = (size_t)r;
		if (nr > space) {
			nr = space;
		}
		len += nr;
		if (memchr(reply, '\n', len) != NULL) {
			break;
		}
	}
	close(fd);

	if (len >= STONEFLY_IPC_REPLY_CAP) {
		len = STONEFLY_IPC_REPLY_CAP - 1;
	}
	reply[len] = '\0';
	char *nl = strchr(reply, '\n');
	if (nl != NULL) {
		*nl = '\0';
	}
	*reply_out = reply;
	return 0;
}

static int ipc_request(const char *cmd, char **reply_out) {
	return ipc_request_arg(cmd, NULL, reply_out);
}

static int ipc_print_ok_reply(char *reply) {
	if (reply[0] == '{' && strncmp(reply, "{\"error\":", 9) == 0) {
		fprintf(stderr, "stonefly: %s\n", reply);
		free(reply);
		return 1;
	}
	printf("%s\n", reply);
	free(reply);
	return 0;
}

/* Human-readable formatter for list replies; must not free reply. */
typedef int (*ipc_list_format_fn)(const char *reply);

static int ipc_cmd_list(int argc, char **argv, const char *cmd, const char *usage,
			ipc_list_format_fn format) {
	bool as_json = false;

	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--json") == 0) {
			as_json = true;
		} else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
			printf("%s\n", usage);
			return 0;
		} else {
			fprintf(stderr, "%s\n", usage);
			return 1;
		}
	}

	char *reply = NULL;
	int rc = ipc_request(cmd, &reply);
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

	rc = format(reply);
	free(reply);
	return rc;
}

static int ipc_cmd_with_id(int argc, char **argv, const char *cmd, const char *usage) {
	const char *id = NULL;
	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
			printf("%s\n", usage);
			return 0;
		}
		if (id != NULL) {
			fprintf(stderr, "%s\n", usage);
			return 1;
		}
		id = argv[i];
	}
	if (id == NULL) {
		fprintf(stderr, "%s\n", usage);
		return 1;
	}

	char *reply = NULL;
	int rc = ipc_request_arg(cmd, id, &reply);
	if (rc != 0) {
		return rc;
	}
	return ipc_print_ok_reply(reply);
}

/* help_detail is optional extra lines after usage (help only; not stderr). */
static int ipc_cmd_ok_noarg(int argc, char **argv, const char *cmd, const char *usage,
			    const char *help_detail) {
	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
			printf("%s\n", usage);
			if (help_detail != NULL) {
				fputs(help_detail, stdout);
			}
			return 0;
		}
		fprintf(stderr, "%s\n", usage);
		return 1;
	}

	char *reply = NULL;
	int rc = ipc_request(cmd, &reply);
	if (rc != 0) {
		return rc;
	}
	return ipc_print_ok_reply(reply);
}

static int windows_format(const char *reply) {
	struct stonefly_window_info infos[STONEFLY_IPC_MAX_WINDOWS];
	size_t n = 0;
	if (!stonefly_windows_from_json(reply, infos, STONEFLY_IPC_MAX_WINDOWS, &n)) {
		fprintf(stderr, "stonefly: bad windows reply\n");
		return 1;
	}
	stonefly_windows_print(infos, n, stdout);
	return 0;
}

static int layers_format(const char *reply) {
	struct stonefly_layer_info infos[STONEFLY_IPC_MAX_LAYERS];
	size_t n = 0;
	if (!stonefly_layers_from_json(reply, infos, STONEFLY_IPC_MAX_LAYERS, &n)) {
		fprintf(stderr, "stonefly: bad layers reply\n");
		return 1;
	}
	stonefly_layers_print(infos, n, stdout);
	return 0;
}

static int outputs_format(const char *reply) {
	struct stonefly_output_info infos[STONEFLY_IPC_MAX_OUTPUTS];
	size_t n = 0;
	if (!stonefly_outputs_from_json(reply, infos, STONEFLY_IPC_MAX_OUTPUTS, &n)) {
		fprintf(stderr, "stonefly: bad outputs reply\n");
		return 1;
	}
	stonefly_outputs_print(infos, n, stdout);
	return 0;
}

static int workspaces_format(const char *reply) {
	struct stonefly_workspace_info infos[STONEFLY_IPC_MAX_WORKSPACES];
	size_t n = 0;
	if (!stonefly_workspaces_from_json(reply, infos, STONEFLY_IPC_MAX_WORKSPACES, &n)) {
		fprintf(stderr, "stonefly: bad workspaces reply\n");
		return 1;
	}
	stonefly_workspaces_print(infos, n, stdout);
	return 0;
}

int stonefly_ipc_cmd_windows(int argc, char **argv) {
	return ipc_cmd_list(argc, argv, "windows", "Usage: stonefly windows [--json]",
			    windows_format);
}

int stonefly_ipc_cmd_layers(int argc, char **argv) {
	return ipc_cmd_list(argc, argv, "layers", "Usage: stonefly layers [--json]", layers_format);
}

int stonefly_ipc_cmd_outputs(int argc, char **argv) {
	return ipc_cmd_list(argc, argv, "outputs", "Usage: stonefly outputs [--json]",
			    outputs_format);
}

int stonefly_ipc_cmd_workspaces(int argc, char **argv) {
	return ipc_cmd_list(argc, argv, "workspaces", "Usage: stonefly workspaces [--json]",
			    workspaces_format);
}

int stonefly_ipc_cmd_focus(int argc, char **argv) {
	return ipc_cmd_with_id(argc, argv, "focus", "Usage: stonefly focus <id>");
}

int stonefly_ipc_cmd_close(int argc, char **argv) {
	return ipc_cmd_with_id(argc, argv, "close", "Usage: stonefly close <id>");
}

int stonefly_ipc_cmd_block_capture(int argc, char **argv) {
	return ipc_cmd_with_id(argc, argv, "block-capture", "Usage: stonefly block-capture <id>");
}

int stonefly_ipc_cmd_unblock_capture(int argc, char **argv) {
	return ipc_cmd_with_id(argc, argv, "unblock-capture",
			       "Usage: stonefly unblock-capture <id>");
}

int stonefly_ipc_cmd_set_urgent(int argc, char **argv) {
	return ipc_cmd_with_id(argc, argv, "set-urgent", "Usage: stonefly set-urgent <id>");
}

int stonefly_ipc_cmd_unset_urgent(int argc, char **argv) {
	return ipc_cmd_with_id(argc, argv, "unset-urgent", "Usage: stonefly unset-urgent <id>");
}

int stonefly_ipc_cmd_toggle_quick_terminal(int argc, char **argv) {
	return ipc_cmd_ok_noarg(argc, argv, "toggle-quick-terminal",
				"Usage: stonefly toggle-quick-terminal", NULL);
}

int stonefly_ipc_cmd_exit(int argc, char **argv) {
	return ipc_cmd_ok_noarg(argc, argv, "exit", "Usage: stonefly exit",
				"  Quit the compositor for WAYLAND_DISPLAY / STONEFLY_SOCKET.\n"
				"  Nested: run with that session's WAYLAND_DISPLAY set.\n");
}

int stonefly_ipc_cmd_config(int argc, char **argv) {
	bool as_json = false;
	const char *section = NULL;

	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--json") == 0) {
			as_json = true;
		} else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
			printf("Usage: stonefly config [section] [--json]\n");
			printf("Sections: options binds taps scratchpads window_rules inputs "
			       "outputs\n");
			printf("Omit section for the full live config.\n");
			return 0;
		} else if (section == NULL && argv[i][0] != '-') {
			section = argv[i];
		} else {
			fprintf(stderr, "Usage: stonefly config [section] [--json]\n");
			return 1;
		}
	}

	char *reply = NULL;
	int rc = ipc_request_arg("config", section, &reply);
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
	} else {
		stonefly_config_print_json(reply, stdout);
	}
	free(reply);
	return 0;
}
