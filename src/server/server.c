#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
#include <wlr/types/wlr_presentation_time.h>
#include <wlr/types/wlr_tearing_control_v1.h>
#include <wlr/types/wlr_xdg_output_v1.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <scenefx/render/fx_renderer/fx_renderer.h>
#include <scenefx/types/wlr_scene.h>

#include "dbus/dbus.h"
#include "decor/decor.h"
#include "effect/background_effect.h"
#include "ipc/ipc.h"
#include "layer/layer.h"
#include "log/log.h"
#include "output/output.h"
#include "seat/seat.h"
#include "server/server.h"
#include "session/session.h"
#include "ssd/ssd.h"
#include "toplevel/ext_capture.h"
#include "toplevel/foreign.h"
#include "wm/drag_snap.h"
#include "wm/scratchpad.h"
#include "wm/snap_preview.h"
#include "wm/workspace.h"
#include "xdg/xdg.h"

static void tearing_new_object(struct wl_listener *listener, void *data) {
	(void)listener;
	struct wlr_tearing_control_v1 *control = data;
	SF_LOGD("tearing control", sf_str("event", "new_object"),
		sf_i64("hint", (int64_t)control->current));
}

bool stonefly_server_init(struct stonefly_server *server) {
	stonefly_config_set_defaults(&server->config);

	server->wl_display = wl_display_create();
	server->session = NULL;
	server->backend = wlr_backend_autocreate(wl_display_get_event_loop(server->wl_display),
						 &server->session);
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
	server->wlr_data_control_mgr = wlr_data_control_manager_v1_create(server->wl_display);

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
	server->scene_layout = wlr_scene_attach_output_layout(server->scene, server->output_layout);

	/* Night light: SceneFX applies LUTs on output commit. */
	server->gamma_control_mgr = wlr_gamma_control_manager_v1_create(server->wl_display);
	if (server->gamma_control_mgr != NULL) {
		wlr_scene_set_gamma_control_manager_v1(server->scene, server->gamma_control_mgr);
	}

	/* Noctalia region/fullscreen screenshots + lock desktop capture. */
	wlr_screencopy_manager_v1_create(server->wl_display);

	/*
	 * wp_presentation: SceneFX wlr_scene_surface_create samples feedback once
	 * this global exists. wlroots 0.20 takes (display, backend, version).
	 */
	server->presentation = wlr_presentation_create(server->wl_display, server->backend, 2);

	/* Advertise tearing-control; page-flip tearing not wired this pass. */
	server->tearing_control_mgr = wlr_tearing_control_manager_v1_create(server->wl_display, 1);
	if (server->tearing_control_mgr != NULL) {
		server->tearing_new_object.notify = tearing_new_object;
		wl_signal_add(&server->tearing_control_mgr->events.new_object,
			      &server->tearing_new_object);
	}

	/* Config before seat/scratchpad/output consumers read policy. */
	if (!stonefly_config_load(server)) {
		SF_LOGE("config load failed");
		return false;
	}
	stonefly_decor_apply_blur_params(server);

	stonefly_layer_setup(server);
	/* After layers so layers.lock sits above overlay. */
	stonefly_session_setup(server);
	stonefly_snap_preview_init(server);
	stonefly_drag_snap_reset(&server->drag_snap);
	/* Before outputs so each head can create an ext-workspace group. */
	stonefly_workspace_setup(server);
	stonefly_output_setup(server);
	stonefly_ssd_setup(server);
	stonefly_xdg_setup(server);
	stonefly_foreign_toplevel_setup(server);
	stonefly_background_effect_setup(server);
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
	stonefly_dbus_update_activation_environment();
	if (!stonefly_ipc_start(server)) {
		SF_LOGW("ipc not available");
	}
	if (startup_cmd != NULL) {
		if (fork() == 0) {
			execl("/bin/sh", "/bin/sh", "-c", startup_cmd, (void *)NULL);
			_exit(1);
		}
	}
	/* After -s so Noctalia can claim org.freedesktop.Notifications. */
	stonefly_dbus_notify_config_warn(server);

	SF_LOGI("Running Wayland compositor", sf_str("display", socket));
	wl_display_run(server->wl_display);
	return 0;
}

void stonefly_server_finish(struct stonefly_server *server) {
	/* Drop bus watches before the display/event loop goes away. */
	stonefly_dbus_finish(server);
	stonefly_ipc_stop(server);
	stonefly_ssd_finish(server);
	wl_display_destroy_clients(server->wl_display);
	free(server->config_warn);
	server->config_warn = NULL;
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
	if (server->xdg_toplevel_icon_mgr != NULL) {
		wl_list_remove(&server->xdg_toplevel_set_icon.link);
		server->xdg_toplevel_icon_mgr = NULL;
	}
	stonefly_background_effect_finish(server);
	stonefly_foreign_toplevel_finish(server);
	stonefly_ext_capture_finish(server);
	stonefly_workspace_finish(server);

	wl_list_remove(&server->cursor_motion.link);
	wl_list_remove(&server->cursor_motion_absolute.link);
	wl_list_remove(&server->cursor_button.link);
	wl_list_remove(&server->cursor_axis.link);
	wl_list_remove(&server->cursor_frame.link);
	wl_list_remove(&server->cursor_touch_down.link);
	wl_list_remove(&server->cursor_touch_up.link);
	wl_list_remove(&server->cursor_touch_motion.link);
	wl_list_remove(&server->cursor_touch_cancel.link);
	wl_list_remove(&server->cursor_touch_frame.link);

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
	stonefly_output_finish(server);
	if (server->tearing_control_mgr != NULL) {
		wl_list_remove(&server->tearing_new_object.link);
		server->tearing_control_mgr = NULL;
	}
	server->presentation = NULL;

	wlr_scene_node_destroy(&server->scene->tree.node);
	wlr_xcursor_manager_destroy(server->cursor_mgr);
	wlr_cursor_destroy(server->cursor);
	wlr_allocator_destroy(server->allocator);
	wlr_renderer_destroy(server->renderer);
	wlr_backend_destroy(server->backend);
	wl_display_destroy(server->wl_display);
}
