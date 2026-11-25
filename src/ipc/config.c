#include "ipc/config.h"

#include <stdio.h>
#include <string.h>
#include <xkbcommon/xkbcommon.h>

#include "config/action.h"
#include "config/config.h"
#include "config/gesture.h"
#include "config/mousebind.h"
#include "config/wheelbind.h"
#include "server/server.h"
#include "ssd/ssd_policy.h"
#include "util/color.h"
#include "util/json.h"
#include "wm/arrange_math.h"
#include "wm/workspace.h"

static void json_rgba(struct json_buf *b, const struct sf_rgba *c) {
	json_byte(b, '[');
	json_f64(b, c->r);
	json_byte(b, ',');
	json_f64(b, c->g);
	json_byte(b, ',');
	json_f64(b, c->b);
	json_byte(b, ',');
	json_f64(b, c->a);
	json_byte(b, ']');
}

static void json_mods(struct json_buf *b, uint32_t mods) {
	json_byte(b, '[');
	bool first = true;
	struct {
		uint32_t bit;
		const char *name;
	} tab[] = {
		{SF_SUPER, "super"},
		{SF_SHIFT, "shift"},
		{SF_CTRL, "ctrl"},
		{SF_ALT, "alt"},
	};
	for (size_t i = 0; i < sizeof tab / sizeof tab[0]; i++) {
		if ((mods & tab[i].bit) == 0) {
			continue;
		}
		if (!first) {
			json_byte(b, ',');
		}
		first = false;
		json_str(b, tab[i].name);
	}
	json_byte(b, ']');
}

static void json_keysym(struct json_buf *b, xkb_keysym_t sym) {
	char name[64];
	if (sym == 0 || xkb_keysym_get_name(sym, name, sizeof name) < 0) {
		json_str(b, "");
		return;
	}
	json_str(b, name);
}

static void emit_anim_op(struct json_buf *b, const struct sf_anim_op *op) {
	json_byte(b, '{');
	json_key(b, "enabled");
	json_bool(b, op->enabled);
	json_byte(b, ',');
	json_key(b, "duration_ms");
	json_i64(b, op->duration_ms);
	json_byte(b, ',');
	json_key(b, "style");
	json_str(b, sf_anim_style_name(op->style));
	json_byte(b, ',');
	json_key(b, "scale");
	json_f64(b, op->scale);
	json_byte(b, '}');
}

