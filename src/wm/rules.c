#include <string.h>
#include <wayland-server-core.h>
#include <wlr/types/wlr_xdg_decoration_v1.h>
#include <wlr/types/wlr_xdg_shell.h>

#include "config/action.h"
#include "config/config.h"
#include "config/config_lua.h"
#include "decor/decor.h"
#include "log/log.h"
#include "ssd/ssd.h"
#include "ssd/ssd_policy.h"
#include "server/server.h"
#include "toplevel/toplevel.h"
#include "wm/arrange.h"
#include "wm/rules.h"
#include "wm/wm.h"
#include "wm/workspace.h"

static bool rule_matches(const struct sf_config *cfg, const struct sf_window_rule *r,
			 const char *title, const char *app_id) {
	if (r->title != NULL && r->title[0] != '\0') {
		if (!sf_lua_pattern_match(cfg, title, r->title)) {
			return false;
		}
	}
	if (r->app_id != NULL && r->app_id[0] != '\0') {
		if (!sf_lua_pattern_match(cfg, app_id, r->app_id)) {
			return false;
		}
	}
	return true;
}

static int rule_specificity(const struct sf_window_rule *r) {
	int n = 0;
	if (r->title != NULL) {
		n += (int)strlen(r->title);
	}
	if (r->app_id != NULL) {
		n += (int)strlen(r->app_id);
	}
	return n;
}

