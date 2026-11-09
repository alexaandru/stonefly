#include <stdlib.h>
#include <time.h>
#include <wlr/backend.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_output_management_v1.h>
#include <scenefx/types/wlr_scene.h>
#include <wlr/types/wlr_xcursor_manager.h>

#include "anim/anim.h"
#include "config/config.h"
#include "decor/decor.h"
#include "layer/layer.h"
#include "log/log.h"
#include "output/output.h"
#include "wm/workspace.h"

static void output_frame(struct wl_listener *listener, void *data) {
	(void)data;
	/* This function is called every time an output is ready to display a frame,
	 * generally at the output's refresh rate (e.g. 60Hz). */
	struct sf_output *output = wl_container_of(listener, output, frame);
	struct wlr_scene *scene = output->server->scene;

	struct wlr_scene_output *scene_output =
		wlr_scene_get_scene_output(scene, output->wlr_output);
	/* VT switch / session deactivate can leave a frame with no scene output. */
	if (scene_output == NULL) {
		return;
	}

	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	/* Advance slides before commit so this frame shows the new pose. */
	sf_anim_tick(output->server, &now);

	/* Render the scene if needed and commit the output */
	wlr_scene_output_commit(scene_output, NULL);

	wlr_scene_output_send_frame_done(scene_output, &now);
}

static void output_rearrange_all(struct sf_server *server) {
	struct sf_output *output;
	wl_list_for_each(output, &server->outputs, link) {
		sf_layer_arrange_output(output);
	}
	sf_decor_update_blur_size(server);
}

void sf_output_mgr_send_config(struct sf_server *server) {
	if (server->output_mgr == NULL) {
		return;
	}

	struct wlr_output_configuration_v1 *config = wlr_output_configuration_v1_create();
	if (config == NULL) {
		SF_LOGW("output manager", sf_str("err", "config create failed"));
		return;
	}

	struct sf_output *output;
	wl_list_for_each(output, &server->outputs, link) {
		struct wlr_output *wlr_output = output->wlr_output;
		struct wlr_output_configuration_head_v1 *head =
			wlr_output_configuration_head_v1_create(config, wlr_output);
		if (head == NULL) {
			SF_LOGW("output manager", sf_str("err", "head create failed"),
				sf_str("output", wlr_output->name));
			wlr_output_configuration_v1_destroy(config);
			return;
		}
		if (wlr_output->enabled) {
			struct wlr_box box;
			wlr_output_layout_get_box(server->output_layout, wlr_output, &box);
			if (!wlr_box_empty(&box)) {
				head->state.x = box.x;
				head->state.y = box.y;
			}
		}
	}

	wlr_output_manager_v1_set_configuration(server->output_mgr, config);
}

static void output_mgr_apply_or_test(struct sf_server *server,
				     struct wlr_output_configuration_v1 *config, bool test) {
	bool ok = true;
	struct wlr_output_configuration_head_v1 *head;

	wl_list_for_each(head, &config->heads, link) {
		struct wlr_output *wlr_output = head->state.output;
		struct wlr_output_state state;
		wlr_output_state_init(&state);
		wlr_output_head_v1_state_apply(&head->state, &state);

		if (test) {
			ok = wlr_output_test_state(wlr_output, &state);
		} else {
			ok = wlr_output_commit_state(wlr_output, &state);
		}
		wlr_output_state_finish(&state);
		if (!ok) {
			SF_LOGW("output manager", sf_str("op", test ? "test" : "apply"),
				sf_str("err", "state failed"), sf_str("output", wlr_output->name));
			break;
		}

		if (test) {
			continue;
		}

		/* Position is not applied by head_state_apply. */
		if (head->state.enabled) {
			struct wlr_output_layout_output *l_output = wlr_output_layout_add(
				server->output_layout, wlr_output, head->state.x, head->state.y);
			struct wlr_scene_output *scene_output =
				wlr_scene_get_scene_output(server->scene, wlr_output);
			if (l_output != NULL && scene_output != NULL) {
				/* Re-link after layout_remove destroyed the prior link. */
				wlr_scene_output_layout_add_output(server->scene_layout, l_output,
								   scene_output);
			}
		} else {
			wlr_output_layout_remove(server->output_layout, wlr_output);
		}
	}

	if (ok) {
		wlr_output_configuration_v1_send_succeeded(config);
	} else {
		wlr_output_configuration_v1_send_failed(config);
	}
	wlr_output_configuration_v1_destroy(config);

	if (!test && ok) {
		struct sf_output *output;
		wl_list_for_each(output, &server->outputs, link) {
			wlr_xcursor_manager_load(server->cursor_mgr, output->wlr_output->scale);
		}
		output_rearrange_all(server);
		sf_output_mgr_send_config(server);
	}
}

static void output_mgr_apply(struct wl_listener *listener, void *data) {
	struct sf_server *server = wl_container_of(listener, server, output_mgr_apply);
	output_mgr_apply_or_test(server, data, false);
}

static void output_mgr_test(struct wl_listener *listener, void *data) {
	struct sf_server *server = wl_container_of(listener, server, output_mgr_test);
	output_mgr_apply_or_test(server, data, true);
}