static void emit_options(struct json_buf *b, const struct sf_config *cfg) {
	json_byte(b, '{');

	json_key(b, "scale");
	json_f64(b, cfg->output_scale);
	json_byte(b, ',');
	json_key(b, "cursor_size");
	json_i64(b, cfg->cursor_size_default);
	json_byte(b, ',');
	json_key(b, "gap");
	json_i64(b, cfg->gap_size);
	json_byte(b, ',');
	json_key(b, "cascade_step");
	json_i64(b, cfg->cascade_step);
	json_byte(b, ',');
	json_key(b, "cascade_wrap");
	json_i64(b, cfg->cascade_wrap);
	json_byte(b, ',');
	json_key(b, "slide_duration_ms");
	json_i64(b, cfg->slide_duration_ms);
	json_byte(b, ',');
	json_key(b, "slide_dir_closest");
	json_bool(b, cfg->slide_dir_closest);
	json_byte(b, ',');
	json_key(b, "morph_duration_ms");
	json_i64(b, cfg->morph_duration_ms);
	json_byte(b, ',');
	json_key(b, "client_opacity");
	json_f64(b, cfg->client_opacity);
	json_byte(b, ',');
	json_key(b, "inactive_opacity");
	json_f64(b, cfg->inactive_opacity);
	json_byte(b, ',');
	json_key(b, "dim_inactive");
	json_f64(b, cfg->dim_inactive);
	json_byte(b, ',');
	json_key(b, "dim_inactive_color");
	json_rgba(b, &cfg->dim_inactive_color);
	json_byte(b, ',');
	json_key(b, "anim");
	json_byte(b, '{');
	json_key(b, "enabled");
	json_bool(b, cfg->anim.enabled);
	json_byte(b, ',');
	json_key(b, "appear");
	emit_anim_op(b, &cfg->anim.appear);
	json_byte(b, ',');
	json_key(b, "close");
	emit_anim_op(b, &cfg->anim.close);
	json_byte(b, ',');
	json_key(b, "minimize");
	emit_anim_op(b, &cfg->anim.minimize);
	json_byte(b, ',');
	json_key(b, "scratch");
	emit_anim_op(b, &cfg->anim.scratch);
	json_byte(b, ',');
	json_key(b, "morph");
	emit_anim_op(b, &cfg->anim.morph);
	json_byte(b, ',');
	json_key(b, "workspace");
	emit_anim_op(b, &cfg->anim.workspace);
	json_byte(b, '}');
	json_byte(b, ',');
	json_key(b, "corner_radius");
	json_i64(b, cfg->corner_radius);
	json_byte(b, ',');
	json_key(b, "border_thickness");
	json_i64(b, cfg->border_thickness);
	json_byte(b, ',');
	json_key(b, "shadow_blur_sigma");
	json_f64(b, cfg->shadow_blur_sigma);
	json_byte(b, ',');
	json_key(b, "border_active");
	json_rgba(b, &cfg->border_active);
	json_byte(b, ',');
	json_key(b, "border_inactive");
	json_rgba(b, &cfg->border_inactive);
	json_byte(b, ',');
	json_key(b, "border_urgent");
	json_rgba(b, &cfg->border_urgent);
	json_byte(b, ',');
	json_key(b, "border_float_active");
	json_rgba(b, &cfg->border_float_active);
	json_byte(b, ',');
	json_key(b, "border_float_inactive");
	json_rgba(b, &cfg->border_float_inactive);
	json_byte(b, ',');
	json_key(b, "shadow_color");
	json_rgba(b, &cfg->shadow_color);
	json_byte(b, ',');
	json_key(b, "window_blur");
	json_bool(b, cfg->window_blur);
	json_byte(b, ',');
	json_key(b, "blur_xray");
	json_bool(b, cfg->blur_xray);
	json_byte(b, ',');
	json_key(b, "blur_num_passes");
	json_i64(b, cfg->blur_num_passes);
	json_byte(b, ',');
	json_key(b, "blur_radius");
	json_i64(b, cfg->blur_radius);
	json_byte(b, ',');
	json_key(b, "blur_noise");
	json_f64(b, cfg->blur_noise);
	json_byte(b, ',');
	json_key(b, "blur_brightness");
	json_f64(b, cfg->blur_brightness);
	json_byte(b, ',');
	json_key(b, "blur_contrast");
	json_f64(b, cfg->blur_contrast);
	json_byte(b, ',');
	json_key(b, "blur_saturation");
	json_f64(b, cfg->blur_saturation);
	json_byte(b, ',');
	json_key(b, "bell_flash_ms");
	json_i64(b, cfg->bell_flash_ms);
	json_byte(b, ',');
	json_key(b, "snap_threshold");
	json_i64(b, cfg->snap_threshold);
	json_byte(b, ',');
	json_key(b, "snap_preview_border");
	json_i64(b, cfg->snap_preview_border);
	json_byte(b, ',');
	json_key(b, "snap_preview_opacity");
	json_f64(b, cfg->snap_preview_opacity);
	json_byte(b, ',');
	json_key(b, "tap_max_hold_ms");
	json_i64(b, cfg->tap_max_hold_ms);
	json_byte(b, ',');
	json_key(b, "double_tap_gap_ms");
	json_i64(b, cfg->double_tap_gap_ms);
	json_byte(b, ',');
	json_key(b, "wheel_bind_cooldown_ms");
	json_i64(b, cfg->wheel_bind_cooldown_ms);
	json_byte(b, ',');
	json_key(b, "arrange");
	json_str(b, sf_arrange_name(cfg->arrange));
	json_byte(b, ',');
	json_key(b, "master_pos");
	json_str(b, sf_master_pos_name(cfg->master_pos));
	json_byte(b, ',');
	json_key(b, "master_factor");
	json_f64(b, (double)cfg->master_factor);
	json_byte(b, ',');
	json_key(b, "dwindle_factor");
	json_f64(b, (double)cfg->dwindle_factor);
	json_byte(b, ',');
	json_key(b, "master_count");
	json_i64(b, cfg->master_count);
	json_byte(b, ',');
	json_key(b, "column_ratio");
	json_f64(b, (double)cfg->column_ratio);
	json_byte(b, ',');
	json_key(b, "swipe_workspace_threshold");
	json_i64(b, cfg->swipe_workspace_threshold);
	json_byte(b, ',');
	json_key(b, "swipe_workspace_natural");
	json_bool(b, cfg->swipe_workspace_natural);
	json_byte(b, ',');
	json_key(b, "cursor_follows_focus");
	json_bool(b, cfg->cursor_follows_focus);
	json_byte(b, ',');
	json_key(b, "nudge_step");
	json_i64(b, cfg->nudge_step);
	json_byte(b, ',');
	json_key(b, "ssd_mode");
	json_str(b, sf_ssd_mode_name(cfg->ssd_mode));
	json_byte(b, ',');
	json_key(b, "client_maximize");
	json_str(b, sf_client_maximize_name(cfg->client_maximize));
	json_byte(b, ',');
	json_key(b, "unminimize_workspace");
	json_str(b, sf_unminimize_workspace_name(cfg->unminimize_workspace));
	json_byte(b, ',');
	json_key(b, "focus_cycle");
	json_str(b, sf_focus_cycle_name(cfg->focus_cycle));
	json_byte(b, ',');
	json_key(b, "tile_insert");
	json_str(b, sf_tile_insert_name(cfg->tile_insert));
	json_byte(b, ',');
	json_key(b, "column_drop_seam");
	json_f64(b, (double)cfg->column_drop_seam);
	json_byte(b, ',');
	json_key(b, "tiled_grab_remap");
	json_bool(b, cfg->tiled_grab_remap);
	json_byte(b, ',');
	json_key(b, "border_resize");
	json_bool(b, cfg->border_resize);
	json_byte(b, ',');
	json_key(b, "resize_border_in");
	json_i64(b, cfg->resize_border_in);
	json_byte(b, ',');
	json_key(b, "resize_border_out");
	json_i64(b, cfg->resize_border_out);
	json_byte(b, ',');
	json_key(b, "ssd_height");
	json_i64(b, cfg->ssd_height);
	json_byte(b, ',');
	json_key(b, "ssd_font_size");
	json_i64(b, cfg->ssd_font_size);
	json_byte(b, ',');
	json_key(b, "capture_block_app_ids");
	json_str(b, cfg->capture_block_app_ids ? cfg->capture_block_app_ids : "");
	json_byte(b, ',');
	json_key(b, "config_watch");
	json_bool(b, cfg->config_watch);

	json_byte(b, '}');
}

