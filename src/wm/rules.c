#include <string.h>
#include <wayland-server-core.h>
#include <wlr/types/wlr_xdg_decoration_v1.h>
#include <wlr/types/wlr_xdg_shell.h>

#include "config/config.h"
#include "config/config_lua.h"
#include "decor/decor.h"
#include "ssd/ssd.h"
#include "ssd/ssd_policy.h"
#include "server/server.h"
#include "wm/rules.h"
#include "wm/wm.h"
#include "toplevel/toplevel.h"

static bool rule_matches(const struct stonefly_config *cfg, const struct stonefly_window_rule *r,
			 const char *title, const char *app_id) {
	if (r->title != NULL && r->title[0] != '\0') {
		if (!stonefly_lua_pattern_match(cfg, title, r->title)) {
			return false;
		}
	}
	if (r->app_id != NULL && r->app_id[0] != '\0') {
		if (!stonefly_lua_pattern_match(cfg, app_id, r->app_id)) {
			return false;
		}
	}
	return true;
}

static int rule_specificity(const struct stonefly_window_rule *r) {
	int n = 0;
	if (r->title != NULL) {
		n += (int)strlen(r->title);
	}
	if (r->app_id != NULL) {
		n += (int)strlen(r->app_id);
	}
	return n;
}

const struct stonefly_window_rule *stonefly_rules_match(const struct stonefly_toplevel *toplevel) {
	if (toplevel == NULL || toplevel->xdg_toplevel == NULL) {
		return NULL;
	}
	const struct stonefly_config *cfg = stonefly_cfg(toplevel->server);
	if (cfg->n_window_rules == 0) {
		return NULL;
	}
	const char *title = toplevel->xdg_toplevel->title;
	const char *app_id = toplevel->xdg_toplevel->app_id;
	const struct stonefly_window_rule *best = NULL;
	int best_score = -1;
	for (size_t i = 0; i < cfg->n_window_rules; i++) {
		const struct stonefly_window_rule *r = &cfg->window_rules[i];
		if (!rule_matches(cfg, r, title, app_id)) {
			continue;
		}
		int score = rule_specificity(r);
		if (score > best_score) {
			best_score = score;
			best = r;
		}
	}
	return best;
}

bool stonefly_rules_wants_center(const struct stonefly_toplevel *toplevel) {
	const struct stonefly_window_rule *r = stonefly_rules_match(toplevel);
	return r != NULL && r->center_set && r->center;
}

void stonefly_rules_apply(struct stonefly_toplevel *toplevel) {
	if (toplevel == NULL || toplevel->xdg_toplevel == NULL) {
		return;
	}
	const struct stonefly_window_rule *r = stonefly_rules_match(toplevel);

	bool borderless = false;
	bool ssd_set = false;
	int ssd_mode = 0;
	if (r != NULL) {
		if (r->borderless_set) {
			borderless = r->borderless;
		}
		if (r->ssd_set) {
			ssd_set = true;
			ssd_mode = r->ssd_mode;
		}
		if (r->float_set && r->float_win) {
			stonefly_toplevel_float_for_resize(toplevel);
		}
		if ((r->width_set || r->height_set) && toplevel->xdg_toplevel->base->initialized) {
			int w = r->width_set ? r->width : 0;
			int h = r->height_set ? r->height : 0;
			if (!r->width_set || !r->height_set) {
				struct wlr_box geom;
				stonefly_toplevel_layout_geom(toplevel, &geom);
				if (!r->width_set) {
					w = geom.width > 0 ? geom.width : 1;
				}
				if (!r->height_set) {
					h = geom.height > 0 ? geom.height : 1;
				}
			}
			wlr_xdg_toplevel_set_size(toplevel->xdg_toplevel, w, h);
		}
	}

	bool chrome_dirty = toplevel->borderless != borderless ||
			    toplevel->rule_ssd_set != ssd_set ||
			    (ssd_set && toplevel->rule_ssd_mode != ssd_mode);
	toplevel->borderless = borderless;
	toplevel->rule_ssd_set = ssd_set;
	toplevel->rule_ssd_mode = ssd_mode;
	if (ssd_set) {
		/* Apply even before xdg-decoration binds (force never/always). */
		int requested = WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_NONE;
		if (toplevel->decoration != NULL) {
			requested = (int)toplevel->decoration->requested_mode;
		}
		toplevel->want_ssd = stonefly_ssd_want((enum stonefly_ssd_mode)ssd_mode, requested);
	}
	if (chrome_dirty) {
		if (toplevel->decoration != NULL) {
			stonefly_ssd_decoration_apply(toplevel, toplevel->decoration);
		} else {
			stonefly_ssd_update(toplevel);
			stonefly_decor_update_geom(toplevel);
		}
	}
}

void stonefly_rules_consider_place(struct stonefly_toplevel *toplevel) {
	if (toplevel == NULL || toplevel->scene_tree == NULL) {
		return;
	}
	if (toplevel->maximized_edges || toplevel->maximized_gaps ||
	    toplevel->snapped != STONEFLY_SNAP_NONE || toplevel->fullscreen) {
		return;
	}
	if (toplevel->minimized || toplevel->anim_hide) {
		return;
	}
	if (!toplevel->soft_place) {
		return;
	}

	stonefly_rules_apply(toplevel);

	if (!stonefly_rules_wants_center(toplevel)) {
		const char *title =
			toplevel->xdg_toplevel != NULL ? toplevel->xdg_toplevel->title : NULL;
		const char *app =
			toplevel->xdg_toplevel != NULL ? toplevel->xdg_toplevel->app_id : NULL;
		if (title != NULL && title[0] != '\0' && app != NULL && app[0] != '\0') {
			toplevel->soft_place = false;
		}
		return;
	}

	toplevel->auto_place = true;
	stonefly_toplevel_place_transient(toplevel);
}

void stonefly_rules_reapply_all(struct stonefly_server *server) {
	if (server == NULL) {
		return;
	}
	struct stonefly_toplevel *t;
	wl_list_for_each(t, &server->toplevels, link) {
		stonefly_rules_apply(t);
		if (t->soft_place || t->auto_place) {
			stonefly_rules_consider_place(t);
		}
	}
}
