#include <stdlib.h>
#include <strings.h>
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
#include <wlr/types/wlr_damage_ring.h>
#include <wlr/types/wlr_tearing_control_v1.h>
#include <scenefx/types/wlr_scene.h>
#include <wlr/types/wlr_xcursor_manager.h>

#include "anim/anim.h"
#include "config/config.h"
#include "decor/decor.h"
#include "layer/layer.h"
#include "log/log.h"
#include "output/output.h"
#include "session/session.h"
#include "toplevel/ext_capture.h"
#include "ui/ui.h"
#include "toplevel/toplevel.h"
#include "wm/layout_sm.h"
#include "wm/overview.h"
#include "wm/wm.h"
#include "wm/workspace.h"

static bool output_client_tearing_hint(struct sf_output *output, struct sf_toplevel *tl) {
	struct sf_server *server = output->server;
	if (server->tearing_control_mgr == NULL || tl == NULL) {
		return false;
	}
	struct wlr_surface *surf = sf_toplevel_wlr_surface(tl);
	if (surf == NULL) {
		return false;
	}
	enum wp_tearing_control_v1_presentation_hint hint =
		wlr_tearing_control_manager_v1_surface_hint_from_surface(
			server->tearing_control_mgr, surf);
	return hint == WP_TEARING_CONTROL_V1_PRESENTATION_HINT_ASYNC;
}

/* Eligible fullscreen candidate on this output (prefer focused). */
static struct sf_toplevel *output_tearing_candidate(struct sf_output *output) {
	struct sf_toplevel *focused = sf_focused_toplevel(output->server);
	if (focused != NULL && sf_layout_sm_is_fullscreen(&focused->layout) &&
	    sf_output_for_toplevel(focused) == output && !focused->minimized) {
		return focused;
	}
	struct sf_toplevel *t;
	wl_list_for_each(t, &output->server->toplevels, link) {
		if (!sf_layout_sm_is_fullscreen(&t->layout) || t->minimized) {
			continue;
		}
		if (sf_output_for_toplevel(t) == output) {
			return t;
		}
	}
	return NULL;
}

static bool output_tearing_eligible(struct sf_output *output, struct sf_toplevel *tl) {
	if (tl == NULL || !output->allow_tearing) {
		return false;
	}
	if (output->tearing_regular_pending) {
		return false;
	}
	if (sf_session_locked(output->server) || sf_overview_interactive(output->server) ||
	    output->ws_anim.active) {
		return false;
	}
	/* Screencopy / image-copy hold attach_render_locks; never tear those frames. */
	if (output->wlr_output->attach_render_locks > 0) {
		return false;
	}
	bool hint = output_client_tearing_hint(output, tl);
	if (tl->rule_tearing_set) {
		return tl->rule_tearing;
	}
	return hint;
}

/*
 * build_state + optional tearing_page_flip + commit. Returns whether a buffer
 * was submitted. track_tearing updates recovery/present bookkeeping.
 */
static bool output_commit_scene(struct sf_output *output, struct wlr_scene_output *scene,
				bool allow_tearing_attempt, bool track_lock_frame) {
	struct wlr_output_state state;
	wlr_output_state_init(&state);
	if (!wlr_scene_output_build_state(scene, &state, NULL)) {
		wlr_output_state_finish(&state);
		return false;
	}

	bool has_buffer =
		(state.committed & (uint32_t)WLR_OUTPUT_STATE_BUFFER) != 0 && state.buffer != NULL;
	bool tear = false;
	if (allow_tearing_attempt && has_buffer) {
		struct sf_toplevel *cand = output_tearing_candidate(output);
		tear = output_tearing_eligible(output, cand);
		if (tear) {
			state.tearing_page_flip = true;
			if (!wlr_output_test_state(output->wlr_output, &state)) {
				state.tearing_page_flip = false;
				tear = false;
				output->tearing_regular_pending = true;
				SF_LOGD("tearing", sf_str("fallback", "backend rejected async"));
			}
		}
	}

	bool ok = wlr_output_commit_state(output->wlr_output, &state);
	if (has_buffer) {
		if (tear && !ok) {
			output->tearing_regular_pending = true;
		} else if (ok && !tear && output->tearing_regular_pending) {
			output->tearing_regular_pending = false;
		}
		if (ok) {
			output->tearing_last_commit = tear;
			output->tearing_tracked_seq = output->wlr_output->commit_seq;
			if (track_lock_frame) {
				output->lock_frame = (struct sf_lock_frame){
					.seq = output->wlr_output->commit_seq,
					.submitted = true,
				};
			}
		}
	}
	wlr_output_state_finish(&state);
	return ok && has_buffer;
}

