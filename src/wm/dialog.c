#include "wm/dialog.h"

#include <stdlib.h>
#include <wayland-server-core.h>
#include <scenefx/types/fx/clipped_region.h>
#include <scenefx/types/wlr_scene.h>
#include <wlr/util/box.h>

#include "config/config.h"
#include "decor/decor.h"
#include "layer/layer.h"
#include "log/log.h"
#include "toplevel/toplevel.h"
#include "wm/arrange.h"
#include "wm/layout_sm.h"
#include "wm/wm.h"
#include "wm/workspace.h"

bool sf_toplevel_modal_attached(const struct sf_toplevel *toplevel) {
	return toplevel != NULL && toplevel->modal_attached_to != NULL;
}

static struct sf_toplevel *shell_parent(struct sf_toplevel *toplevel) {
	return sf_toplevel_parent(toplevel);
}

static bool wants_attach(struct sf_toplevel *toplevel) {
	if (toplevel == NULL || !toplevel->modal || toplevel->server == NULL) {
		return false;
	}
	if (!sf_cfg(toplevel->server)->attach_modal_dialogs) {
		return false;
	}
	if (!sf_toplevel_is_mapped(toplevel) || toplevel->scene_tree == NULL) {
		return false;
	}
	struct sf_toplevel *parent = shell_parent(toplevel);
	if (parent == NULL || parent->scene_tree == NULL || parent->attachment_tree == NULL) {
		return false;
	}
	/* Reject cycles. */
	for (struct sf_toplevel *a = parent; a != NULL; a = a->modal_attached_to) {
		if (a == toplevel) {
			return false;
		}
	}
	return true;
}

struct sf_toplevel *sf_toplevel_modal_input_delegate(struct sf_toplevel *toplevel) {
	if (toplevel == NULL || toplevel->server == NULL) {
		return toplevel;
	}
	struct sf_toplevel *delegate = toplevel;
	for (;;) {
		struct sf_toplevel *child = NULL;
		struct sf_toplevel *t;
		wl_list_for_each(t, &toplevel->server->toplevels, link) {
			if (t->modal_attached_to == delegate && t->modal) {
				child = t;
				/* Prefer MRU (list head is front). */
				break;
			}
		}
		if (child == NULL) {
			return delegate;
		}
		delegate = child;
	}
}

static void sync_modal_shade(struct sf_toplevel *parent, bool blocked) {
	if (parent == NULL || parent->scene_tree == NULL) {
		return;
	}
	const struct sf_config *cfg = sf_cfg(parent->server);
	float dim = cfg->dialog_dim;
	if (dim < 0.0f) {
		dim = 0.0f;
	}
	if (dim > 1.0f) {
		dim = 1.0f;
	}
	float alpha = blocked ? dim : 0.0f;
	if (parent->modal_shade == NULL) {
		if (alpha <= 0.001f) {
			return;
		}
		static const float clear[4] = {0.0f, 0.0f, 0.0f, 0.0f};
		parent->modal_shade = wlr_scene_rect_create(parent->scene_tree, 0, 0, clear);
		if (parent->modal_shade == NULL) {
			return;
		}
		/* Hit-test pass-through is in sf_desktop_toplevel_at (like dim). */
		if (parent->attachment_tree != NULL) {
			wlr_scene_node_place_below(&parent->modal_shade->node,
						   &parent->attachment_tree->node);
		}
	}
	struct wlr_box geo;
	sf_toplevel_surface_geom(parent, &geo);
	int w = geo.width > 0 ? geo.width : 1;
	int h = geo.height > 0 ? geo.height : 1;
	float color[4] = {0.0f, 0.0f, 0.0f, alpha};
	wlr_scene_rect_set_color(parent->modal_shade, color);
	wlr_scene_rect_set_size(parent->modal_shade, w, h);
	wlr_scene_node_set_position(&parent->modal_shade->node, 0, 0);
	/* Match content arc (Umbriel surfaceRadius / nested content edge). */
	int radius = cfg->corner_radius;
	if (sf_layout_sm_is_fullscreen(&parent->layout)) {
		radius = 0;
	}
	wlr_scene_rect_set_corner_radii(parent->modal_shade, corner_radii_all(radius));
	wlr_scene_node_set_enabled(&parent->modal_shade->node, alpha > 0.001f);
}

static bool parent_blocked(struct sf_toplevel *parent) {
	if (parent == NULL || parent->server == NULL) {
		return false;
	}
	struct sf_toplevel *t;
	wl_list_for_each(t, &parent->server->toplevels, link) {
		if (t->modal_attached_to == parent) {
			return true;
		}
	}
	return false;
}

