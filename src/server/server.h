#ifndef SF_SERVER_H
#define SF_SERVER_H

#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include <wayland-server-core.h>
#include <wlr/util/box.h>

struct wlr_allocator;
struct wlr_backend;
struct wlr_session;
struct wlr_cursor;
struct wlr_cursor_shape_manager_v1;
struct wlr_layer_shell_v1;
struct wlr_output_layout;
struct wlr_renderer;
struct wlr_scene;
struct wlr_scene_optimized_blur;
struct wlr_scene_output_layout;
struct wlr_scene_tree;
struct wlr_seat;
struct wlr_xcursor_manager;
struct wlr_xdg_activation_v1;
struct wlr_xdg_shell;
struct wlr_xdg_system_bell_v1;
struct wlr_xdg_wm_dialog_v1;
struct wlr_xdg_decoration_manager_v1;
struct wlr_xdg_toplevel_tag_manager_v1;
struct wlr_xdg_toplevel_icon_manager_v1;
struct wlr_ext_foreign_toplevel_list_v1;
struct wlr_ext_foreign_toplevel_image_capture_source_manager_v1;
struct wlr_ext_data_control_manager_v1;
struct wlr_data_control_manager_v1;
struct wlr_gamma_control_manager_v1;
struct wlr_session_lock_manager_v1;
struct wlr_session_lock_v1;
struct wlr_idle_notifier_v1;
struct wlr_idle_inhibit_manager_v1;
struct wlr_output_power_manager_v1;
struct wlr_output_manager_v1;
struct wlr_presentation;
struct wlr_tearing_control_manager_v1;
struct wlr_scene_rect;
struct wlr_ext_workspace_manager_v1;
struct wlr_pointer_gestures_v1;
struct wlr_virtual_keyboard_manager_v1;
struct wlr_relative_pointer_manager_v1;
struct wlr_pointer_constraints_v1;
struct wlr_pointer_constraint_v1;

struct sf_toplevel;
struct sf_output;
struct sf_keyboard;
struct sf_ipc;
struct sf_layer_surface;
struct sf_snap_preview;
struct sf_dbus_notify;

#include "config/config.h"
#include "wm/drag_snap.h"

enum sf_cursor_mode {
	SF_CURSOR_PASSTHROUGH,
	SF_CURSOR_MOVE,
	SF_CURSOR_RESIZE,
	/* Super+Shift+RMB: grow/shrink around a fixed center (both axes). */
	SF_CURSOR_RESIZE_CENTER,
	/* Super+LMB on a tiled window: drag to swap tile order (not float/snap). */
	SF_CURSOR_ARRANGE_SWAP,
	/* Super+RMB on a tiled window: adjust column_ratio. */
	SF_CURSOR_ARRANGE_RESIZE,
};

/* Discriminator for scene node->data (toplevel vs layer vs SSD). First field. */
enum sf_node_kind {
	SF_NODE_TOPLEVEL = 1,
	SF_NODE_LAYER = 2,
	SF_NODE_SSD = 3,
};

struct sf_server {
	struct wl_display *wl_display;
	struct wlr_backend *backend;
	/* DRM/seat session from autocreate; NULL under nested backends. */
	struct wlr_session *session;
	struct wlr_renderer *renderer;
	struct wlr_allocator *allocator;
	struct wlr_scene *scene;
	struct wlr_scene_output_layout *scene_layout;

	/* Z-order: background < bottom < blur < toplevel < top <
	 * fullscreen < overlay < lock. Fullscreen xdg sits above the bar. */
	struct {
		struct wlr_scene_tree *background;
		struct wlr_scene_tree *bottom;
		struct wlr_scene_tree *toplevel;
		struct wlr_scene_tree *top;
		struct wlr_scene_tree *fullscreen;
		struct wlr_scene_tree *overlay;
		struct wlr_scene_tree *lock;
	} layers;
	struct wlr_scene_optimized_blur *blur_layer;

	/* Client-close ghosts sliding out after surface destroy. */
	struct wl_list ghosts;

	struct wlr_layer_shell_v1 *layer_shell;
	struct wl_listener new_layer_surface;
	struct sf_layer_surface *exclusive_layer;