const struct sf_window_rule *sf_rules_match(const struct sf_toplevel *toplevel) {
	if (toplevel == NULL || toplevel->xdg_toplevel == NULL) {
		return NULL;
	}
	const struct sf_config *cfg = sf_cfg(toplevel->server);
	if (cfg->n_window_rules == 0) {
		return NULL;
	}
	const char *title = toplevel->xdg_toplevel->title;
	const char *app_id = toplevel->xdg_toplevel->app_id;
	const struct sf_window_rule *best = NULL;
	int best_score = -1;
	for (size_t i = 0; i < cfg->n_window_rules; i++) {
		const struct sf_window_rule *r = &cfg->window_rules[i];
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

bool sf_rules_wants_center(const struct sf_toplevel *toplevel) {
	const struct sf_window_rule *r = sf_rules_match(toplevel);
	return r != NULL && r->center_set && r->center;
}

bool sf_rules_wants_float(const struct sf_toplevel *toplevel) {
	const struct sf_window_rule *r = sf_rules_match(toplevel);
	return r != NULL && r->float_set && r->float_win;
}

static bool rule_field_still_possible(const struct sf_toplevel *toplevel, bool want_center,
				      bool want_float) {
	if (toplevel == NULL || toplevel->xdg_toplevel == NULL) {
		return false;
	}
	const struct sf_config *cfg = sf_cfg(toplevel->server);
	if (cfg->n_window_rules == 0) {
		return false;
	}
	const char *title = toplevel->xdg_toplevel->title;
	const char *app_id = toplevel->xdg_toplevel->app_id;
	bool title_known = title != NULL && title[0] != '\0';
	bool app_known = app_id != NULL && app_id[0] != '\0';

	for (size_t i = 0; i < cfg->n_window_rules; i++) {
		const struct sf_window_rule *r = &cfg->window_rules[i];
		if (want_center && (!r->center_set || !r->center)) {
			continue;
		}
		if (want_float && (!r->float_set || !r->float_win)) {
			continue;
		}
		if (!want_center && !want_float) {
			continue;
		}
		bool has_title = r->title != NULL && r->title[0] != '\0';
		bool has_app = r->app_id != NULL && r->app_id[0] != '\0';
		if (has_title && title_known && !sf_lua_pattern_match(cfg, title, r->title)) {
			continue;
		}
		if (has_app && app_known && !sf_lua_pattern_match(cfg, app_id, r->app_id)) {
			continue;
		}
		/* Ruled out only when a required field is known and mismatches.
		 * Missing fields keep the rule possible (SoftPlace wait). */
		if ((has_title && !title_known) || (has_app && !app_known)) {
			return true;
		}
	}
	return false;
}

bool sf_rules_center_still_possible(const struct sf_toplevel *toplevel) {
	return rule_field_still_possible(toplevel, true, false);
}

bool sf_rules_float_still_possible(const struct sf_toplevel *toplevel) {
	return rule_field_still_possible(toplevel, false, true);
}

void sf_rules_apply(struct sf_toplevel *toplevel) {
	if (toplevel == NULL || toplevel->xdg_toplevel == NULL) {
		return;
	}
	const struct sf_window_rule *r = sf_rules_match(toplevel);

	bool borderless = false;
	bool ssd_set = false;
	enum sf_ssd_mode ssd_mode = SF_SSD_NEVER;
	if (r != NULL) {
		const char *app = toplevel->xdg_toplevel->app_id != NULL
					  ? toplevel->xdg_toplevel->app_id
					  : "";
		const char *title =
			toplevel->xdg_toplevel->title != NULL ? toplevel->xdg_toplevel->title : "";
		SF_LOGI("window_rule", sf_str("id", r->id), sf_str("app_id", app),
			sf_str("title", title));
		if (r->borderless_set) {
			borderless = r->borderless;
		}
		if (r->always_on_top_set) {
			sf_toplevel_set_always_on_top(toplevel, r->always_on_top);
		}
		if (r->ssd_set) {
			ssd_set = true;
			ssd_mode = r->ssd_mode;
		}
		if (r->float_set && r->float_win) {
			/* Sticky tile opt-out; restore pre-tile size if we were packed. */
			sf_arrange_float_from_rule(toplevel);
		}
		if ((r->width_set || r->height_set) && toplevel->xdg_toplevel->base->initialized) {
			int w = r->width_set ? r->width : 0;
			int h = r->height_set ? r->height : 0;
			if (!r->width_set || !r->height_set) {
				struct wlr_box geom;
				sf_toplevel_layout_geom(toplevel, &geom);
				if (!r->width_set) {
					w = geom.width > 0 ? geom.width : 1;
				}
				if (!r->height_set) {
					h = geom.height > 0 ? geom.height : 1;
				}
			}
			wlr_xdg_toplevel_set_size(toplevel->xdg_toplevel, w, h);
		}
		/* WM actions last so maximize wins over SoftPlace sizing. */
		for (size_t i = 0; i < r->n_steps; i++) {
			SF_LOGI("window_rule action", sf_str("id", r->id),
				sf_str("action", sf_action_name(r->steps[i].action)),
				sf_str("cmd", r->steps[i].cmd != NULL ? r->steps[i].cmd : ""));
		}
		if (r->n_steps > 0) {
			sf_run_actions(toplevel->server, toplevel, r->steps, r->n_steps);
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
		toplevel->want_ssd = sf_ssd_want(ssd_mode, requested);
	}
	if (chrome_dirty) {
		if (toplevel->decoration != NULL) {
			sf_ssd_decoration_apply(toplevel, toplevel->decoration);
		} else {
			sf_ssd_update(toplevel);
			sf_decor_update_geom(toplevel);
		}
	}
}

static void consider_join_after_soft(struct sf_toplevel *toplevel) {
	if (toplevel->arrange_floating || toplevel->workspace == NULL) {
		return;
	}
	if (!sf_arrange_is_tiling(sf_workspace_arrange(toplevel->workspace))) {
		return;
	}
	if (sf_rules_float_still_possible(toplevel) || sf_rules_wants_float(toplevel)) {
		return;
	}
	sf_arrange_remember_float_box(toplevel);
	sf_arrange_join_toplevel(toplevel);
	sf_arrange_schedule(toplevel->workspace);
}

void sf_rules_consider_place(struct sf_toplevel *toplevel) {
	if (toplevel == NULL || toplevel->scene_tree == NULL) {
		return;
	}
	if (sf_layout_sm_is_fixed(&toplevel->layout)) {
		sf_toplevel_try_appear(toplevel);
		return;
	}
	if (toplevel->minimized || toplevel->anim_hide) {
		return;
	}
	/* Soft already dropped: still re-apply rules on late identity (title/app_id)
	 * so a float match or deferred tile join is not skipped forever. */
	if (!sf_place_sm_is_soft(&toplevel->place)) {
		sf_rules_apply(toplevel);
		if (sf_layout_sm_is_fixed(&toplevel->layout)) {
			sf_toplevel_clear_auto_place(toplevel);
			return;
		}
		consider_join_after_soft(toplevel);
		sf_toplevel_try_appear(toplevel);
		return;
	}

	sf_rules_apply(toplevel);

	/* Maximize/snap from :action owns geometry — skip SoftPlace center. */
	if (sf_layout_sm_is_fixed(&toplevel->layout)) {
		sf_toplevel_clear_auto_place(toplevel);
		return;
	}

	/* Float decided (or ruled out): join strays that were waiting on SoftPlace. */
	consider_join_after_soft(toplevel);

	if (!sf_rules_wants_center(toplevel)) {
		/* Keep soft while center *or* float identity is still pending — dropping
		 * soft early makes the !soft path the only chance to join/float, and a
		 * late title float rule would never run if we also skipped rules_apply. */
		if (!sf_rules_center_still_possible(toplevel) &&
		    !sf_rules_float_still_possible(toplevel)) {
			sf_toplevel_place_drop_soft(toplevel);
		}
		sf_toplevel_try_appear(toplevel);
		return;
	}

	sf_toplevel_place_soft_center(toplevel);
	sf_toplevel_place_transient(toplevel);
	sf_toplevel_try_appear(toplevel);
}

void sf_rules_reapply_all(struct sf_server *server) {
	if (server == NULL) {
		return;
	}
	struct sf_toplevel *t;
	wl_list_for_each(t, &server->toplevels, link) {
		sf_rules_apply(t);
		if (sf_place_sm_is_soft(&t->place) || sf_place_sm_is_managed(&t->place)) {
			sf_rules_consider_place(t);
		}
	}
}
