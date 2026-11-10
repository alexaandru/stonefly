#include "wm/wm.h"

#include <float.h>
#include <string.h>
#include <strings.h>
#include <wlr/types/wlr_output.h>
#include <scenefx/types/wlr_scene.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/box.h>

#include "anim/anim.h"
#include "config/config.h"
#include "layer/layer.h"
#include "log/log.h"
#include "output/output.h"
#include "seat/seat.h"
#include "ssd/ssd.h"
#include "toplevel/foreign.h"
#include "wm/geom.h"
#include "wm/scratchpad.h"
#include "wm/workspace.h"

static bool output_live(const struct sf_output *output) {
	return output != NULL && output->wlr_output != NULL && output->full.width > 0 &&
	       output->full.height > 0;
}

static int live_output_count(struct sf_server *server) {
	int n = 0;
	struct sf_output *o;
	wl_list_for_each(o, &server->outputs, link) {
		if (output_live(o)) {
			n++;
		}
	}
	return n;
}

static struct sf_output *live_output_at(struct sf_server *server, int index) {
	int i = 0;
	struct sf_output *o;
	wl_list_for_each(o, &server->outputs, link) {
		if (!output_live(o)) {
			continue;
		}
		if (i == index) {
			return o;
		}
		i++;
	}
	return NULL;
}

static int live_output_index(struct sf_server *server, const struct sf_output *want) {
	int i = 0;
	struct sf_output *o;
	wl_list_for_each(o, &server->outputs, link) {
		if (!output_live(o)) {
			continue;
		}
		if (o == want) {
			return i;
		}
		i++;
	}
	return 0;
}

static struct sf_output *cycle_output(struct sf_server *server, struct sf_output *from, int delta) {
	int n = live_output_count(server);
	if (n <= 0) {
		return NULL;
	}
	int idx = live_output_index(server, from);
	idx = (idx + delta % n + n) % n;
	return live_output_at(server, idx);
}

static struct sf_output *output_in_direction(struct sf_output *from, const char *dir,
					     struct sf_server *server) {
	if (!output_live(from)) {
		return NULL;
	}
	double fx = from->full.x + from->full.width / 2.0;
	double fy = from->full.y + from->full.height / 2.0;
	struct sf_output *best = NULL;
	double best_dist = DBL_MAX;
	struct sf_output *head;
	wl_list_for_each(head, &server->outputs, link) {
		if (!output_live(head) || head == from) {
			continue;
		}
		if (strcasecmp(dir, "left") == 0) {
			if (head->full.x + head->full.width > from->full.x) {
				continue;
			}
		} else if (strcasecmp(dir, "right") == 0) {
			if (head->full.x < from->full.x + from->full.width) {
				continue;
			}
		} else if (strcasecmp(dir, "up") == 0) {
			if (head->full.y + head->full.height > from->full.y) {
				continue;
			}
		} else if (strcasecmp(dir, "down") == 0) {
			if (head->full.y < from->full.y + from->full.height) {
				continue;
			}
		} else {
			continue;
		}
		double ox = head->full.x + head->full.width / 2.0;
		double oy = head->full.y + head->full.height / 2.0;
		double dx = ox - fx;
		double dy = oy - fy;
		double dist = dx * dx + dy * dy;
		if (dist < best_dist) {
			best_dist = dist;
			best = head;
		}
	}
	return best;
}

static struct sf_output *output_by_name(struct sf_server *server, const char *name) {
	struct sf_output *o;
	wl_list_for_each(o, &server->outputs, link) {
		if (!output_live(o) || o->wlr_output->name == NULL) {
			continue;
		}
		if (strcasecmp(o->wlr_output->name, name) == 0) {
			return o;
		}
	}
	return NULL;
}