static void output_present(struct wl_listener *listener, void *data) {
	struct sf_output *output = wl_container_of(listener, output, present);
	const struct wlr_output_event_present *event = data;
	sf_session_output_present(output, event->commit_seq, event->presented);

	if (output->tearing_last_commit && event->commit_seq == output->tearing_tracked_seq &&
	    !event->presented) {
		/* Failed async present: force a regular flip next frame. */
		output->tearing_regular_pending = true;
		output->tearing_last_commit = false;
		struct wlr_scene_output *scene =
			wlr_scene_get_scene_output(output->server->scene, output->wlr_output);
		if (scene != NULL) {
			wlr_damage_ring_add_whole(&scene->damage_ring);
		}
		wlr_output_schedule_frame(output->wlr_output);
		SF_LOGD("tearing", sf_str("fallback", "async present failed"));
	}
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

	bool lock_await = output->server->lock.awaiting_present && !output->lock_frame.submitted &&
			  !output->lock_frame.presented;
	/*
	 * Dual-pass capture blanking on stock SceneFX: screencopy copies the
	 * committed buffer. Commit once with blocked trees hidden (clients
	 * capture underneath), restore, then commit the full scene for display.
	 * Both commits run in this handler so the last buffer is what scans out.
	 */
	bool capture_filter = !lock_await && output->wlr_output->attach_render_locks > 0 &&
			      sf_capture_has_blocked(output->server);
	if (capture_filter) {
		sf_capture_set_blocked_hidden(output->server, true);
		(void)output_commit_scene(output, scene_output, false, false);
		sf_capture_set_blocked_hidden(output->server, false);
		/* New swapchain buffer after the capture pass needs a full redraw. */
		wlr_damage_ring_add_whole(&scene_output->damage_ring);
	}

	if (lock_await) {
		if (!output_commit_scene(output, scene_output, false, true)) {
			wlr_output_schedule_frame(output->wlr_output);
		}
	} else {
		(void)output_commit_scene(output, scene_output, !capture_filter, false);
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
		struct wlr_output_layout_output *lo =
			wlr_output_layout_get(server->output_layout, wlr_output);
		head->state.enabled = lo != NULL;
		if (lo != NULL) {
			head->state.x = lo->x;
			head->state.y = lo->y;
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

/* A logical configuration includes sleeping heads. Permit layout changes but
 * don't wake them or silently accept backend properties we didn't apply. */
static bool output_mask_sleeping(struct wlr_backend_output_state *heads, size_t count) {
	for (size_t i = 0; i < count; i++) {
		struct wlr_output *wlr = heads[i].output;
		struct sf_output *output = wlr->data;
		struct wlr_output_state *state = &heads[i].base;
		if (output == NULL || !output->dpms_off || !state->enabled) {
			continue;
		}
		bool mode_matches = state->mode_type == WLR_OUTPUT_STATE_MODE_FIXED
					    ? state->mode == wlr->current_mode
					    : wlr->current_mode == NULL &&
						      state->custom_mode.width == wlr->width &&
						      state->custom_mode.height == wlr->height &&
						      state->custom_mode.refresh == wlr->refresh;
		if (!mode_matches ||
		    wl_fixed_from_double(state->scale) != wl_fixed_from_double(wlr->scale) ||
		    state->transform != wlr->transform ||
		    state->adaptive_sync_enabled !=
			    (wlr->adaptive_sync_status == WLR_OUTPUT_ADAPTIVE_SYNC_ENABLED)) {
			return false;
		}
		wlr_output_state_finish(state);
		wlr_output_state_init(state);
		wlr_output_state_set_enabled(state, false);
	}
	return true;
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
	bool ok = output_mask_sleeping(states, count) &&
		  wlr_output_swapchain_manager_prepare(&mgr, states, count);
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
		wl_list_for_each(output, &server->outputs, link) {
			if (wlr_output_layout_get(server->output_layout, output->wlr_output) ==
			    NULL) {
				output->dpms_off = false;
			} else {
				sf_workspace_adopt_parked(output);
			}
		}
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
	/* Nested Wayland/X11 window resize — keep splash centered on the new box. */
	sf_cheatsheet_on_output_change(output->server);
	sf_output_mgr_send_config(output->server);
}

static void output_destroy(struct wl_listener *listener, void *data) {
	(void)data;
	struct sf_output *output = wl_container_of(listener, output, destroy);
	struct sf_server *server = output->server;
	struct sf_layer_surface *layer, *tmp;

	bool live_strip = !wl_list_empty(&output->workspaces);
	if (live_strip) {
		sf_overview_force_close(server);
	}
	for (int i = 0; i <= ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY; i++) {
		wl_list_for_each_safe(layer, tmp, &output->layers[i], link) {
			wlr_layer_surface_v1_destroy(layer->layer_surface);
		}
	}

	sf_workspace_output_finish(output);
	if (live_strip) {
		wlr_output_layout_remove(server->output_layout, output->wlr_output);
	}
	output->wlr_output->data = NULL;
	wl_list_remove(&output->present.link);
	wl_list_remove(&output->frame.link);
	wl_list_remove(&output->request_state.link);
	wl_list_remove(&output->destroy.link);
	wl_list_remove(&output->link);
	free(output);

	/* At shutdown the strips were already finished before the scene. */
	if (live_strip) {
		struct sf_output *other;
		wl_list_for_each(other, &server->outputs, link) {
			if (wlr_output_layout_get(server->output_layout, other->wlr_output) !=
			    NULL) {
				sf_layer_arrange_output(other);
				sf_workspace_adopt_parked(other);
			}
		}
	}
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
	sf_output_apply_config(output);
	sf_layer_arrange_output(output);
	sf_workspace_adopt_parked(output);
	sf_decor_update_blur_size(server);
	sf_output_mgr_send_config(server);
}

void sf_output_apply_config(struct sf_output *output) {
	if (output == NULL || output->wlr_output == NULL || output->server == NULL) {
		return;
	}
	const struct sf_config *cfg = sf_cfg(output->server);
	enum sf_arrange arrange = SF_ARRANGE_UNSET;
	bool allow_tearing = false;
	const char *name = output->wlr_output->name;
	if (cfg != NULL && name != NULL) {
		for (size_t i = 0; i < cfg->n_output_rules; i++) {
			const struct sf_output_rule *rule = &cfg->output_rules[i];
			if (rule->match != NULL && strcasecmp(rule->match, name) == 0) {
				arrange = rule->arrange;
				if (rule->tearing_set) {
					allow_tearing = rule->tearing;
				}
			}
		}
	}
	output->arrange = arrange;
	if (output->allow_tearing != allow_tearing) {
		output->tearing_regular_pending = false;
		wlr_output_schedule_frame(output->wlr_output);
	}
	output->allow_tearing = allow_tearing;
}

void sf_output_apply_all(struct sf_server *server) {
	if (server == NULL) {
		return;
	}
	struct sf_output *output;
	wl_list_for_each(output, &server->outputs, link) {
		sf_output_apply_config(output);
	}
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
