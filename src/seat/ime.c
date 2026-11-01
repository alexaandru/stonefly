#include "seat/ime.h"

#include <stdlib.h>
#include <wlr/types/wlr_input_method_v2.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_text_input_v3.h>
#include <wlr/types/wlr_virtual_keyboard_v1.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/box.h>

#include "layer/layer.h"
#include "log/log.h"
#include "server/server.h"

#define SF_SAME_CLIENT(a, b)                                                                       \
	(wl_resource_get_client((a)->resource) == wl_resource_get_client((b)->resource))

struct sf_ime_text_input {
	struct wl_list link;
	struct sf_server *server;
	struct wlr_text_input_v3 *input;
	struct wl_listener enable;
	struct wl_listener commit;
	struct wl_listener disable;
	struct wl_listener destroy;
};

struct sf_ime_popup {
	struct wl_list link;
	struct sf_server *server;
	struct wlr_input_popup_surface_v2 *popup;
	struct wlr_scene_tree *tree;
	struct wl_listener destroy;
	struct wl_listener surface_commit;
};

static void listener_clear(struct wl_listener *listener) {
	if (listener->link.next == NULL) {
		return;
	}
	wl_list_remove(&listener->link);
	listener->link.next = NULL;
	listener->link.prev = NULL;
}

static bool ime_owns_keyboard(struct sf_server *server, struct wlr_keyboard *keyboard) {
	struct wlr_input_method_v2 *im = server->ime.input_method;
	if (im == NULL || keyboard == NULL) {
		return false;
	}
	struct wlr_virtual_keyboard_v1 *vk = wlr_input_device_get_virtual_keyboard(&keyboard->base);
	return vk != NULL && SF_SAME_CLIENT(vk, im);
}

static struct wlr_input_method_keyboard_grab_v2 *
ime_grab_for_keyboard(struct sf_server *server, struct wlr_keyboard *keyboard) {
	struct wlr_input_method_v2 *im = server->ime.input_method;
	if (im == NULL || im->keyboard_grab == NULL || keyboard == NULL) {
		return NULL;
	}
	if (ime_owns_keyboard(server, keyboard)) {
		return NULL;
	}
	return im->keyboard_grab;
}

bool sf_ime_grab_forward_key(struct sf_server *server, struct wlr_keyboard *keyboard,
			     struct wlr_keyboard_key_event *event) {
	struct wlr_input_method_keyboard_grab_v2 *grab = ime_grab_for_keyboard(server, keyboard);
	if (grab == NULL) {
		return false;
	}
	wlr_input_method_keyboard_grab_v2_set_keyboard(grab, keyboard);
	wlr_input_method_keyboard_grab_v2_send_key(grab, event->time_msec, event->keycode,
						   event->state);
	return true;
}

bool sf_ime_grab_forward_modifiers(struct sf_server *server, struct wlr_keyboard *keyboard) {
	struct wlr_input_method_keyboard_grab_v2 *grab = ime_grab_for_keyboard(server, keyboard);
	if (grab == NULL) {
		return false;
	}
	wlr_input_method_keyboard_grab_v2_set_keyboard(grab, keyboard);
	wlr_input_method_keyboard_grab_v2_send_modifiers(grab, &keyboard->modifiers);
	return true;
}

void sf_ime_canonicalize_owned_keyboard(struct sf_server *server, struct wlr_keyboard *keyboard) {
	struct wlr_input_method_v2 *im = server->ime.input_method;
	if (!ime_owns_keyboard(server, keyboard) || im->keyboard_grab == NULL) {
		return;
	}
	struct wlr_keyboard *grabbed = im->keyboard_grab->keyboard;
	if (grabbed == NULL || grabbed->keymap == NULL || keyboard->keymap == NULL) {
		return;
	}
	if (grabbed->keymap == keyboard->keymap) {
		return;
	}
	if (!wlr_keyboard_keymaps_match(grabbed->keymap, keyboard->keymap)) {
		return;
	}
	/* IME often mirrors the grab map through its virtual keyboard; share the
	 * object so clients do not see a spurious keymap change. */
	wlr_keyboard_set_keymap(keyboard, grabbed->keymap);
}

static void update_popup_position(struct sf_ime_popup *popup);
static void update_popup_positions(struct sf_server *server);
static void send_state_to_input_method(struct sf_server *server);
static void update_text_input_focus(struct sf_server *server);
static void update_active_text_input(struct sf_server *server);