struct sf_output *sf_output_resolve(struct sf_server *server, struct sf_output *from,
				    const char *target) {
	if (server == NULL || target == NULL || target[0] == '\0') {
		return NULL;
	}
	if (live_output_count(server) <= 0) {
		return NULL;
	}
	if (!output_live(from)) {
		from = sf_output_at_cursor(server);
	}
	if (!output_live(from)) {
		from = live_output_at(server, 0);
	}
	if (strcasecmp(target, "left") == 0 || strcasecmp(target, "right") == 0 ||
	    strcasecmp(target, "up") == 0 || strcasecmp(target, "down") == 0) {
		return output_in_direction(from, target, server);
	}
	if (strcasecmp(target, "next") == 0) {
		return cycle_output(server, from, 1);
	}
	if (strcasecmp(target, "prev") == 0 || strcasecmp(target, "previous") == 0) {
		return cycle_output(server, from, -1);
	}
	if (strcasecmp(target, "current") == 0) {
		return from;
	}
	return output_by_name(server, target);
}

static bool toplevel_center_on_output(struct sf_toplevel *toplevel, struct sf_output *output) {
	struct wlr_box geom;
	sf_toplevel_layout_geom(toplevel, &geom);
	if (geom.width <= 0 || geom.height <= 0 || !output_live(output)) {
		return false;
	}
	double cx = geom.x + geom.width / 2.0;
	double cy = geom.y + geom.height / 2.0;
	return cx >= output->full.x && cy >= output->full.y &&
	       cx < output->full.x + output->full.width &&
	       cy < output->full.y + output->full.height;
}

static struct sf_toplevel *top_toplevel_on_output(struct sf_server *server,
						  struct sf_output *output) {
	struct sf_toplevel *t;
	wl_list_for_each(t, &server->toplevels, link) {
		if (t->minimized || sf_toplevel_is_scratchpad(t)) {
			continue;
		}
		if (!sf_toplevel_workspace_visible(t)) {
			continue;
		}
		if (sf_output_for_toplevel(t) != output) {
			continue;
		}
		return t;
	}
	wl_list_for_each(t, &server->toplevels, link) {
		if (t->minimized || !sf_toplevel_is_scratchpad(t)) {
			continue;
		}
		if (sf_output_for_toplevel(t) == output) {
			return t;
		}
	}
	return NULL;
}

static void park_on_usable(struct sf_toplevel *toplevel, const struct wlr_box *usable) {
	struct wlr_box geom;
	sf_toplevel_layout_geom(toplevel, &geom);
	int w = geom.width > 0 ? geom.width : 1;
	int h = geom.height > 0 ? geom.height : 1;
	int x = usable->x + (usable->width - w) / 2;
	int y = usable->y + (usable->height - h) / 2;
	sf_anim_cancel(toplevel, false);
	if (toplevel->scene_tree != NULL) {
		wlr_scene_node_set_position(&toplevel->scene_tree->node, x, y);
	}
	toplevel->resting_x = x;
	toplevel->resting_y = y;
}

static void center_floating_on_usable(struct sf_toplevel *toplevel, const struct wlr_box *usable) {
	struct wlr_box area = *usable;
	sf_box_inset_gap(&area, sf_cfg(toplevel->server)->gap_size);
	int top_inset = sf_ssd_top_inset(toplevel);
	if (area.height > top_inset) {
		area.y += top_inset;
		area.height -= top_inset;
	}
	if (area.width <= 0 || area.height <= 0) {
		return;
	}
	struct wlr_box geom;
	sf_toplevel_layout_geom(toplevel, &geom);
	int w = geom.width > 0 ? geom.width : (area.width < 800 ? area.width : 800);
	int h = geom.height > 0 ? geom.height : (area.height < 600 ? area.height : 600);
	if (w > area.width) {
		w = area.width;
	}
	if (h > area.height) {
		h = area.height;
	}
	struct wlr_box to = {
		.x = area.x + (area.width - w) / 2,
		.y = area.y + (area.height - h) / 2,
		.width = w,
		.height = h,
	};
	sf_toplevel_clear_auto_place(toplevel);
	if (geom.width > 0 && geom.height > 0 && sf_anim_start_morph(toplevel, &geom, &to)) {
		return;
	}
	sf_anim_cancel(toplevel, false);
	if (toplevel->scene_tree != NULL) {
		wlr_scene_node_set_position(&toplevel->scene_tree->node, to.x, to.y);
	}
	toplevel->resting_x = to.x;
	toplevel->resting_y = to.y;
	if (toplevel->xdg_toplevel != NULL && toplevel->xdg_toplevel->base->initialized &&
	    (geom.width != to.width || geom.height != to.height)) {
		wlr_xdg_toplevel_set_size(toplevel->xdg_toplevel, to.width, to.height);
	}
}