static void emit_action_steps(struct json_buf *b, const struct sf_action_step *steps, size_t n) {
	if (n == 0) {
		json_raw(b, "null");
		return;
	}
	if (n == 1) {
		json_byte(b, '{');
		json_key(b, "action");
		json_str(b, sf_action_name(steps[0].action));
		if (steps[0].cmd != NULL && steps[0].cmd[0] != '\0') {
			json_byte(b, ',');
			json_key(b, "cmd");
			json_str(b, steps[0].cmd);
		}
		json_byte(b, '}');
		return;
	}
	json_byte(b, '[');
	for (size_t i = 0; i < n; i++) {
		if (i > 0) {
			json_byte(b, ',');
		}
		json_byte(b, '{');
		json_key(b, "action");
		json_str(b, sf_action_name(steps[i].action));
		if (steps[i].cmd != NULL && steps[i].cmd[0] != '\0') {
			json_byte(b, ',');
			json_key(b, "cmd");
			json_str(b, steps[i].cmd);
		}
		json_byte(b, '}');
	}
	json_byte(b, ']');
}

static void emit_binds(struct json_buf *b, const struct sf_config *cfg) {
	json_byte(b, '[');
	for (size_t i = 0; i < cfg->n_keybinds; i++) {
		const struct sf_keybind *k = &cfg->keybinds[i];
		if (i > 0) {
			json_byte(b, ',');
		}
		json_byte(b, '{');
		json_key(b, "mods");
		json_mods(b, k->mods);
		json_byte(b, ',');
		json_key(b, "key");
		json_keysym(b, k->sym);
		if (k->sym_alt != 0) {
			json_byte(b, ',');
			json_key(b, "key_alt");
			json_keysym(b, k->sym_alt);
		}
		json_byte(b, ',');
		json_key(b, "action");
		emit_action_steps(b, k->steps, k->n_steps);
		json_byte(b, '}');
	}
	json_byte(b, ']');
}

