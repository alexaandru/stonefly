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
#include <wlr/types/wlr_data_control_v1.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_ext_data_control_v1.h>
#include <wlr/types/wlr_fractional_scale_v1.h>
#include <wlr/types/wlr_gamma_control_v1.h>
#include <wlr/types/wlr_primary_selection_v1.h>
#include <wlr/types/wlr_screencopy_v1.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_viewporter.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_xdg_output_v1.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <scenefx/render/fx_renderer/fx_renderer.h>
#include <scenefx/types/wlr_scene.h>

#include "decor/decor.h"
#include "ipc/ipc.h"
#include "layer/layer.h"
#include "log/log.h"
#include "output/output.h"
#include "seat/seat.h"
#include "server/server.h"
#include "session/session.h"
#include "toplevel/ext_capture.h"
#include "toplevel/foreign.h"
#include "wm/drag_snap.h"
#include "wm/scratchpad.h"
#include "wm/snap_preview.h"
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
	stonefly_config_set_defaults(&server->config);

	server->wl_display = wl_display_create();
	server->session = NULL;
	server->backend = wlr_backend_autocreate(
		wl_display_get_event_loop(server->wl_display), &server->session);
	if (server->backend == NULL) {
		SF_LOGE("failed to create wlr_backend");
		return false;
	}

	/* SceneFX FX renderer (drop-in for wlr_renderer_autocreate). */
	server->renderer = fx_renderer_create(server->backend);
	if (server->renderer == NULL) {
		SF_LOGE("failed to create fx_renderer");
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
	/* Middle-click paste + data-control primary path. */
	wlr_primary_selection_v1_device_manager_create(server->wl_display);

	/*
	 * Clipboard history (Noctalia / cliphist): ext-data-control preferred;
	 * keep legacy wlr-data-control for older clients while wlroots ships it.
	 */
	server->ext_data_control_mgr =
		wlr_ext_data_control_manager_v1_create(server->wl_display, 1);
	server->wlr_data_control_mgr =
		wlr_data_control_manager_v1_create(server->wl_display);

	server->output_layout = wlr_output_layout_create(server->wl_display);
	wl_list_init(&server->minimized);
	wl_list_init(&server->ghosts);

	/*
	 * Logical size/position for clients (Noctalia panel centering, etc.).
	 * Without this, shells fall back to mode/wl_output.scale (integer) and
	 * mis-center under fractional STONEFLY_OUTPUT_SCALE.
	 */
	wlr_xdg_output_manager_v1_create(server->wl_display, server->output_layout);

	/* Fractional-scale clients need viewporter for destination sizing. */
	wlr_viewporter_create(server->wl_display);
	wlr_fractional_scale_manager_v1_create(server->wl_display, 1);

	server->scene = wlr_scene_create();
	server->scene_layout =
		wlr_scene_attach_output_layout(server->scene, server->output_layout);

	/* Night light: SceneFX applies LUTs on output commit. */
	server->gamma_control_mgr =
		wlr_gamma_control_manager_v1_create(server->wl_display);
	if (server->gamma_control_mgr != NULL) {
		wlr_scene_set_gamma_control_manager_v1(server->scene,
			server->gamma_control_mgr);
	}

	/* Noctalia region/fullscreen screenshots + lock desktop capture. */
	wlr_screencopy_manager_v1_create(server->wl_display);

	/* Config before seat/scratchpad/output consumers read policy. */
	if (!stonefly_config_load(server)) {
		SF_LOGE("config load failed");
		return false;
	}

	stonefly_layer_setup(server);
	/* After layers so layers.lock sits above overlay. */
	stonefly_session_setup(server);
	stonefly_snap_preview_init(server);
	stonefly_drag_snap_reset(&server->drag_snap);
	stonefly_output_setup(server);
	stonefly_xdg_setup(server);
	stonefly_foreign_toplevel_setup(server);
	stonefly_ext_capture_setup(server);
	stonefly_seat_setup(server);
	stonefly_scratchpad_init(server);
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
	stonefly_config_finish(&server->config);

	stonefly_ghost_finish_all(server);
	stonefly_snap_preview_finish(server);
	stonefly_seat_finish(server);
	stonefly_scratchpad_finish(server);
	stonefly_session_finish(server);
	stonefly_layer_finish(server);

	wl_list_remove(&server->new_xdg_toplevel.link);
	wl_list_remove(&server->new_xdg_popup.link);
	if (server->xdg_activation != NULL) {
		wl_list_remove(&server->request_activate.link);
	}
	if (server->system_bell != NULL) {
		wl_list_remove(&server->system_bell_ring.link);
	}
	if (server->xdg_toplevel_tag_mgr != NULL) {
		wl_list_remove(&server->xdg_toplevel_set_tag.link);
		wl_list_remove(&server->xdg_toplevel_set_description.link);
		server->xdg_toplevel_tag_mgr = NULL;
	}
	stonefly_foreign_toplevel_finish(server);
	stonefly_ext_capture_finish(server);

	wl_list_remove(&server->cursor_motion.link);
	wl_list_remove(&server->cursor_motion_absolute.link);
	wl_list_remove(&server->cursor_button.link);
	wl_list_remove(&server->cursor_axis.link);
	wl_list_remove(&server->cursor_frame.link);

	wl_list_remove(&server->new_input.link);
	wl_list_remove(&server->request_cursor.link);
	wl_list_remove(&server->request_set_cursor_shape.link);
	wl_list_remove(&server->pointer_focus_change.link);
	wl_list_remove(&server->request_set_selection.link);
	wl_list_remove(&server->request_set_primary_selection.link);
	if (server->xdg_decoration_mgr != NULL) {
		wl_list_remove(&server->new_xdg_decoration.link);
	}

	wl_list_remove(&server->new_output.link);

	wlr_scene_node_destroy(&server->scene->tree.node);
	wlr_xcursor_manager_destroy(server->cursor_mgr);
	wlr_cursor_destroy(server->cursor);
	wlr_allocator_destroy(server->allocator);
	wlr_renderer_destroy(server->renderer);
	wlr_backend_destroy(server->backend);
	wl_display_destroy(server->wl_display);
}