static void send_state_to_input_method(struct sf_server *server) {
	struct wlr_input_method_v2 *im = server->ime.input_method;
	struct wlr_text_input_v3 *ti = server->ime.active_text_input;
	if (im == NULL || ti == NULL) {
		return;
	}
	const struct wlr_text_input_v3_state *cur = &ti->current;
	/* Chromium may set surrounding/content_type after the enable commit;
	 * wlroots freezes active_features at enable, so read current.features. */
	if ((cur->features & (uint32_t)WLR_TEXT_INPUT_V3_FEATURE_SURROUNDING_TEXT) != 0) {
		wlr_input_method_v2_send_surrounding_text(im, cur->surrounding.text,
							  cur->surrounding.cursor,
							  cur->surrounding.anchor);
	}
	wlr_input_method_v2_send_text_change_cause(im, cur->text_change_cause);
	if ((cur->features & (uint32_t)WLR_TEXT_INPUT_V3_FEATURE_CONTENT_TYPE) != 0) {
		wlr_input_method_v2_send_content_type(im, cur->content_type.hint,
						      cur->content_type.purpose);
	}
	wlr_input_method_v2_send_done(im);
}

static void update_text_input_focus(struct sf_server *server) {
	struct sf_ime_text_input *ti;
	wl_list_for_each(ti, &server->ime.text_inputs, link) {
		struct wlr_surface *target = NULL;
		if (server->ime.input_method != NULL && server->ime.focused_surface != NULL &&
		    SF_SAME_CLIENT(ti->input, server->ime.focused_surface)) {
			target = server->ime.focused_surface;
		}
		if (ti->input->focused_surface == target) {
			continue;
		}
		if (ti->input->focused_surface != NULL) {
			wlr_text_input_v3_send_leave(ti->input);
		}
		if (target != NULL) {
			wlr_text_input_v3_send_enter(ti->input, target);
		}
	}
}

static void update_active_text_input(struct sf_server *server) {
	struct wlr_text_input_v3 *candidate = NULL;
	if (server->ime.input_method != NULL) {
		struct sf_ime_text_input *ti;
		wl_list_for_each(ti, &server->ime.text_inputs, link) {
			if (ti->input->focused_surface != NULL && ti->input->current_enabled) {
				candidate = ti->input;
				break;
			}
		}
	}
	if (candidate == server->ime.active_text_input) {
		return;
	}
	if (server->ime.input_method != NULL) {
		if (candidate != NULL) {
			wlr_input_method_v2_send_activate(server->ime.input_method);
		} else {
			wlr_input_method_v2_send_deactivate(server->ime.input_method);
		}
		wlr_input_method_v2_send_done(server->ime.input_method);
	}
	server->ime.active_text_input = candidate;
	if (candidate != NULL) {
		send_state_to_input_method(server);
		update_popup_positions(server);
	}
}

static bool cursor_rect_to_layout(struct sf_server *server, struct wlr_box *cursor_rect) {
	struct wlr_surface *focused = server->ime.focused_surface;
	if (focused == NULL) {
		return false;
	}
	struct wlr_surface *root = wlr_surface_get_root_surface(focused);
	if (root == NULL) {
		return false;
	}

	struct wlr_xdg_surface *xdg = wlr_xdg_surface_try_from_wlr_surface(root);
	if (xdg != NULL && xdg->data != NULL) {
		struct wlr_scene_tree *tree = xdg->data;
		int lx = 0, ly = 0;
		wlr_scene_node_coords(&tree->node, &lx, &ly);
		cursor_rect->x += lx - xdg->geometry.x;
		cursor_rect->y += ly - xdg->geometry.y;
		return true;
	}

	struct wlr_layer_surface_v1 *ls = wlr_layer_surface_v1_try_from_wlr_surface(root);
	if (ls != NULL && ls->data != NULL) {
		struct sf_layer_surface *layer = ls->data;
		if (layer->scene == NULL) {
			return false;
		}
		int lx = 0, ly = 0;
		wlr_scene_node_coords(&layer->scene->node, &lx, &ly);
		cursor_rect->x += lx;
		cursor_rect->y += ly;
		return true;
	}
	return false;
}