static void emit_mousebinds(struct json_buf *b, const struct sf_config *cfg) {
	json_byte(b, '[');
	for (size_t i = 0; i < cfg->n_mousebinds; i++) {
		const struct sf_mousebind *m = &cfg->mousebinds[i];
		if (i > 0) {
			json_byte(b, ',');
		}
		json_byte(b, '{');
		json_key(b, "mods");
		json_mods(b, m->mods);
		json_byte(b, ',');
		json_key(b, "button");
		json_str(b, sf_mousebind_button_name(m->button));
		json_byte(b, ',');
		json_key(b, "arrange");
		json_str(b, sf_arrange_name(m->arrange));
		json_byte(b, ',');
		json_key(b, "action");
		emit_action_steps(b, m->steps, m->n_steps);
		json_byte(b, '}');
	}
	json_byte(b, ']');
}

static void emit_wheelbinds(struct json_buf *b, const struct sf_config *cfg) {
	json_byte(b, '[');
	for (size_t i = 0; i < cfg->n_wheelbinds; i++) {
		const struct sf_wheelbind *w = &cfg->wheelbinds[i];
		if (i > 0) {
			json_byte(b, ',');
		}
		json_byte(b, '{');
		json_key(b, "mods");
		json_mods(b, w->mods);
		json_byte(b, ',');
		json_key(b, "dir");
		json_str(b, sf_wheelbind_dir_name(w->dir));
		json_byte(b, ',');
		json_key(b, "arrange");
		json_str(b, sf_arrange_name(w->arrange));
		json_byte(b, ',');
		json_key(b, "action");
		emit_action_steps(b, w->steps, w->n_steps);
		json_byte(b, '}');
	}
	json_byte(b, ']');
}

static void emit_taps(struct json_buf *b, const struct sf_config *cfg) {
	json_byte(b, '[');
	for (size_t i = 0; i < cfg->n_tapbinds; i++) {
		const struct sf_tapbind *t = &cfg->tapbinds[i];
		if (i > 0) {
			json_byte(b, ',');
		}
		json_byte(b, '{');
		json_key(b, "key");
		json_keysym(b, t->sym);
		if (t->sym_alt != 0) {
			json_byte(b, ',');
			json_key(b, "key_alt");
			json_keysym(b, t->sym_alt);
		}
		json_byte(b, ',');
		json_key(b, "levels");
		json_byte(b, '[');
		for (size_t j = 0; j < t->n_levels; j++) {
			const struct sf_tap_level *lv = &t->levels[j];
			if (j > 0) {
				json_byte(b, ',');
			}
			json_byte(b, '{');
			json_key(b, "count");
			json_i64(b, (int64_t)lv->count);
			json_byte(b, ',');
			json_key(b, "action");
			emit_action_steps(b, lv->steps, lv->n_steps);
			json_byte(b, '}');
		}
		json_byte(b, ']');
		json_byte(b, '}');
	}
	json_byte(b, ']');
}

static void emit_scratchpads(struct json_buf *b, const struct sf_config *cfg) {
	json_byte(b, '[');
	for (size_t i = 0; i < cfg->n_scratchpads; i++) {
		const struct sf_scratchpad_spec *s = &cfg->scratchpads[i];
		if (i > 0) {
			json_byte(b, ',');
		}
		json_byte(b, '{');
		json_key(b, "name");
		json_str(b, s->name ? s->name : "");
		json_byte(b, ',');
		json_key(b, "cmd");
		json_str(b, s->cmd ? s->cmd : "");
		json_byte(b, ',');
		json_key(b, "app_id");
		json_str(b, s->app_id ? s->app_id : "");
		json_byte(b, ',');
		json_key(b, "auto_hide");
		json_bool(b, s->auto_hide);
		json_byte(b, '}');
	}
	json_byte(b, ']');
}

static void emit_execs(struct json_buf *b, const struct sf_config *cfg) {
	json_byte(b, '[');
	for (size_t i = 0; i < cfg->n_execs; i++) {
		const struct sf_exec *e = &cfg->execs[i];
		if (i > 0) {
			json_byte(b, ',');
		}
		json_byte(b, '{');
		json_key(b, "cmd");
		json_str(b, e->cmd ? e->cmd : "");
		json_byte(b, ',');
		json_key(b, "reload");
		json_bool(b, e->reload);
		json_byte(b, '}');
	}
	json_byte(b, ']');
}

