#ifndef STONEFLY_SERVER_H
#define STONEFLY_SERVER_H

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

struct stonefly_toplevel;
struct stonefly_output;
struct stonefly_keyboard;
struct stonefly_ipc;
struct stonefly_layer_surface;
struct stonefly_snap_preview;
struct stonefly_dbus_notify;

#include "config/config.h"
#include "wm/drag_snap.h"

enum stonefly_cursor_mode {
	STONEFLY_CURSOR_PASSTHROUGH,
	STONEFLY_CURSOR_MOVE,
	STONEFLY_CURSOR_RESIZE,
};

/* Discriminator for scene node->data (toplevel vs layer vs SSD). First field. */
enum stonefly_node_kind {
	STONEFLY_NODE_TOPLEVEL = 1,
	STONEFLY_NODE_LAYER = 2,
	STONEFLY_NODE_SSD = 3,
};

struct stonefly_server {
	struct wl_display *wl_display;
	struct wlr_backend *backend;
	/* DRM/seat session from autocreate; NULL under nested backends. */
	struct wlr_session *session;
	struct wlr_renderer *renderer;
	struct wlr_allocator *allocator;
	struct wlr_scene *scene;
	struct wlr_scene_output_layout *scene_layout;

	/* Z-order: background < bottom < blur < toplevel < top < overlay < lock. */
	struct {
		struct wlr_scene_tree *background;
		struct wlr_scene_tree *bottom;
		struct wlr_scene_tree *toplevel;
		struct wlr_scene_tree *top;
		struct wlr_scene_tree *overlay;
		struct wlr_scene_tree *lock;
	} layers;
	struct wlr_scene_optimized_blur *blur_layer;

	/* Client-close ghosts sliding out after surface destroy. */
	struct wl_list ghosts;

	struct wlr_layer_shell_v1 *layer_shell;
	struct wl_listener new_layer_surface;
	struct stonefly_layer_surface *exclusive_layer;

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
	enum stonefly_cursor_mode cursor_mode;
	struct stonefly_toplevel *grabbed_toplevel;
	double grab_x, grab_y;
	struct wlr_box grab_geobox;
	uint32_t resize_edges;
	struct stonefly_drag_snap drag_snap;
	struct stonefly_snap_preview *snap_preview;

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

	/* zwlr_foreign_toplevel_management_v1 */
	struct wl_global *foreign_toplevel_global;
	struct wl_list foreign_managers;
	struct wl_list foreign_handles;

	/* ext-foreign-toplevel-list + image-capture (Noctalia switcher snapshots) */
	struct wlr_ext_foreign_toplevel_list_v1 *ext_foreign_toplevel_list;
	struct wlr_ext_foreign_toplevel_image_capture_source_manager_v1 *ext_toplevel_capture_mgr;
	struct wl_listener ext_toplevel_capture_request;

	struct stonefly_ipc *ipc;

	struct stonefly_config config;

	/* Lone-modifier tap (Super): peeble bind_tap semantics. */
	struct {
		bool armed;
		struct timespec since;
		int taps;
		struct wl_event_source *timer;
		int single;
		int double_tap;
		char *single_cmd;
		char *double_cmd;
	} tap;

	/* ext-session-lock-v1 + idle + output-power (Noctalia auto-lock / DPMS). */
	bool locked;
	struct wlr_session_lock_manager_v1 *session_lock_mgr;
	struct wlr_session_lock_v1 *session_lock;
	struct wlr_scene_tree *session_lock_tree;
	struct wlr_scene_rect *lock_blank;
	struct wl_list lock_surfaces;
	struct wl_listener session_lock_new;
	struct wl_listener session_lock_new_surface;
	struct wl_listener session_lock_unlock;
	struct wl_listener session_lock_destroy;

	struct wlr_idle_notifier_v1 *idle_notifier;
	struct wlr_idle_inhibit_manager_v1 *idle_inhibit_mgr;
	struct wl_listener idle_inhibitor_new;

	struct wlr_output_power_manager_v1 *output_power_mgr;
	struct wl_listener output_power_set_mode;

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
	struct wlr_pointer_constraints_v1 *pointer_constraints;
	struct wl_listener new_pointer_constraint;
	struct wlr_pointer_constraint_v1 *current_constraint;

	/* Heap; set when user init.lua fails and we fall back to embedded/C.
	 * Surfaced via FDO Notifications after the display is up (peeble-style). */
	char *config_warn;
	/* Pending NameOwnerChanged wait + Notify (owned by dbus.c). */
	struct stonefly_dbus_notify *config_notify;
};

bool stonefly_server_init(struct stonefly_server *server);
int stonefly_server_run(struct stonefly_server *server, const char *startup_cmd);
void stonefly_server_finish(struct stonefly_server *server);

#endif