	struct wlr_xdg_shell *xdg_shell;
	struct wl_listener new_xdg_toplevel;
	struct wl_listener new_xdg_popup;
	struct wl_list toplevels;

	/* FORK.md #1281 / #1389 — demand-attention + system bell. */
	struct wlr_xdg_activation_v1 *xdg_activation;
	struct wl_listener request_activate;
	struct wlr_xdg_system_bell_v1 *system_bell;
	struct wl_listener system_bell_ring;

	/* xdg_wm_dialog_v1 — client modal hint (with set_parent). */
	struct wlr_xdg_wm_dialog_v1 *xdg_dialog_mgr;
	struct wl_listener new_xdg_dialog;

	/* xdg-decoration: mode from ssd_mode + client request_mode. */
	struct wlr_xdg_decoration_manager_v1 *xdg_decoration_mgr;
	struct wl_listener new_xdg_decoration;

	/* xdg-toplevel-tag-v1 — client identity for rules / persist / IPC. */
	struct wlr_xdg_toplevel_tag_manager_v1 *xdg_toplevel_tag_mgr;
	struct wl_listener xdg_toplevel_set_tag;
	struct wl_listener xdg_toplevel_set_description;

	/* xdg-toplevel-icon-v1 — per-window icon name / buffers for IPC / SSD. */
	struct wlr_xdg_toplevel_icon_manager_v1 *xdg_toplevel_icon_mgr;
	struct wl_listener xdg_toplevel_set_icon;

	/* ext-background-effect-v1 — client blur region → client_blur gate. */
	struct wl_global *background_effect_global;

	/* Noctalia night light / clipboard history (wlroots helpers). */
	struct wlr_gamma_control_manager_v1 *gamma_control_mgr;
	struct wlr_ext_data_control_manager_v1 *ext_data_control_mgr;
	struct wlr_data_control_manager_v1 *wlr_data_control_mgr;

	struct wlr_cursor *cursor;
	struct wlr_xcursor_manager *cursor_mgr;
	struct wlr_cursor_shape_manager_v1 *cursor_shape_mgr;
	struct wl_listener cursor_motion;
	struct wl_listener cursor_motion_absolute;
	struct wl_listener cursor_button;
	struct wl_listener cursor_axis;
	struct wl_listener cursor_frame;
	struct wl_listener cursor_touch_down;
	struct wl_listener cursor_touch_up;
	struct wl_listener cursor_touch_motion;
	struct wl_listener cursor_touch_cancel;
	struct wl_listener cursor_touch_frame;

	struct wlr_seat *seat;
	struct wl_listener new_input;
	struct wl_listener request_cursor;
	struct wl_listener request_set_cursor_shape;
	struct wl_listener pointer_focus_change;
	struct wl_listener request_set_selection;
	struct wl_listener request_set_primary_selection;
	struct wl_list keyboards;
	struct wl_list touch;
	enum sf_cursor_mode cursor_mode;
	/* True while process_cursor_motion set a WM resize/move xcursor face.
	 * Cleared when leaving the hit band so the face cannot stick. */
	bool pointer_wm_cursor;
	struct sf_toplevel *grabbed_toplevel;
	double grab_x, grab_y;
	struct wlr_box grab_geobox;
	uint32_t resize_edges;
	/* Layout-space content center locked for SF_CURSOR_RESIZE_CENTER. */
	double resize_center_x, resize_center_y;
	struct sf_drag_snap drag_snap;
	/* Tile-swap drag (SF_CURSOR_ARRANGE_SWAP). */
	struct {
		bool active;
		struct sf_toplevel *hover;
	} arrange_swap;
	/* Tile-factor resize (SF_CURSOR_ARRANGE_RESIZE). */
	struct {
		bool active;
		bool vertical; /* dy → factor (master top/bottom); else dx */
		double start_cx;
		double start_cy;
		float start_factor;
		int span; /* usable width or height for delta→factor */
	} arrange_resize;
	struct sf_snap_preview *snap_preview;

	struct wlr_output_layout *output_layout;
	struct wl_list outputs;
	struct wl_listener new_output;