static const char *input_accel_name(enum sf_input_accel p) {
	switch (p) {
	case SF_INPUT_ACCEL_NONE:
		return "none";
	case SF_INPUT_ACCEL_FLAT:
		return "flat";
	case SF_INPUT_ACCEL_ADAPTIVE:
		return "adaptive";
	case SF_INPUT_ACCEL_UNSET:
		break;
	}
	return NULL;
}

static void emit_input_tri(struct json_buf *b, int v) {
	if (v < 0) {
		json_raw(b, "null");
	} else {
		json_bool(b, v != 0);
	}
}

static void emit_gestures(struct json_buf *b, const struct sf_config *cfg) {
	json_byte(b, '[');
	for (size_t i = 0; i < cfg->n_gestures; i++) {
		const struct sf_gesture *g = &cfg->gestures[i];
		if (i > 0) {
			json_byte(b, ',');
		}
		json_byte(b, '{');
		json_key(b, "fingers");
		json_i64(b, g->fingers);
		json_byte(b, ',');
		json_key(b, "direction");
		json_str(b, sf_gesture_dir_name(g->dir));
		json_byte(b, ',');
		json_key(b, "mods");
		json_mods(b, g->mods);
		json_byte(b, ',');
		json_key(b, "kind");
		json_str(b, sf_gesture_kind_name(g->kind));
		json_byte(b, ',');
		json_key(b, "action");
		if (g->kind == SF_GESTURE_KIND_ACTION) {
			emit_action_steps(b, g->steps, g->n_steps);
		} else {
			json_str(b, sf_gesture_kind_name(g->kind));
		}
		json_byte(b, '}');
	}
	json_byte(b, ']');
}

static void emit_outputs(struct json_buf *b, const struct sf_config *cfg) {
	json_byte(b, '[');
	for (size_t i = 0; i < cfg->n_output_rules; i++) {
		const struct sf_output_rule *r = &cfg->output_rules[i];
		if (i > 0) {
			json_byte(b, ',');
		}
		json_byte(b, '{');
		json_key(b, "match");
		json_str(b, r->match ? r->match : "");
		json_byte(b, ',');
		json_key(b, "arrange");
		json_str(b, sf_arrange_name(r->arrange));
		json_byte(b, ',');
		json_key(b, "tearing");
		if (r->tearing_set) {
			json_bool(b, r->tearing);
		} else {
			json_raw(b, "null");
		}
		json_byte(b, '}');
	}
	json_byte(b, ']');
}

static void emit_inputs(struct json_buf *b, const struct sf_config *cfg) {
	json_byte(b, '[');
	for (size_t i = 0; i < cfg->n_input_rules; i++) {
		const struct sf_input_rule *r = &cfg->input_rules[i];
		if (i > 0) {
			json_byte(b, ',');
		}
		json_byte(b, '{');
		json_key(b, "match");
		json_str(b, r->match ? r->match : "");
		json_byte(b, ',');
		json_key(b, "tap_to_click");
		emit_input_tri(b, r->tap_to_click);
		json_byte(b, ',');
		json_key(b, "natural_scroll");
		emit_input_tri(b, r->natural_scroll);
		json_byte(b, ',');
		json_key(b, "left_handed");
		emit_input_tri(b, r->left_handed);
		json_byte(b, ',');
		json_key(b, "dwt");
		emit_input_tri(b, r->dwt);
		json_byte(b, ',');
		json_key(b, "accel_speed");
		if (r->accel_speed_set) {
			json_f64(b, r->accel_speed);
		} else {
			json_raw(b, "null");
		}
		json_byte(b, ',');
		json_key(b, "accel_profile");
		{
			const char *name = input_accel_name(r->accel_profile);
			if (name != NULL) {
				json_str(b, name);
			} else {
				json_raw(b, "null");
			}
		}
		json_byte(b, ',');
		json_key(b, "repeat_rate");
		if (r->repeat_rate_set) {
			json_i64(b, r->repeat_rate);
		} else {
			json_raw(b, "null");
		}
		json_byte(b, ',');
		json_key(b, "repeat_delay");
		if (r->repeat_delay_set) {
			json_i64(b, r->repeat_delay);
		} else {
			json_raw(b, "null");
		}
		json_byte(b, '}');
	}
	json_byte(b, ']');
}