void sf_toplevel_place_on_output(struct sf_toplevel *toplevel, struct sf_output *output) {
	if (toplevel == NULL || !output_live(output) || sf_toplevel_is_scratchpad(toplevel)) {
		return;
	}
	if (output->active == NULL) {
		return;
	}
	struct wlr_box usable;
	if (!sf_output_usable_box(output, &usable) || usable.width <= 0 || usable.height <= 0) {
		return;
	}

	sf_workspace_assign(toplevel, output->active);

	bool max_edges = toplevel->maximized_edges;
	bool max_gaps = toplevel->maximized_gaps;
	bool fullscreen = toplevel->fullscreen;
	bool snapped = toplevel->snapped != SF_SNAP_NONE;

	if (snapped && !max_edges && !max_gaps && !fullscreen) {
		sf_toplevel_layout_float(toplevel, false);
	}

	if (fullscreen) {
		park_on_usable(toplevel, &usable);
		/* Drop then re-apply so the SM retargets onto the new output. */
		if (toplevel->fullscreen) {
			sf_toplevel_set_fullscreen(toplevel, false, NULL);
		}
		sf_toplevel_set_fullscreen(toplevel, true, output->wlr_output);
		sf_foreign_toplevel_on_outputs_change(toplevel);
		return;
	}
	if (max_edges) {
		park_on_usable(toplevel, &usable);
		sf_toplevel_layout_float(toplevel, false);
		sf_toplevel_set_max_edges(toplevel, true);
		sf_foreign_toplevel_on_outputs_change(toplevel);
		return;
	}
	if (max_gaps) {
		park_on_usable(toplevel, &usable);
		sf_toplevel_layout_float(toplevel, false);
		sf_toplevel_set_max_gaps(toplevel, true);
		sf_foreign_toplevel_on_outputs_change(toplevel);
		return;
	}

	if (toplevel_center_on_output(toplevel, output)) {
		sf_foreign_toplevel_on_outputs_change(toplevel);
		return;
	}
	center_floating_on_usable(toplevel, &usable);
	sf_foreign_toplevel_on_outputs_change(toplevel);
}

void sf_focus_monitor(struct sf_server *server, const char *target) {
	if (server == NULL) {
		return;
	}
	struct sf_output *from = sf_output_at_cursor(server);
	struct sf_toplevel *focused = sf_focused_toplevel(server);
	if (focused != NULL) {
		struct sf_output *fo = sf_output_for_toplevel(focused);
		if (output_live(fo)) {
			from = fo;
		}
	}
	struct sf_output *out = sf_output_resolve(server, from, target);
	if (out == NULL) {
		SF_LOGW("focus_monitor", sf_str("target", target != NULL ? target : ""));
		return;
	}
	double cx = out->full.x + out->full.width / 2.0;
	double cy = out->full.y + out->full.height / 2.0;
	sf_pointer_warp_to(server, cx, cy);

	struct sf_toplevel *top = top_toplevel_on_output(server, out);
	if (top != NULL) {
		sf_focus_toplevel(top);
		sf_pointer_follow_focus(server);
	}
}

void sf_send_to_monitor(struct sf_server *server, struct sf_toplevel *toplevel,
			const char *target) {
	if (server == NULL || toplevel == NULL || sf_toplevel_is_scratchpad(toplevel)) {
		return;
	}
	struct sf_output *from = sf_output_for_toplevel(toplevel);
	struct sf_output *out = sf_output_resolve(server, from, target);
	if (out == NULL) {
		SF_LOGW("send_to_monitor", sf_str("target", target != NULL ? target : ""));
		return;
	}
	sf_toplevel_place_on_output(toplevel, out);
	sf_focus_toplevel(toplevel);
	/* Always land the pointer on the moved window (peeble send path). */
	sf_pointer_center(server, toplevel);
}