void sf_toplevel_modal_layout_attachments(struct sf_toplevel *parent) {
	if (parent == NULL || parent->server == NULL || parent->attachment_tree == NULL) {
		return;
	}
	struct wlr_box parent_geom;
	sf_toplevel_layout_geom(parent, &parent_geom);
	struct wlr_box area = {0};
	double pcx = parent_geom.x + parent_geom.width / 2.0;
	double pcy = parent_geom.y + parent_geom.height / 2.0;
	if (!sf_layout_usable_at(parent->server, pcx, pcy, &area)) {
		if (!sf_toplevel_output_box(parent, &area)) {
			area = parent_geom;
		}
	}

	struct sf_toplevel *t;
	wl_list_for_each(t, &parent->server->toplevels, link) {
		if (t->modal_attached_to != parent || t->scene_tree == NULL) {
			continue;
		}
		struct wlr_box geo;
		sf_toplevel_surface_geom(t, &geo);
		int w = geo.width > 0 ? geo.width : 1;
		int h = geo.height > 0 ? geo.height : 1;
		/* Relative to parent content origin. */
		int x = (parent_geom.width - w) / 2;
		int y = (parent_geom.height - h) / 2;
		/* Keep on usable in absolute space. */
		int abs_x = parent_geom.x + x;
		int abs_y = parent_geom.y + y;
		if (area.width > 0 && area.height > 0) {
			if (abs_x + w > area.x + area.width) {
				abs_x = area.x + area.width - w;
			}
			if (abs_y + h > area.y + area.height) {
				abs_y = area.y + area.height - h;
			}
			if (abs_x < area.x) {
				abs_x = area.x;
			}
			if (abs_y < area.y) {
				abs_y = area.y;
			}
			x = abs_x - parent_geom.x;
			y = abs_y - parent_geom.y;
		}
		wlr_scene_node_set_position(&t->scene_tree->node, x, y);
		t->resting_x = abs_x;
		t->resting_y = abs_y;
		/* Keep natural client size; only re-center in the parent box. */
		sf_decor_update_geom(t);
	}
	sync_modal_shade(parent, parent_blocked(parent));
}

static void attach_to(struct sf_toplevel *toplevel, struct sf_toplevel *parent) {
	if (toplevel->modal_attached_to == parent) {
		sf_toplevel_modal_layout_attachments(parent);
		return;
	}
	if (toplevel->modal_attached_to != NULL) {
		sf_toplevel_modal_detach(toplevel);
	}

	/* Leave tiling / FS / max as ordinary float under the parent. */
	if (sf_layout_sm_is_fullscreen(&toplevel->layout)) {
		sf_toplevel_set_fullscreen(toplevel, false, NULL);
	}
	sf_arrange_unlink(toplevel);
	sf_arrange_float(toplevel);

	if (parent->attachment_tree == NULL) {
		return;
	}
	wlr_scene_node_reparent(&toplevel->scene_tree->node, parent->attachment_tree);
	toplevel->modal_attached_to = parent;
	if (parent->workspace != NULL && toplevel->workspace != parent->workspace) {
		/* Membership for foreign/IPC; scene follows parent. */
		toplevel->workspace = parent->workspace;
	}
	if (toplevel->scene_tree != NULL) {
		wlr_scene_node_set_enabled(&toplevel->scene_tree->node, true);
	}
	if (parent->minimized) {
		sf_toplevel_apply_shown(parent, false);
	}
	sf_toplevel_modal_layout_attachments(parent);
	/* Application waits on the dialog — hand keyboard to it when parent had focus. */
	struct sf_toplevel *focused = sf_focused_toplevel(toplevel->server);
	if (focused != NULL) {
		struct sf_toplevel *root = focused;
		while (root->modal_attached_to != NULL) {
			root = root->modal_attached_to;
		}
		struct sf_toplevel *parent_root = parent;
		while (parent_root->modal_attached_to != NULL) {
			parent_root = parent_root->modal_attached_to;
		}
		if (root == parent_root) {
			/* MRU head so input_delegate prefers this child. */
			wl_list_remove(&toplevel->link);
			wl_list_insert(&toplevel->server->toplevels, &toplevel->link);
			sf_focus_toplevel(toplevel);
		}
	}
	SF_LOGI("modal dialog attach", sf_i64("id", (int64_t)toplevel->id),
		sf_i64("parent", (int64_t)parent->id));
}

void sf_toplevel_modal_detach(struct sf_toplevel *toplevel) {
	if (toplevel == NULL || toplevel->modal_attached_to == NULL) {
		return;
	}
	struct sf_toplevel *parent = toplevel->modal_attached_to;
	struct wlr_box geom;
	sf_toplevel_layout_geom(toplevel, &geom);
	int abs_x = geom.x;
	int abs_y = geom.y;

	toplevel->modal_attached_to = NULL;
	sf_toplevel_sync_scene_layer(toplevel);
	if (toplevel->scene_tree != NULL) {
		wlr_scene_node_set_position(&toplevel->scene_tree->node, abs_x, abs_y);
	}
	sf_toplevel_set_pos(toplevel, abs_x, abs_y);
	toplevel->resting_x = abs_x;
	toplevel->resting_y = abs_y;
	sf_decor_update_geom(toplevel);
	sync_modal_shade(parent, parent_blocked(parent));
	SF_LOGI("modal dialog detach", sf_i64("id", (int64_t)toplevel->id),
		sf_i64("parent", (int64_t)parent->id));
}

void sf_toplevel_modal_release_attachments(struct sf_toplevel *parent) {
	if (parent == NULL || parent->server == NULL) {
		return;
	}
	struct sf_toplevel *t, *tmp;
	wl_list_for_each_safe(t, tmp, &parent->server->toplevels, link) {
		if (t->modal_attached_to == parent) {
			sf_toplevel_modal_detach(t);
		}
	}
	sync_modal_shade(parent, false);
}

void sf_toplevel_modal_reconcile(struct sf_toplevel *toplevel) {
	if (toplevel == NULL) {
		return;
	}
	struct sf_toplevel *want = wants_attach(toplevel) ? shell_parent(toplevel) : NULL;
	if (want == toplevel->modal_attached_to) {
		if (want != NULL) {
			sf_toplevel_modal_layout_attachments(want);
		}
		return;
	}
	if (want != NULL) {
		attach_to(toplevel, want);
	} else {
		sf_toplevel_modal_detach(toplevel);
	}
}