static void emit_window_rules(struct json_buf *b, const struct sf_config *cfg) {
	json_byte(b, '[');
	for (size_t i = 0; i < cfg->n_window_rules; i++) {
		const struct sf_window_rule *r = &cfg->window_rules[i];
		if (i > 0) {
			json_byte(b, ',');
		}
		json_byte(b, '{');
		json_key(b, "id");
		json_str(b, r->id);
		json_byte(b, ',');
		json_key(b, "parent");
		json_str(b, r->parent_id);
		json_byte(b, ',');
		json_key(b, "app_id");
		json_str(b, r->app_id ? r->app_id : "");
		json_byte(b, ',');
		json_key(b, "title");
		json_str(b, r->title ? r->title : "");
		json_byte(b, ',');
		json_key(b, "tag");
		json_str(b, r->tag ? r->tag : "");
		json_byte(b, ',');
		json_key(b, "center");
		if (r->center_set) {
			json_bool(b, r->center);
		} else {
			json_raw(b, "null");
		}
		json_byte(b, ',');
		json_key(b, "float");
		if (r->float_set) {
			json_bool(b, r->float_win);
		} else {
			json_raw(b, "null");
		}
		json_byte(b, ',');
		json_key(b, "borderless");
		if (r->borderless_set) {
			json_bool(b, r->borderless);
		} else {
			json_raw(b, "null");
		}
		json_byte(b, ',');
		json_key(b, "always_on_top");
		if (r->always_on_top_set) {
			json_bool(b, r->always_on_top);
		} else {
			json_raw(b, "null");
		}
		json_byte(b, ',');
		json_key(b, "opacity");
		if (r->opacity_set) {
			json_f64(b, r->opacity);
		} else {
			json_raw(b, "null");
		}
		json_byte(b, ',');
		json_key(b, "inactive_opacity");
		if (r->inactive_opacity_set) {
			json_f64(b, r->inactive_opacity);
		} else {
			json_raw(b, "null");
		}
		json_byte(b, ',');
		json_key(b, "no_dim");
		if (r->no_dim_set) {
			json_bool(b, r->no_dim);
		} else {
			json_raw(b, "null");
		}
		json_byte(b, ',');
		json_key(b, "tearing");
		if (r->tearing_set) {
			json_bool(b, r->tearing);
		} else {
			json_raw(b, "null");
		}
		json_byte(b, ',');
		json_key(b, "ssd");
		if (r->ssd_set) {
			json_str(b, sf_ssd_mode_name(r->ssd_mode));
		} else {
			json_raw(b, "null");
		}
		json_byte(b, ',');
		json_key(b, "width");
		if (r->width_set) {
			json_i64(b, r->width);
		} else {
			json_raw(b, "null");
		}
		json_byte(b, ',');
		json_key(b, "height");
		if (r->height_set) {
			json_i64(b, r->height);
		} else {
			json_raw(b, "null");
		}
		json_byte(b, ',');
		json_key(b, "action");
		emit_action_steps(b, r->steps, r->n_steps);
		json_byte(b, '}');
	}
	json_byte(b, ']');
}

static bool section_ok(const char *section) {
	return section == NULL || section[0] == '\0' || strcmp(section, "options") == 0 ||
	       strcmp(section, "binds") == 0 || strcmp(section, "mousebinds") == 0 ||
	       strcmp(section, "wheelbinds") == 0 || strcmp(section, "taps") == 0 ||
	       strcmp(section, "scratchpads") == 0 || strcmp(section, "window_rules") == 0 ||
	       strcmp(section, "execs") == 0 || strcmp(section, "inputs") == 0 ||
	       strcmp(section, "gestures") == 0 || strcmp(section, "outputs") == 0;
}

