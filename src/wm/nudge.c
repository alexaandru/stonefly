#include "wm/wm.h"

#include <strings.h>
#include <scenefx/types/wlr_scene.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/box.h>

#include "anim/anim.h"
#include "config/config.h"
#include "wm/scratchpad.h"

#define SF_NUDGE_MIN_SIZE 50

enum sf_nudge_dir {
	SF_NUDGE_LEFT = 0,
	SF_NUDGE_RIGHT,
	SF_NUDGE_UP,
	SF_NUDGE_DOWN,
};

static bool parse_dir(const char *dir, enum sf_nudge_dir *out) {
	if (dir == NULL || out == NULL) {
		return false;
	}
	if (strcasecmp(dir, "left") == 0) {
		*out = SF_NUDGE_LEFT;
		return true;
	}
	if (strcasecmp(dir, "right") == 0) {
		*out = SF_NUDGE_RIGHT;
		return true;
	}
	if (strcasecmp(dir, "up") == 0) {
		*out = SF_NUDGE_UP;
		return true;
	}
	if (strcasecmp(dir, "down") == 0) {
		*out = SF_NUDGE_DOWN;
		return true;
	}
	return false;
}

static bool nudge_allowed(const struct sf_toplevel *toplevel) {
	if (toplevel == NULL || toplevel->scene_tree == NULL || toplevel->minimized) {
		return false;
	}
	if (sf_toplevel_is_scratchpad(toplevel)) {
		return true;
	}
	if (toplevel->fullscreen || toplevel->maximized_edges || toplevel->maximized_gaps ||
	    toplevel->snapped != SF_SNAP_NONE) {
		return false;
	}
	return true;
}

static int nudge_step(struct sf_server *server) {
	int step = sf_cfg(server)->nudge_step;
	return step > 0 ? step : 1;
}

static void apply_box_instant(struct sf_toplevel *toplevel, const struct wlr_box *box) {
	sf_anim_cancel(toplevel, false);
	wlr_scene_node_set_position(&toplevel->scene_tree->node, box->x, box->y);
	toplevel->resting_x = box->x;
	toplevel->resting_y = box->y;
	if (toplevel->xdg_toplevel != NULL && toplevel->xdg_toplevel->base->initialized) {
		wlr_xdg_toplevel_set_size(toplevel->xdg_toplevel, box->width, box->height);
	}
}

void sf_toplevel_nudge(struct sf_toplevel *toplevel, const char *dir) {
	enum sf_nudge_dir d;
	if (!nudge_allowed(toplevel) || !parse_dir(dir, &d)) {
		return;
	}
	int step = nudge_step(toplevel->server);
	struct wlr_box geom;
	sf_toplevel_layout_geom(toplevel, &geom);
	if (geom.width <= 0 || geom.height <= 0) {
		return;
	}
	switch (d) {
	case SF_NUDGE_LEFT:
		geom.x -= step;
		break;
	case SF_NUDGE_RIGHT:
		geom.x += step;
		break;
	case SF_NUDGE_UP:
		geom.y -= step;
		break;
	case SF_NUDGE_DOWN:
		geom.y += step;
		break;
	}
	sf_toplevel_clear_auto_place(toplevel);
	apply_box_instant(toplevel, &geom);
	sf_toplevel_ensure_on_screen(toplevel);
}

void sf_toplevel_resize_edge(struct sf_toplevel *toplevel, const char *dir) {
	enum sf_nudge_dir d;
	if (!nudge_allowed(toplevel) || !parse_dir(dir, &d)) {
		return;
	}
	int step = nudge_step(toplevel->server);
	struct wlr_box geom;
	sf_toplevel_layout_geom(toplevel, &geom);
	if (geom.width <= 0 || geom.height <= 0) {
		return;
	}
	/* Right/down grow; left/up shrink (top-left anchored). */
	switch (d) {
	case SF_NUDGE_LEFT:
		geom.width -= step;
		break;
	case SF_NUDGE_RIGHT:
		geom.width += step;
		break;
	case SF_NUDGE_UP:
		geom.height -= step;
		break;
	case SF_NUDGE_DOWN:
		geom.height += step;
		break;
	}
	if (geom.width < SF_NUDGE_MIN_SIZE) {
		geom.width = SF_NUDGE_MIN_SIZE;
	}
	if (geom.height < SF_NUDGE_MIN_SIZE) {
		geom.height = SF_NUDGE_MIN_SIZE;
	}
	sf_toplevel_clear_auto_place(toplevel);
	apply_box_instant(toplevel, &geom);
	sf_toplevel_ensure_on_screen(toplevel);
}
