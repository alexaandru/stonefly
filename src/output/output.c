#include <stdlib.h>
#include <time.h>
#include <wlr/backend.h>
#include <wlr/render/pass.h>
#include <wlr/render/swapchain.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_buffer.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_output_management_v1.h>
#include <wlr/types/wlr_output_swapchain_manager.h>
#include <scenefx/types/wlr_scene.h>
#include <wlr/types/wlr_xcursor_manager.h>

#include "anim/anim.h"
#include "config/config.h"
#include "decor/decor.h"
#include "layer/layer.h"
#include "log/log.h"
#include "output/output.h"
#include "session/session.h"
#include "wm/workspace.h"

static void output_present(struct wl_listener *listener, void *data) {
	struct sf_output *output = wl_container_of(listener, output, present);
	const struct wlr_output_event_present *event = data;
	sf_session_output_present(output, event->commit_seq, event->presented);
}

static void output_commit_lock_frame(struct sf_output *output, struct wlr_scene_output *scene) {
	/* A successful scene commit can be a no-op. Only track a submitted buffer. */
	struct wlr_output_state state;
	wlr_output_state_init(&state);
	if (wlr_scene_output_build_state(scene, &state, NULL) && state.buffer != NULL &&
	    wlr_output_commit_state(output->wlr_output, &state)) {
		output->lock_frame = (struct sf_lock_frame){
			.seq = output->wlr_output->commit_seq,
			.submitted = true,
		};
	} else {
		wlr_output_schedule_frame(output->wlr_output);
	}
	wlr_output_state_finish(&state);
}

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

	/* Rendering is damage driven. Frame callbacks pace clients, not proof of a flip. */
	if (sf_session_locked(output->server) && wlr_box_empty(&output->lock_box)) {
		return;
	}
	if (output->server->lock.awaiting_present && !output->lock_frame.submitted &&
	    !output->lock_frame.presented) {
		output_commit_lock_frame(output, scene_output);
	} else {
		wlr_scene_output_commit(scene_output, NULL);
	}
	wlr_scene_output_send_frame_done(scene_output, &now);
}

static void output_rearrange_all(struct sf_server *server) {
	struct sf_output *output;
	wl_list_for_each(output, &server->outputs, link) {
		sf_layer_arrange_output(output);
	}
	sf_decor_update_blur_size(server);
}

