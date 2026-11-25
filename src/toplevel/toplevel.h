#ifndef SF_TOPLEVEL_H
#define SF_TOPLEVEL_H

#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include <pixman.h>
#include <wayland-server-core.h>
#include <wlr/util/box.h>

#include "server/server.h"
#include "ssd/ssd_policy.h"
#include "wm/layout_sm.h"
#include "wm/place_sm.h"

struct wlr_buffer;
struct wlr_scene_blur;
struct wlr_scene_rect;
struct wlr_scene_shadow;
struct wlr_scene;
struct wlr_ext_foreign_toplevel_handle_v1;
struct wlr_ext_image_capture_source_v1;
struct wlr_xdg_toplevel;
struct wlr_xdg_toplevel_decoration_v1;
struct wlr_xdg_toplevel_icon_v1;
struct wlr_xwayland_surface;
struct wlr_surface;
struct sf_workspace;
struct sf_column;
struct sf_ssd;

struct sf_toplevel {
	enum sf_node_kind kind; /* SF_NODE_TOPLEVEL */
	struct wl_list link;
	struct wl_list minimize_link;
	/* Row link inside sf_column.rows (columns arrange). */
	struct wl_list column_link;
	struct sf_server *server;
	struct wlr_xdg_toplevel *xdg_toplevel;
	struct wlr_xwayland_surface *xwayland_surface;
	/* Wrapper: blur + shadow + border + xdg_scene_tree. */
	struct wlr_scene_tree *scene_tree;
	struct wlr_scene_tree *xdg_scene_tree;
	/* Hosts modal dialog scene trees (Umbriel attachmentTree). */
	struct wlr_scene_tree *attachment_tree;
	struct wlr_scene_blur *blur;
	struct wlr_scene_shadow *shadow;
	/* Full rect + clipped_region hole (mango). Blur stays off so the underlay
	 * is not an opaque black plate; hole lets client opacity show the desktop. */
	struct wlr_scene_rect *border;
	/* Umbriel-style opaque plate under FS content: hides wallpaper while the
	 * client buffer catches up to the output size (and covers fractional
	 * hairlines). Sized to the FS tile; disabled otherwise. */
	struct wlr_scene_rect *fs_backdrop;
	/* Unfocused spotlight dim overlay above content, below border. */
	struct wlr_scene_rect *dim_overlay;
	/* Shade while a modal dialog is attached (above content, below attach). */
	struct wlr_scene_rect *modal_shade;
	/* Non-NULL when this window's scene_tree is under parent's attachment_tree. */
	struct sf_toplevel *modal_attached_to;

	/* Off-screen duplicate tree for ext-image-capture (Noctalia switcher). */
	struct wlr_ext_foreign_toplevel_handle_v1 *ext_handle;
	struct wlr_scene *image_capture_scene;
	struct wlr_scene_tree *image_capture_tree;
	struct wlr_ext_image_capture_source_v1 *image_capture_source;
	struct wl_listener map;
	struct wl_listener unmap;
	struct wl_listener commit;
	struct wl_listener destroy;
	struct wl_listener request_move;
	struct wl_listener request_resize;
	struct wl_listener request_maximize;
	struct wl_listener request_minimize;
	struct wl_listener request_fullscreen;
	struct wl_listener set_title;
	struct wl_listener set_app_id;
	struct wl_listener set_parent;
	/* Xwayland-only listeners (linked only when xwayland_surface != NULL). */
	struct wl_listener xwayland_associate;
	struct wl_listener xwayland_dissociate;
	struct wl_listener xwayland_request_configure;
	struct wl_listener xwayland_request_activate;
	struct wl_listener xwayland_set_override_redirect;
	struct wl_listener xwayland_set_window_type;
	struct wl_listener xwayland_set_size_hints;
	bool xwayland_surface_listeners_attached;

	/* Stable for the life of this toplevel; used by IPC `windows`. */
	uint64_t id;