static void output_request_state(struct wl_listener *listener, void *data) {
	/* This function is called when the backend requests a new state for
	 * the output. For example, Wayland and X11 backends request a new mode
	 * when the output window is resized. */
	struct sf_output *output = wl_container_of(listener, output, request_state);
	const struct wlr_output_event_request_state *event = data;
	wlr_output_commit_state(output->wlr_output, event->state);
	sf_layer_arrange_output(output);
	sf_decor_update_blur_size(output->server);
	sf_output_mgr_send_config(output->server);
}

static void output_destroy(struct wl_listener *listener, void *data) {
	(void)data;
	struct sf_output *output = wl_container_of(listener, output, destroy);
	struct sf_server *server = output->server;
	struct sf_layer_surface *layer, *tmp;

	for (int i = 0; i <= ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY; i++) {
		wl_list_for_each_safe(layer, tmp, &output->layers[i], link) {
			wlr_layer_surface_v1_destroy(layer->layer_surface);
		}
	}

	sf_workspace_output_finish(output);
	output->wlr_output->data = NULL;
	wl_list_remove(&output->frame.link);
	wl_list_remove(&output->request_state.link);
	wl_list_remove(&output->destroy.link);
	wl_list_remove(&output->link);
	free(output);

	sf_output_mgr_send_config(server);
}

static void server_new_output(struct wl_listener *listener, void *data) {
	/* This event is raised by the backend when a new output (aka a display or
	 * monitor) becomes available. */
	struct sf_server *server = wl_container_of(listener, server, new_output);
	struct wlr_output *wlr_output = data;

	/* Configures the output created by the backend to use our allocator
	 * and our renderer. Must be done once, before committing the output */
	wlr_output_init_render(wlr_output, server->allocator, server->renderer);

	/* The output may be disabled, switch it on. */
	float scale = sf_output_scale_factor(server);
	struct wlr_output_state state;
	wlr_output_state_init(&state);
	wlr_output_state_set_enabled(&state, true);
	wlr_output_state_set_scale(&state, scale);

	/* Some backends don't have modes. DRM+KMS does, and we need to set a mode
	 * before we can use the output. The mode is a tuple of (width, height,
	 * refresh rate), and each monitor supports only a specific set of modes. We
	 * just pick the monitor's preferred mode, a more sophisticated compositor
	 * would let the user configure it. */
	struct wlr_output_mode *mode = wlr_output_preferred_mode(wlr_output);
	if (mode != NULL) {
		wlr_output_state_set_mode(&state, mode);
	}

	/* Atomically applies the new output state. */
	wlr_output_commit_state(wlr_output, &state);
	wlr_output_state_finish(&state);

	wlr_xcursor_manager_load(server->cursor_mgr, scale);

	/* Allocates and configures our state for this output */
	struct sf_output *output = calloc(1, sizeof(*output));
	output->wlr_output = wlr_output;
	output->server = server;
	wlr_output->data = output;
	for (int i = 0; i <= ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY; i++) {
		wl_list_init(&output->layers[i]);
	}

	/* Sets up a listener for the frame event. */
	output->frame.notify = output_frame;
	wl_signal_add(&wlr_output->events.frame, &output->frame);

	/* Sets up a listener for the state request event. */
	output->request_state.notify = output_request_state;
	wl_signal_add(&wlr_output->events.request_state, &output->request_state);

	/* Sets up a listener for the destroy event. */
	output->destroy.notify = output_destroy;
	wl_signal_add(&wlr_output->events.destroy, &output->destroy);

	wl_list_insert(&server->outputs, &output->link);

	/* Adds this to the output layout. The add_auto function arranges outputs
	 * from left-to-right in the order they appear. A more sophisticated
	 * compositor would let the user configure the arrangement of outputs in the
	 * layout.
	 *
	 * The output layout utility automatically adds a wl_output global to the
	 * display, which Wayland clients can see to find out information about the
	 * output (such as DPI, scale factor, manufacturer, etc).
	 */
	struct wlr_output_layout_output *l_output =
		wlr_output_layout_add_auto(server->output_layout, wlr_output);
	struct wlr_scene_output *scene_output = wlr_scene_output_create(server->scene, wlr_output);
	wlr_scene_output_layout_add_output(server->scene_layout, l_output, scene_output);

	sf_workspace_output_init(output);
	sf_layer_arrange_output(output);
	sf_decor_update_blur_size(server);
	sf_output_mgr_send_config(server);
}

float sf_output_scale_factor(struct sf_server *server) {
	/* Nested Wayland / headless: no DRM session — parent already scaled. */
	if (server->session == NULL) {
		return 1.0f;
	}
	return sf_cfg(server)->output_scale;
}

void sf_output_setup(struct sf_server *server) {
	wl_list_init(&server->outputs);
	server->new_output.notify = server_new_output;
	wl_signal_add(&server->backend->events.new_output, &server->new_output);

	server->output_mgr = wlr_output_manager_v1_create(server->wl_display);
	if (server->output_mgr == NULL) {
		SF_LOGW("output manager", sf_str("err", "create failed"));
		return;
	}
	server->output_mgr_apply.notify = output_mgr_apply;
	wl_signal_add(&server->output_mgr->events.apply, &server->output_mgr_apply);
	server->output_mgr_test.notify = output_mgr_test;
	wl_signal_add(&server->output_mgr->events.test, &server->output_mgr_test);
}

void sf_output_finish(struct sf_server *server) {
	if (server->output_mgr != NULL) {
		wl_list_remove(&server->output_mgr_apply.link);
		wl_list_remove(&server->output_mgr_test.link);
		server->output_mgr = NULL;
	}
}