static struct wlr_output_configuration_v1 *output_snapshot(struct sf_server *server) {
	struct wlr_output_configuration_v1 *config = wlr_output_configuration_v1_create();
	if (config == NULL) {
		SF_LOGW("output manager", sf_str("err", "config create failed"));
		return NULL;
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
			return NULL;
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

	return config;
}

void sf_output_mgr_send_config(struct sf_server *server) {
	if (server->output_mgr != NULL) {
		struct wlr_output_configuration_v1 *config = output_snapshot(server);
		if (config != NULL) {
			wlr_output_manager_v1_set_configuration(server->output_mgr, config);
		}
	}
}

static bool output_apply_layout(struct sf_server *server, struct wlr_output_configuration_v1 *cfg) {
	struct wlr_output_configuration_head_v1 *head;
	wl_list_for_each(head, &cfg->heads, link) {
		struct wlr_output *output = head->state.output;
		if (!head->state.enabled) {
			wlr_output_layout_remove(server->output_layout, output);
			continue;
		}
		struct wlr_output_layout_output *lo = wlr_output_layout_add(
			server->output_layout, output, head->state.x, head->state.y);
		struct wlr_scene_output *so = wlr_scene_get_scene_output(server->scene, output);
		if (lo == NULL || so == NULL) {
			return false;
		}
		wlr_scene_output_layout_add_output(server->scene_layout, lo, so);
	}
	return true;
}

/* A new mode can extend past the current lock blank. Its first buffer must
 * already be safe; the regular scene repaints once the new layout is known. */
static bool output_build_blank(struct sf_server *server, struct wlr_swapchain *swapchain,
			       struct wlr_output_state *state) {
	struct wlr_buffer *buffer = wlr_swapchain_acquire(swapchain);
	if (buffer == NULL) {
		return false;
	}
	struct wlr_render_pass *pass =
		wlr_renderer_begin_buffer_pass(server->renderer, buffer, NULL);
	bool ok = false;
	if (pass != NULL) {
		wlr_render_pass_add_rect(pass, &(struct wlr_render_rect_options){
						       .color = {0, 0, 0, 1},
						       .blend_mode = WLR_RENDER_BLEND_MODE_NONE,
					       });
		ok = wlr_render_pass_submit(pass);
		if (ok) {
			wlr_output_state_set_buffer(state, buffer);
		}
	}
	wlr_buffer_unlock(buffer);
	return ok;
}

static bool output_configure_heads(struct sf_server *server,
				   struct wlr_output_configuration_v1 *cfg, bool test,
				   bool *attempted) {
	size_t count = 0;
	struct wlr_backend_output_state *states =
		wlr_output_configuration_v1_build_state(cfg, &count);
	if (states == NULL) {
		return false;
	}
	struct wlr_output_swapchain_manager mgr;
	wlr_output_swapchain_manager_init(&mgr, server->backend);
	bool ok = wlr_output_swapchain_manager_prepare(&mgr, states, count);
	if (!test && ok) {
		for (size_t i = 0; i < count && ok; i++) {
			struct wlr_backend_output_state *head = &states[i];
			if (!head->base.enabled) {
				continue;
			}
			struct wlr_scene_output *scene =
				wlr_scene_get_scene_output(server->scene, head->output);
			struct wlr_scene_output_state_options opts = {
				.swapchain = wlr_output_swapchain_manager_get_swapchain(
					&mgr, head->output),
			};
			if (scene == NULL || opts.swapchain == NULL) {
				ok = false;
			} else if (sf_session_locked(server) ||
				   wlr_output_layout_get(server->output_layout, head->output) ==
					   NULL) {
				ok = output_build_blank(server, opts.swapchain, &head->base);
			} else {
				ok = wlr_scene_output_build_state(scene, &head->base, &opts);
			}
		}
		if (ok) {
			*attempted = true;
			ok = wlr_backend_commit(server->backend, states, count);
		}
		if (ok) {
			wlr_output_swapchain_manager_apply(&mgr);
		}
	}
	for (size_t i = 0; i < count; i++) {
		wlr_output_state_finish(&states[i].base);
	}
	free(states);
	wlr_output_swapchain_manager_finish(&mgr);
	return ok;
}

static void output_mgr_apply_or_test(struct sf_server *server,
				     struct wlr_output_configuration_v1 *config, bool test) {
	bool attempted = false;
	struct wlr_output_configuration_v1 *prev = test ? NULL : output_snapshot(server);
	bool ok =
		(test || prev != NULL) && output_configure_heads(server, config, test, &attempted);
	if (ok && !test) {
		ok = output_apply_layout(server, config);
	}
	if (!ok && attempted) {
		bool restored = output_configure_heads(server, prev, false, &attempted);
		if (!output_apply_layout(server, prev) || !restored) {
			SF_LOGE("output configuration rollback incomplete");
		}
	}
	if (prev != NULL) {
		wlr_output_configuration_v1_destroy(prev);
	}
	if (ok) {
		wlr_output_configuration_v1_send_succeeded(config);
	} else {
		wlr_output_configuration_v1_send_failed(config);
	}
	wlr_output_configuration_v1_destroy(config);
	if (!test) {
		struct sf_output *output;
		wl_list_for_each(output, &server->outputs, link) {
			wlr_xcursor_manager_load(server->cursor_mgr, output->wlr_output->scale);
			struct wlr_scene_output *scene =
				wlr_scene_get_scene_output(server->scene, output->wlr_output);
			if (scene != NULL && output->wlr_output->enabled) {
				wlr_damage_ring_add_whole(&scene->damage_ring);
				wlr_output_schedule_frame(output->wlr_output);
			}
		}
		output_rearrange_all(server);
		sf_session_sync_outputs(server);
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
	if (!wlr_output_commit_state(output->wlr_output, event->state)) {
		return;
	}
	sf_session_sync_outputs(output->server);
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
	wl_list_remove(&output->present.link);
	wl_list_remove(&output->frame.link);
	wl_list_remove(&output->request_state.link);
	wl_list_remove(&output->destroy.link);
	wl_list_remove(&output->link);
	free(output);

	sf_session_sync_outputs(server);
	sf_output_mgr_send_config(server);
}

static void server_new_output(struct wl_listener *listener, void *data) {
	/* This event is raised by the backend when a new output (aka a display or
	 * monitor) becomes available. */
	struct sf_server *server = wl_container_of(listener, server, new_output);
	struct wlr_output *wlr_output = data;

	/* Configures the output created by the backend to use our allocator
	 * and our renderer. Must be done once, before committing the output */
	if (!wlr_output_init_render(wlr_output, server->allocator, server->renderer)) {
		SF_LOGW("output render initialization failed", sf_str("output", wlr_output->name));
		return;
	}
	struct sf_output *output = calloc(1, sizeof(*output));
	if (output == NULL) {
		return;
	}
	struct wlr_scene_output *scene_output = wlr_scene_output_create(server->scene, wlr_output);
	if (scene_output == NULL) {
		free(output);
		return;
	}
	struct wlr_output_layout_output *l_output =
		wlr_output_layout_add_auto(server->output_layout, wlr_output);
	if (l_output == NULL) {
		wlr_scene_output_destroy(scene_output);
		free(output);
		return;
	}
	wlr_scene_output_layout_add_output(server->scene_layout, l_output, scene_output);

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
	bool ok = wlr_output_commit_state(wlr_output, &state);
	wlr_output_state_finish(&state);
	if (!ok) {
		wlr_output_layout_remove(server->output_layout, wlr_output);
		wlr_scene_output_destroy(scene_output);
		free(output);
		SF_LOGW("initial output commit failed", sf_str("output", wlr_output->name));
		return;
	}

	wlr_xcursor_manager_load(server->cursor_mgr, scale);

	/* Publish the head only after its scene and initial state are ready. */
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

	output->present.notify = output_present;
	wl_signal_add(&wlr_output->events.present, &output->present);
	sf_session_sync_outputs(server);

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