	/* zwlr_output_manager_v1 — wdisplays / kanshi / Noctalia display settings. */
	struct wlr_output_manager_v1 *output_mgr;
	struct wl_listener output_mgr_apply;
	struct wl_listener output_mgr_test;

	/* wp_presentation + wp_tearing_control_v1 (advertise only this pass). */
	struct wlr_presentation *presentation;
	struct wlr_tearing_control_manager_v1 *tearing_control_mgr;
	struct wl_listener tearing_new_object;

	struct wl_list minimized;

	uint64_t next_toplevel_id;

	/* zwlr-foreign-toplevel + ext-foreign-toplevel-list + image-capture */
	struct {
		struct wl_global *wlr_global;
		struct wl_list managers;
		struct wl_list handles;
		struct wlr_ext_foreign_toplevel_list_v1 *ext_list;
		struct wlr_ext_foreign_toplevel_image_capture_source_manager_v1 *capture_mgr;
		struct wl_listener on_capture_request;
	} foreign;

	struct sf_ipc *ipc;

	struct sf_config config;

	/* Lone-modifier tap (Super): peeble bind_tap semantics.
	 * Step pointers alias into live config (stable until reload). */
	struct {
		bool armed;
		struct timespec since;
		int taps;
		struct wl_event_source *timer;
		const struct sf_action_step *single_steps;
		size_t n_single_steps;
		const struct sf_action_step *double_steps;
		size_t n_double_steps;
	} tap;

	/* ext-session-lock-v1 (Noctalia auto-lock). */
	struct {
		bool active;
		struct wlr_session_lock_manager_v1 *mgr;
		struct wlr_session_lock_v1 *current;
		struct wlr_scene_tree *tree;
		struct wlr_scene_rect *blank;
		struct wl_list surfaces;
		struct wl_listener on_new;
		struct wl_listener on_new_surface;
		struct wl_listener on_unlock;
		struct wl_listener on_destroy;
	} lock;

	struct {
		struct wlr_idle_notifier_v1 *notifier;
		struct wlr_idle_inhibit_manager_v1 *inhibit_mgr;
		struct wl_listener on_inhibitor_new;
	} idle;

	struct {
		struct wlr_output_power_manager_v1 *mgr;
		struct wl_listener on_set_mode;
	} output_power;

	/* ext-workspace-v1 — one group per output, handles per workspace. */
	struct wlr_ext_workspace_manager_v1 *ext_workspace_mgr;
	struct wl_listener ext_workspace_commit;
	uint32_t next_workspace_ext_id;

	/* Forward unused touchpad gestures to clients. */
	struct wlr_pointer_gestures_v1 *pointer_gestures;

	/* In-progress 3-finger workspace swipe. */
	struct {
		bool active;
		bool consumed;
		uint32_t fingers;
		double dx, dy;
	} swipe;

	/* Pointer devices with gesture listeners attached. */
	struct wl_list pointers;

	/* zwp_virtual_keyboard_v1 — attach like a normal keyboard. */
	struct wlr_virtual_keyboard_manager_v1 *virtual_keyboard_mgr;
	struct wl_listener new_virtual_keyboard;

	/* zwp_relative_pointer_manager_v1 — raw deltas for locked/raw clients. */
	struct wlr_relative_pointer_manager_v1 *relative_pointer_mgr;

	/* zwp_pointer_constraints_v1 — lock/confine when surface has focus. */
	struct {
		struct wlr_pointer_constraints_v1 *mgr;
		struct wl_listener on_new;
		struct wlr_pointer_constraint_v1 *current;
	} constraints;

	/* Heap; set when user init.lua fails and we fall back to embedded/C.
	 * Surfaced via FDO Notifications after the display is up (peeble-style). */
	char *config_warn;
	/* Pending NameOwnerChanged wait + Notify (owned by dbus.c). */
	struct sf_dbus_notify *config_notify;
};

bool sf_server_init(struct sf_server *server);
int sf_server_run(struct sf_server *server, const char *startup_cmd);
void sf_server_finish(struct sf_server *server);

#endif