static void update_popup_position(struct sf_ime_popup *popup) {
	struct sf_server *server = popup->server;
	struct wlr_text_input_v3 *ti = server->ime.active_text_input;
	if (ti == NULL || server->ime.focused_surface == NULL || !popup->popup->surface->mapped) {
		return;
	}

	struct wlr_box cursor_rect = {0};
	if ((ti->current.features & (uint32_t)WLR_TEXT_INPUT_V3_FEATURE_CURSOR_RECTANGLE) != 0) {
		cursor_rect = ti->current.cursor_rectangle;
	}
	if (!cursor_rect_to_layout(server, &cursor_rect)) {
		return;
	}

	struct wlr_output *output =
		wlr_output_layout_output_at(server->output_layout, cursor_rect.x, cursor_rect.y);
	if (output == NULL) {
		return;
	}
	struct wlr_box output_box;
	wlr_output_layout_get_box(server->output_layout, output, &output_box);

	/* Protocol constraint_adjustment is a bitfield, not a closed enum set. */
	uint32_t adj = (uint32_t)XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_FLIP_Y |
		       (uint32_t)XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_SLIDE_X;
	struct wlr_xdg_positioner_rules rules = {0};
	rules.anchor_rect = cursor_rect;
	rules.anchor = XDG_POSITIONER_ANCHOR_BOTTOM_LEFT;
	rules.gravity = XDG_POSITIONER_GRAVITY_BOTTOM_RIGHT;
	/* NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange) */
	rules.constraint_adjustment = (enum xdg_positioner_constraint_adjustment)adj;
	rules.size.width = popup->popup->surface->current.width;
	rules.size.height = popup->popup->surface->current.height;

	struct wlr_box popup_box;
	wlr_xdg_positioner_rules_get_geometry(&rules, &popup_box);
	wlr_xdg_positioner_rules_unconstrain_box(&rules, &output_box, &popup_box);
	wlr_scene_node_set_position(&popup->tree->node, popup_box.x, popup_box.y);

	struct wlr_box popup_cursor = {
		.x = cursor_rect.x - popup_box.x,
		.y = cursor_rect.y - popup_box.y,
		.width = cursor_rect.width,
		.height = cursor_rect.height,
	};
	wlr_input_popup_surface_v2_send_text_input_rectangle(popup->popup, &popup_cursor);
}

static void update_popup_positions(struct sf_server *server) {
	struct sf_ime_popup *popup;
	wl_list_for_each(popup, &server->ime.popups, link) {
		update_popup_position(popup);
	}
}

static void on_text_input_enable(struct wl_listener *listener, void *data) {
	(void)data;
	struct sf_ime_text_input *ti = wl_container_of(listener, ti, enable);
	update_active_text_input(ti->server);
	wlr_text_input_v3_send_done(ti->input);
}

static void on_text_input_commit(struct wl_listener *listener, void *data) {
	(void)data;
	struct sf_ime_text_input *ti = wl_container_of(listener, ti, commit);
	if (ti->server->ime.active_text_input == ti->input) {
		send_state_to_input_method(ti->server);
		update_popup_positions(ti->server);
	}
}

static void on_text_input_disable(struct wl_listener *listener, void *data) {
	(void)data;
	struct sf_ime_text_input *ti = wl_container_of(listener, ti, disable);
	update_active_text_input(ti->server);
}

static void on_text_input_destroy(struct wl_listener *listener, void *data) {
	(void)data;
	struct sf_ime_text_input *ti = wl_container_of(listener, ti, destroy);
	struct sf_server *server = ti->server;
	wl_list_remove(&ti->enable.link);
	wl_list_remove(&ti->commit.link);
	wl_list_remove(&ti->disable.link);
	wl_list_remove(&ti->destroy.link);
	wl_list_remove(&ti->link);
	free(ti);
	update_active_text_input(server);
}

static void on_new_text_input(struct wl_listener *listener, void *data) {
	struct sf_server *server = wl_container_of(listener, server, ime.new_text_input);
	struct wlr_text_input_v3 *input = data;
	if (input->seat != server->seat) {
		return;
	}

	struct sf_ime_text_input *ti = calloc(1, sizeof(*ti));
	if (ti == NULL) {
		return;
	}
	ti->server = server;
	ti->input = input;
	ti->enable.notify = on_text_input_enable;
	wl_signal_add(&input->events.enable, &ti->enable);
	ti->commit.notify = on_text_input_commit;
	wl_signal_add(&input->events.commit, &ti->commit);
	ti->disable.notify = on_text_input_disable;
	wl_signal_add(&input->events.disable, &ti->disable);
	ti->destroy.notify = on_text_input_destroy;
	wl_signal_add(&input->events.destroy, &ti->destroy);
	wl_list_insert(&server->ime.text_inputs, &ti->link);
	update_text_input_focus(server);
}