	/* Exclusive float/max/snap/fullscreen + PreMax geom (wm/layout_sm). */
	struct sf_layout_sm layout;
	/* Map / SoftPlace / appear-hold (wm/place_sm). */
	struct sf_place_sm place;
	/* Workspace arrange engine owns this window's box. */
	bool arrange_managed;
	/* Explicit sticky float (toggle_float / float / rule). Tiling layouts
	 * rejoin every other eligible window so nothing sits under the strip.
	 * Tile-originated max-edges does *not* set this — see rejoin flag. */
	bool arrange_floating;
	/* Tile broke out for max-edges as a transient overlay. Unmax / focus
	 * change / Super+Tab release rejoins the strip. Cleared when the window
	 * was already an explicit float before max-edges. */
	bool arrange_max_edges_rejoin;
	/* First-class column membership (umbriel). NULL when not in the strip. */
	struct sf_column *column;
	/* Cross-axis weight inside the column (default 1). */
	float column_row_weight;
	/* Primary extent remembered across consume; restored on expel. */
	float column_extent_saved;
	/* Strip cell remembered across leave_tiling / float / max-edges break-out
	 * so unfloat / unmax / stacking→columns restores the same column+row. */
	bool arrange_slot_saved;
	int arrange_slot_col;
	int arrange_slot_row;
	float arrange_slot_frac;
	bool arrange_slot_full;
	/* After break-out: suppress GEOM regain until box leaves hold. */
	bool geom_regain_hold;
	struct wlr_box geom_regain_box;
	/* Float geom captured when first joining a tiling arrange; restored
	 * when returning to stacking. */
	bool arrange_float_saved;
	struct wlr_box arrange_float_box;
	/* True after the user floated and sized a window (unfloat remembers).
	 * Next Super+v restores arrange_float_box instead of the tile cell. */
	bool arrange_float_custom;
	/* Last cell size proposed by columns pack; appear waits until the
	 * client commits near this (or give-up commits elapse). 0 = none. */
	int arrange_prop_w;
	int arrange_prop_h;
	/* Commits seen while waiting for arrange_prop_* before appear. */
	int arrange_prop_waits;
	/* Neighbor cell waiting for an opener/closer slide before morph
	 * (push/pull in sync with the appear/close slide). */
	bool arrange_deferred;
	struct wlr_box arrange_deferred_box;
	/* Tiled map: pack/defer until appear slide finishes. Survives scene
	 * enable so a mid-slide arrange cannot cancel the opener slide into a
	 * short morph (sequential push-then-pop). */
	bool arrange_appear_pending;
	/* Slide uses column right-gap even after unlink (WM close / hide). */
	bool anim_slide_column;
	bool minimized;
	/* xdg_dialog_v1.set_modal (requires parent for attach). */
	bool modal;
	/* window_rule borderless: no WM border/shadow. */
	bool borderless;
	/* Sticky: stay above normal windows in the same scene tree. */
	bool always_on_top;
	/* window_rule ssd override; when rule_ssd_set, use rule_ssd_mode. */
	bool rule_ssd_set;
	enum sf_ssd_mode rule_ssd_mode;
	/* window_rule per-window opacity / dim opt-out. */
	bool rule_opacity_set;
	float rule_opacity;
	bool rule_inactive_opacity_set;
	float rule_inactive_opacity;
	bool rule_no_dim_set;
	bool rule_no_dim;
	/* window_rule :tearing true|false; unset follows client hint. */
	bool rule_tearing_set;
	bool rule_tearing;
	/* Demand-attention until focused (xdg-activation). */
	bool urgent;
	/* Block ext-image-capture / full-output screencopy (IPC / config). */
	bool capture_blocked;
	/* Scene tree temporarily disabled for the filtered capture pass. */
	bool capture_pass_hidden;
	/* Transient system-bell flash overrides border color. */
	bool bell_flash;
	struct wl_event_source *bell_timer;

	/* Named scratchpad id, or empty when not on a pad. */
	char scratchpad[32];

	/* Per-output workspace membership; NULL for scratchpads. */
	struct sf_workspace *workspace;

	/* xdg-toplevel-tag-v1 (client-set; heap, may be NULL). */
	char *tag;
	char *description;

	/* xdg-toplevel-icon-v1 (ref'd icon + cached name for IPC). */
	struct wlr_xdg_toplevel_icon_v1 *icon;
	char *icon_name;

