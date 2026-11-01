#include "config/config.h"

#include <string.h>

enum sf_client_maximize sf_client_maximize_parse(const char *s) {
	if (s == NULL || s[0] == '\0') {
		return SF_CLIENT_MAXIMIZE_GAPS;
	}
	if (strcmp(s, "edges") == 0 || strcmp(s, "edge") == 0) {
		return SF_CLIENT_MAXIMIZE_EDGES;
	}
	if (strcmp(s, "gaps") == 0 || strcmp(s, "gap") == 0 || strcmp(s, "column") == 0) {
		return SF_CLIENT_MAXIMIZE_GAPS;
	}
	return SF_CLIENT_MAXIMIZE_GAPS;
}

const char *sf_client_maximize_name(enum sf_client_maximize mode) {
	switch (mode) {
	case SF_CLIENT_MAXIMIZE_EDGES:
		return "edges";
	case SF_CLIENT_MAXIMIZE_GAPS:
	default:
		return "gaps";
	}
}

enum sf_unminimize_workspace sf_unminimize_workspace_parse(const char *s) {
	if (s == NULL || s[0] == '\0') {
		return SF_UNMINIMIZE_WS_ACTIVE;
	}
	if (strcmp(s, "home") == 0) {
		return SF_UNMINIMIZE_WS_HOME;
	}
	if (strcmp(s, "active") == 0) {
		return SF_UNMINIMIZE_WS_ACTIVE;
	}
	return SF_UNMINIMIZE_WS_ACTIVE;
}

const char *sf_unminimize_workspace_name(enum sf_unminimize_workspace mode) {
	switch (mode) {
	case SF_UNMINIMIZE_WS_HOME:
		return "home";
	case SF_UNMINIMIZE_WS_ACTIVE:
	default:
		return "active";
	}
}

enum sf_focus_cycle sf_focus_cycle_parse(const char *s) {
	if (s == NULL || s[0] == '\0' || strcmp(s, "spatial") == 0) {
		return SF_FOCUS_CYCLE_SPATIAL;
	}
	if (strcmp(s, "mru") == 0 || strcmp(s, "recent") == 0) {
		return SF_FOCUS_CYCLE_MRU;
	}
	if (strcmp(s, "strip") == 0 || strcmp(s, "columns") == 0) {
		return SF_FOCUS_CYCLE_STRIP;
	}
	return SF_FOCUS_CYCLE_SPATIAL;
}

const char *sf_focus_cycle_name(enum sf_focus_cycle mode) {
	switch (mode) {
	case SF_FOCUS_CYCLE_MRU:
		return "mru";
	case SF_FOCUS_CYCLE_STRIP:
		return "strip";
	case SF_FOCUS_CYCLE_SPATIAL:
	default:
		return "spatial";
	}
}

enum sf_tile_insert sf_tile_insert_parse(const char *s) {
	if (s == NULL || s[0] == '\0' || strcmp(s, "after_focus") == 0 || strcmp(s, "after") == 0) {
		return SF_TILE_INSERT_AFTER_FOCUS;
	}
	if (strcmp(s, "end") == 0 || strcmp(s, "last") == 0) {
		return SF_TILE_INSERT_END;
	}
	if (strcmp(s, "begin") == 0 || strcmp(s, "start") == 0 || strcmp(s, "first") == 0) {
		return SF_TILE_INSERT_BEGIN;
	}
	return SF_TILE_INSERT_AFTER_FOCUS;
}

const char *sf_tile_insert_name(enum sf_tile_insert mode) {
	switch (mode) {
	case SF_TILE_INSERT_END:
		return "end";
	case SF_TILE_INSERT_BEGIN:
		return "begin";
	case SF_TILE_INSERT_AFTER_FOCUS:
	default:
		return "after_focus";
	}
}

enum sf_anim_style sf_anim_style_parse(const char *s) {
	if (s == NULL || s[0] == '\0' || strcmp(s, "default") == 0 || strcmp(s, "auto") == 0) {
		return SF_ANIM_STYLE_DEFAULT;
	}
	if (strcmp(s, "popin") == 0 || strcmp(s, "popout") == 0 || strcmp(s, "zoom") == 0) {
		return SF_ANIM_STYLE_POPIN;
	}
	if (strcmp(s, "slide") == 0) {
		return SF_ANIM_STYLE_SLIDE;
	}
	if (strcmp(s, "fade") == 0) {
		return SF_ANIM_STYLE_FADE;
	}
	if (strcmp(s, "none") == 0 || strcmp(s, "off") == 0) {
		return SF_ANIM_STYLE_NONE;
	}

	return SF_ANIM_STYLE_DEFAULT;
}

const char *sf_anim_style_name(enum sf_anim_style style) {
	switch (style) {
	case SF_ANIM_STYLE_POPIN:
		return "popin";
	case SF_ANIM_STYLE_SLIDE:
		return "slide";
	case SF_ANIM_STYLE_FADE:
		return "fade";
	case SF_ANIM_STYLE_NONE:
		return "none";
	case SF_ANIM_STYLE_DEFAULT:
	default:
		return "default";
	}
}

int sf_anim_op_duration_ms(const struct sf_config *cfg, const struct sf_anim_op *op,
			   int fallback_ms) {
	if (cfg == NULL || op == NULL || !cfg->anim.enabled || !op->enabled ||
	    op->style == SF_ANIM_STYLE_NONE) {
		return 0;
	}
	int dur = op->duration_ms >= 0 ? op->duration_ms : fallback_ms;
	return dur > 0 ? dur : 0;
}
