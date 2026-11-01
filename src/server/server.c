#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include <wayland-server-core.h>
#include <wlr/backend.h>
#include <wlr/render/allocator.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_fractional_scale_v1.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_viewporter.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_xcursor_manager.h>

#include "ipc/ipc.h"
#include "log/log.h"
#include "output/output.h"
#include "seat/seat.h"
#include "server/server.h"
#include "xdg/xdg.h"

/*
 * Peeble runs the same helper at session start. D-Bus-activated apps
 * (Ghostty via `ghostty +new-window`, etc.) inherit the activation
 * environment from the session bus — not from the keybind's fork.
 * Without this, WAYLAND_DISPLAY stays stale and those clients flash
 * then die under a nested/TTY stonefly session.
 */
static void stonefly_update_dbus_activation_environment(void) {
	pid_t pid = fork();
	if (pid < 0) {
		SF_LOGW("dbus-update-activation-environment fork failed",
			sf_str("err", strerror(errno)));
		return;
	}
	if (pid == 0) {
		execlp("dbus-update-activation-environment",
			"dbus-update-activation-environment", "--all",
			(char *)NULL);
		_exit(127);
	}

	int status = 0;
	if (waitpid(pid, &status, 0) < 0) {
		SF_LOGW("dbus-update-activation-environment wait failed",
			sf_str("err", strerror(errno)));
		return;
	}
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
		int code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
		char codebuf[16];
		snprintf(codebuf, sizeof(codebuf), "%d", code);
		SF_LOGW("dbus-update-activation-environment failed",
			sf_str("exit", codebuf));
	}
}

bool stonefly_server_init(struct stonefly_server *server) {
	server->wl_display = wl_display_create();
	server->backend = wlr_backend_autocreate(
		wl_display_get_event_loop(server->wl_display), NULL);
	if (server->backend == NULL) {
		SF_LOGE("failed to create wlr_backend");
		return false;
	}

	server->renderer = wlr_renderer_autocreate(server->backend);
	if (server->renderer == NULL) {
		SF_LOGE("failed to create wlr_renderer");
		return false;
	}
	wlr_renderer_init_wl_display(server->renderer, server->wl_display);

	server->allocator = wlr_allocator_autocreate(server->backend, server->renderer);
	if (server->allocator == NULL) {
		SF_LOGE("failed to create wlr_allocator");
		return false;
	}

	wlr_compositor_create(server->wl_display, 5, server->renderer);
	wlr_subcompositor_create(server->wl_display);
	wlr_data_device_manager_create(server->wl_display);

	server->output_layout = wlr_output_layout_create(server->wl_display);
	wl_list_init(&server->minimized);

	/* Fractional-scale clients need viewporter for destination sizing. */
	wlr_viewporter_create(server->wl_display);
	wlr_fractional_scale_manager_v1_create(server->wl_display, 1);

	server->scene = wlr_scene_create();
	server->scene_layout =
		wlr_scene_attach_output_layout(server->scene, server->output_layout);

	stonefly_output_setup(server);
	stonefly_xdg_setup(server);
	stonefly_seat_setup(server);
	return true;
}

int stonefly_server_run(struct stonefly_server *server, const char *startup_cmd) {
	const char *socket = wl_display_add_socket_auto(server->wl_display);
	if (!socket) {
		wlr_backend_destroy(server->backend);
		return 1;
	}

	if (!wlr_backend_start(server->backend)) {
		wlr_backend_destroy(server->backend);
		wl_display_destroy(server->wl_display);
		return 1;
	}

	setenv("WAYLAND_DISPLAY", socket, true);
	stonefly_update_dbus_activation_environment();
	if (!stonefly_ipc_start(server)) {
		SF_LOGW("ipc not available");
	}
	if (startup_cmd != NULL) {
		if (fork() == 0) {
			execl("/bin/sh", "/bin/sh", "-c", startup_cmd, (void *)NULL);
			_exit(1);
		}
	}

	SF_LOGI("Running Wayland compositor", sf_str("display", socket));
	wl_display_run(server->wl_display);
	return 0;
}

void stonefly_server_finish(struct stonefly_server *server) {
	stonefly_ipc_stop(server);
	wl_display_destroy_clients(server->wl_display);

	wl_list_remove(&server->new_xdg_toplevel.link);
	wl_list_remove(&server->new_xdg_popup.link);

	wl_list_remove(&server->cursor_motion.link);
	wl_list_remove(&server->cursor_motion_absolute.link);
	wl_list_remove(&server->cursor_button.link);
	wl_list_remove(&server->cursor_axis.link);
	wl_list_remove(&server->cursor_frame.link);

	wl_list_remove(&server->new_input.link);
	wl_list_remove(&server->request_cursor.link);
	wl_list_remove(&server->pointer_focus_change.link);
	wl_list_remove(&server->request_set_selection.link);

	wl_list_remove(&server->new_output.link);

	wlr_scene_node_destroy(&server->scene->tree.node);
	wlr_xcursor_manager_destroy(server->cursor_mgr);
	wlr_cursor_destroy(server->cursor);
	wlr_allocator_destroy(server->allocator);
	wlr_renderer_destroy(server->renderer);
	wlr_backend_destroy(server->backend);
	wl_display_destroy(server->wl_display);
}