	/* ext-background-effect-v1: non-empty blur region → plates under
	 * client_blur_tree (sibling of xdg_scene_tree, surface-local). Never
	 * parent plates inside xdg_scene_tree — that breaks
	 * wlr_scene_subsurface_tree_set_clip (assert found). Chrome `blur`
	 * stays the window_blur full plate (disabled while client plates are
	 * active). */
	bool client_blur;
	pixman_region32_t blur_region; /* surface-local; empty when off */
	struct wl_list blur_plates;    /* sf_blur_plate.link */
	struct wlr_scene_tree *client_blur_tree;

	/* xdg-decoration: client preference + compositor SSD titlebar. */
	bool want_ssd;
	/* Last mode we passed to set_mode (avoid configure storms). */
	int ssd_set_mode;
	struct wlr_xdg_toplevel_decoration_v1 *decoration;
	struct sf_ssd *ssd;

	/* Last good client buffer for Ctrl+W / CSD close-ghost (NULL-buffer unmap). */
	struct wlr_buffer *close_snap;
	int close_snap_w;
	int close_snap_h;

	/* Logical resting position while a slide/morph runs (scene node is visual). */
	int resting_x;
	int resting_y;
	bool anim_hide;
	/* Re-apply pop present crop after surface commit (scene helper resets
	 * dest_size; without this, terminals flicker scale↔full every damage).
	 * Also used for post-pop settle scrub. pop_idle_kind: 1=reapply 2=settle. */
	struct wl_event_source *pop_present_idle;
	int pop_idle_kind;
	/* Pending zwlr-foreign-toplevel props; flushed on wl idle (see foreign.c). */
	uint32_t foreign_dirty;
	/* True while Umbriel presented crop is active (pop only). */
	bool present_stretch;
	struct {
		bool active;
		bool morph; /* false = pos-only slide; true = live peeble-style morph */
		/* Umbriel popin/popout: present-scale + fade at the final slot
		 * (no client set_size). Tiled appear/hide; floats/scratch slide. */
		bool pop;
		struct timespec start;
		int64_t dur_ns;
		int from_x, from_y;
		int to_x, to_y;
		int from_w, from_h;
		int to_w, to_h;
		/* Last size proposed this morph (skip redundant set_size).
		 * During pop: current presented size for decor/crop. */
		int prop_w, prop_h;
		float fade_from;
		float fade_to;
		float fade; /* current; decor multiplies chrome opacity */
		void (*on_done)(struct sf_toplevel *);
	} anim;
};

/* Backend-agnostic toplevel accessors (xdg-shell + Xwayland). */
bool sf_toplevel_has_backend(const struct sf_toplevel *toplevel);
struct wlr_surface *sf_toplevel_wlr_surface(const struct sf_toplevel *toplevel);
bool sf_toplevel_is_mapped(const struct sf_toplevel *toplevel);
bool sf_toplevel_is_initialized(const struct sf_toplevel *toplevel);
const char *sf_toplevel_title(const struct sf_toplevel *toplevel);
const char *sf_toplevel_app_id(const struct sf_toplevel *toplevel);
void sf_toplevel_surface_geom(const struct sf_toplevel *toplevel, struct wlr_box *out);
bool sf_toplevel_has_parent(const struct sf_toplevel *toplevel);
struct sf_toplevel *sf_toplevel_parent(const struct sf_toplevel *toplevel);
/* Umbriel looksTiled: false → float (parent / X dialog types / fixed min==max). */
bool sf_toplevel_looks_tiled(const struct sf_toplevel *toplevel);
void sf_toplevel_min_size(const struct sf_toplevel *toplevel, int *min_w, int *min_h);
void sf_toplevel_set_size(struct sf_toplevel *toplevel, int w, int h);
void sf_toplevel_set_pos(struct sf_toplevel *toplevel, int x, int y);
void sf_toplevel_set_activated(struct sf_toplevel *toplevel, bool active);
void sf_toplevel_set_maximized_state(struct sf_toplevel *toplevel, bool maximized);
void sf_toplevel_set_fullscreen_state(struct sf_toplevel *toplevel, bool fullscreen);
void sf_toplevel_set_minimized_state(struct sf_toplevel *toplevel, bool minimized);
void sf_toplevel_set_tiled_edges(struct sf_toplevel *toplevel, uint32_t edges);
void sf_toplevel_send_close(struct sf_toplevel *toplevel);

#endif