static void on_input_method_commit(struct wl_listener *listener, void *data) {
	(void)data;
	struct sf_server *server = wl_container_of(listener, server, ime.input_method_commit);
	struct wlr_text_input_v3 *ti = server->ime.active_text_input;
	struct wlr_input_method_v2 *im = server->ime.input_method;
	if (ti == NULL || im == NULL) {
		return;
	}
	const struct wlr_input_method_v2_state *cur = &im->current;
	if (cur->preedit.text != NULL) {
		wlr_text_input_v3_send_preedit_string(
			ti, cur->preedit.text, cur->preedit.cursor_begin, cur->preedit.cursor_end);
	}
	if (cur->commit_text != NULL) {
		wlr_text_input_v3_send_commit_string(ti, cur->commit_text);
	}
	if (cur->delete.before_length != 0 || cur->delete.after_length != 0) {
		wlr_text_input_v3_send_delete_surrounding_text(ti, cur->delete.before_length,
							       cur->delete.after_length);
	}
	wlr_text_input_v3_send_done(ti);
}

static void on_grab_destroy(struct wl_listener *listener, void *data) {
	(void)data;
	struct sf_server *server = wl_container_of(listener, server, ime.grab_destroy);
	struct wlr_input_method_keyboard_grab_v2 *grab =
		server->ime.input_method != NULL ? server->ime.input_method->keyboard_grab : NULL;
	listener_clear(&server->ime.grab_destroy);
	if (grab != NULL && grab->keyboard != NULL) {
		wlr_seat_set_keyboard(server->seat, grab->keyboard);
		wlr_seat_keyboard_notify_modifiers(server->seat, &grab->keyboard->modifiers);
	}
}

static void on_input_method_grab_keyboard(struct wl_listener *listener, void *data) {
	struct sf_server *server =
		wl_container_of(listener, server, ime.input_method_grab_keyboard);
	struct wlr_input_method_keyboard_grab_v2 *grab = data;
	if (grab == NULL) {
		return;
	}
	struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(server->seat);
	if (keyboard != NULL && ime_grab_for_keyboard(server, keyboard) == grab) {
		wlr_input_method_keyboard_grab_v2_set_keyboard(grab, keyboard);
	}
	listener_clear(&server->ime.grab_destroy);
	server->ime.grab_destroy.notify = on_grab_destroy;
	wl_signal_add(&grab->events.destroy, &server->ime.grab_destroy);
}

static void on_popup_destroy(struct wl_listener *listener, void *data) {
	(void)data;
	struct sf_ime_popup *popup = wl_container_of(listener, popup, destroy);
	wl_list_remove(&popup->destroy.link);
	wl_list_remove(&popup->surface_commit.link);
	wl_list_remove(&popup->link);
	if (popup->tree != NULL) {
		wlr_scene_node_destroy(&popup->tree->node);
		popup->tree = NULL;
	}
	free(popup);
}

static void on_popup_surface_commit(struct wl_listener *listener, void *data) {
	(void)data;
	struct sf_ime_popup *popup = wl_container_of(listener, popup, surface_commit);
	update_popup_position(popup);
}

static void on_input_method_new_popup(struct wl_listener *listener, void *data) {
	struct sf_server *server = wl_container_of(listener, server, ime.input_method_new_popup);
	struct wlr_input_popup_surface_v2 *popup_surface = data;
	struct sf_ime_popup *popup = calloc(1, sizeof(*popup));
	if (popup == NULL) {
		return;
	}
	popup->server = server;
	popup->popup = popup_surface;
	popup->tree = wlr_scene_tree_create(server->layers.ime);
	if (popup->tree == NULL) {
		free(popup);
		return;
	}
	wlr_scene_subsurface_tree_create(popup->tree, popup_surface->surface);
	popup->destroy.notify = on_popup_destroy;
	wl_signal_add(&popup_surface->events.destroy, &popup->destroy);
	popup->surface_commit.notify = on_popup_surface_commit;
	wl_signal_add(&popup_surface->surface->events.commit, &popup->surface_commit);
	wl_list_insert(&server->ime.popups, &popup->link);
	update_popup_positions(server);
}

static void on_input_method_destroy(struct wl_listener *listener, void *data) {
	(void)data;
	struct sf_server *server = wl_container_of(listener, server, ime.input_method_destroy);
	listener_clear(&server->ime.input_method_commit);
	listener_clear(&server->ime.input_method_grab_keyboard);
	listener_clear(&server->ime.input_method_destroy);
	listener_clear(&server->ime.input_method_new_popup);
	listener_clear(&server->ime.grab_destroy);
	server->ime.input_method = NULL;
	update_text_input_focus(server);
	update_active_text_input(server);
}