int sf_config_to_json(struct sf_server *server, const char *section, char *buf, size_t cap) {
	if (server == NULL || buf == NULL || cap == 0 || !section_ok(section)) {
		return -1;
	}
	const struct sf_config *cfg = sf_cfg(server);
	struct json_buf b;
	json_buf_init(&b, buf, cap);

	bool all = section == NULL || section[0] == '\0';

	if (all) {
		json_byte(&b, '{');
	}

	bool need_comma = false;
	if (all || strcmp(section, "options") == 0) {
		if (all) {
			json_key(&b, "options");
		}
		emit_options(&b, cfg);
		need_comma = true;
	}
	if (all || strcmp(section, "binds") == 0) {
		if (all) {
			if (need_comma) {
				json_byte(&b, ',');
			}
			json_key(&b, "binds");
		}
		emit_binds(&b, cfg);
		need_comma = true;
	}
	if (all || strcmp(section, "mousebinds") == 0) {
		if (all) {
			if (need_comma) {
				json_byte(&b, ',');
			}
			json_key(&b, "mousebinds");
		}
		emit_mousebinds(&b, cfg);
		need_comma = true;
	}
	if (all || strcmp(section, "wheelbinds") == 0) {
		if (all) {
			if (need_comma) {
				json_byte(&b, ',');
			}
			json_key(&b, "wheelbinds");
		}
		emit_wheelbinds(&b, cfg);
		need_comma = true;
	}
	if (all || strcmp(section, "taps") == 0) {
		if (all) {
			if (need_comma) {
				json_byte(&b, ',');
			}
			json_key(&b, "taps");
		}
		emit_taps(&b, cfg);
		need_comma = true;
	}
	if (all || strcmp(section, "scratchpads") == 0) {
		if (all) {
			if (need_comma) {
				json_byte(&b, ',');
			}
			json_key(&b, "scratchpads");
		}
		emit_scratchpads(&b, cfg);
		need_comma = true;
	}
	if (all || strcmp(section, "window_rules") == 0) {
		if (all) {
			if (need_comma) {
				json_byte(&b, ',');
			}
			json_key(&b, "window_rules");
		}
		emit_window_rules(&b, cfg);
		need_comma = true;
	}
	if (all || strcmp(section, "execs") == 0) {
		if (all) {
			if (need_comma) {
				json_byte(&b, ',');
			}
			json_key(&b, "execs");
		}
		emit_execs(&b, cfg);
		need_comma = true;
	}
	if (all || strcmp(section, "inputs") == 0) {
		if (all) {
			if (need_comma) {
				json_byte(&b, ',');
			}
			json_key(&b, "inputs");
		}
		emit_inputs(&b, cfg);
		need_comma = true;
	}
	if (all || strcmp(section, "gestures") == 0) {
		if (all) {
			if (need_comma) {
				json_byte(&b, ',');
			}
			json_key(&b, "gestures");
		}
		emit_gestures(&b, cfg);
		need_comma = true;
	}
	if (all || strcmp(section, "outputs") == 0) {
		if (all) {
			if (need_comma) {
				json_byte(&b, ',');
			}
			json_key(&b, "outputs");
		}
		emit_outputs(&b, cfg);
	}

	if (all) {
		json_byte(&b, '}');
	}
	json_finish(&b);
	if (!json_ok(&b)) {
		return -1;
	}
	return (int)b.len;
}

/* Indenting pretty-printer for the compact JSON we emit (no nested strings with {). */
void sf_config_print_json(const char *json, FILE *fp) {
	if (json == NULL || fp == NULL) {
		return;
	}
	int indent = 0;
	bool in_str = false;
	bool esc = false;
	for (const char *p = json; *p != '\0'; p++) {
		char c = *p;
		if (in_str) {
			fputc(c, fp);
			if (esc) {
				esc = false;
			} else if (c == '\\') {
				esc = true;
			} else if (c == '"') {
				in_str = false;
			}
			continue;
		}
		switch (c) {
		case '"':
			in_str = true;
			fputc(c, fp);
			break;
		case '{':
		case '[':
			fputc(c, fp);
			fputc('\n', fp);
			indent++;
			for (int i = 0; i < indent; i++) {
				fputs("  ", fp);
			}
			break;
		case '}':
		case ']':
			fputc('\n', fp);
			indent--;
			if (indent < 0) {
				indent = 0;
			}
			for (int i = 0; i < indent; i++) {
				fputs("  ", fp);
			}
			fputc(c, fp);
			break;
		case ',':
			fputc(c, fp);
			fputc('\n', fp);
			for (int i = 0; i < indent; i++) {
				fputs("  ", fp);
			}
			break;
		case ':':
			fputs(": ", fp);
			break;
		default:
			fputc(c, fp);
			break;
		}
	}
	fputc('\n', fp);
}