static void on_new_input_method(struct wl_listener *listener, void *data) {
	struct sf_server *server = wl_container_of(listener, server, ime.new_input_method);
	struct wlr_input_method_v2 *im = data;
	if (im->seat != server->seat) {
		return;
	}
	if (server->ime.input_method != NULL) {
		SF_LOGI("ime second input method rejected");
		wlr_input_method_v2_send_unavailable(im);
		return;
	}

	server->ime.input_method = im;
	server->ime.input_method_commit.notify = on_input_method_commit;
	wl_signal_add(&im->events.commit, &server->ime.input_method_commit);
	server->ime.input_method_grab_keyboard.notify = on_input_method_grab_keyboard;
	wl_signal_add(&im->events.grab_keyboard, &server->ime.input_method_grab_keyboard);
	server->ime.input_method_destroy.notify = on_input_method_destroy;
	wl_signal_add(&im->events.destroy, &server->ime.input_method_destroy);
	server->ime.input_method_new_popup.notify = on_input_method_new_popup;
	wl_signal_add(&im->events.new_popup_surface, &server->ime.input_method_new_popup);
	update_text_input_focus(server);
	update_active_text_input(server);
}

static void on_keyboard_focus_change(struct wl_listener *listener, void *data) {
	struct sf_server *server = wl_container_of(listener, server, ime.keyboard_focus_change);
	struct wlr_seat_keyboard_focus_change_event *event = data;
	server->ime.focused_surface = event->new_surface;
	update_text_input_focus(server);
	update_active_text_input(server);
}

void sf_ime_setup(struct sf_server *server) {
	wl_list_init(&server->ime.text_inputs);
	wl_list_init(&server->ime.popups);
	server->ime.input_method = NULL;
	server->ime.focused_surface = NULL;
	server->ime.active_text_input = NULL;
	server->ime.grab_destroy.link.next = NULL;
	server->ime.input_method_commit.link.next = NULL;
	server->ime.input_method_grab_keyboard.link.next = NULL;
	server->ime.input_method_destroy.link.next = NULL;
	server->ime.input_method_new_popup.link.next = NULL;

	server->ime.text_input_mgr = wlr_text_input_manager_v3_create(server->wl_display);
	server->ime.input_method_mgr = wlr_input_method_manager_v2_create(server->wl_display);
	if (server->ime.text_input_mgr == NULL || server->ime.input_method_mgr == NULL) {
		SF_LOGW("ime managers unavailable");
		return;
	}

	server->ime.new_text_input.notify = on_new_text_input;
	wl_signal_add(&server->ime.text_input_mgr->events.new_text_input,
		      &server->ime.new_text_input);
	server->ime.new_input_method.notify = on_new_input_method;
	wl_signal_add(&server->ime.input_method_mgr->events.new_input_method,
		      &server->ime.new_input_method);
	server->ime.keyboard_focus_change.notify = on_keyboard_focus_change;
	wl_signal_add(&server->seat->keyboard_state.events.focus_change,
		      &server->ime.keyboard_focus_change);

	SF_LOGI("ime relay", sf_str("text_input", "v3"), sf_str("input_method", "v2"));
}

void sf_ime_finish(struct sf_server *server) {
	listener_clear(&server->ime.grab_destroy);
	listener_clear(&server->ime.input_method_commit);
	listener_clear(&server->ime.input_method_grab_keyboard);
	listener_clear(&server->ime.input_method_destroy);
	listener_clear(&server->ime.input_method_new_popup);
	listener_clear(&server->ime.keyboard_focus_change);
	listener_clear(&server->ime.new_input_method);
	listener_clear(&server->ime.new_text_input);

	struct sf_ime_popup *popup, *ptmp;
	wl_list_for_each_safe(popup, ptmp, &server->ime.popups, link) {
		wl_list_remove(&popup->destroy.link);
		wl_list_remove(&popup->surface_commit.link);
		wl_list_remove(&popup->link);
		if (popup->tree != NULL) {
			wlr_scene_node_destroy(&popup->tree->node);
		}
		free(popup);
	}
	struct sf_ime_text_input *ti, *ttmp;
	wl_list_for_each_safe(ti, ttmp, &server->ime.text_inputs, link) {
		wl_list_remove(&ti->enable.link);
		wl_list_remove(&ti->commit.link);
		wl_list_remove(&ti->disable.link);
		wl_list_remove(&ti->destroy.link);
		wl_list_remove(&ti->link);
		free(ti);
	}

	server->ime.input_method = NULL;
	server->ime.focused_surface = NULL;
	server->ime.active_text_input = NULL;
	server->ime.text_input_mgr = NULL;
	server->ime.input_method_mgr = NULL;
}
