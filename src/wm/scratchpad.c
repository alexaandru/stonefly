#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_xdg_activation_v1.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/box.h>

#include "anim/anim.h"
#include "config/config.h"
#include "log/log.h"
#include "server/server.h"
#include "util/spawn.h"
#include "wm/layout_sm.h"
#include "wm/scratchpad.h"
#include "wm/wm.h"
#include "wm/workspace.h"

#define SF_SCRATCH_CLAIM_TIMEOUT_S 15
#define SF_SCRATCH_LAUNCH_MAGIC 0x53465044u /* 'SFPD' */

struct sf_scratch_slot {
	const struct sf_scratchpad_spec *spec;
	struct sf_toplevel *toplevel;
};

struct sf_scratch_launch {
	uint32_t magic;
	char pad_name[32];
	struct wlr_xdg_activation_token_v1 *token;
	struct wl_listener destroy;
};

struct sf_scratch_pending {
	char name[32];
	char app_id[64];
	struct timespec since;
	bool active;
	struct sf_scratch_launch *launch;
};

/* One slot per configured pad (capped at SF_SCRATCHPAD_MAX). */
static struct sf_scratch_slot scratch_slots[SF_SCRATCHPAD_MAX];
static size_t scratch_nslots;
static struct sf_scratch_pending scratch_pending;

static struct sf_scratch_slot *slot_by_name(const char *name) {
	if (name == NULL || name[0] == '\0') {
		return NULL;
	}
	for (size_t i = 0; i < scratch_nslots; i++) {
		if (strcmp(scratch_slots[i].spec->name, name) == 0) {
			return &scratch_slots[i];
		}
	}
	return NULL;
}

static bool pending_expired(void) {
	if (!scratch_pending.active) {
		return true;
	}
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	time_t dt = now.tv_sec - scratch_pending.since.tv_sec;
	return dt >= SF_SCRATCH_CLAIM_TIMEOUT_S;
}

static void launch_destroy(struct wl_listener *listener, void *data) {
	(void)data;
	struct sf_scratch_launch *launch = wl_container_of(listener, launch, destroy);
	wl_list_remove(&launch->destroy.link);
	if (scratch_pending.launch == launch) {
		scratch_pending.launch = NULL;
	}
	free(launch);
}

static void pending_clear(void) {
	if (scratch_pending.launch != NULL && scratch_pending.launch->token != NULL) {
		struct wlr_xdg_activation_token_v1 *token = scratch_pending.launch->token;
		scratch_pending.launch->token = NULL;
		/* Destroy emits launch_destroy → frees launch + clears pointer. */
		wlr_xdg_activation_token_v1_destroy(token);
	} else if (scratch_pending.launch != NULL) {
		wl_list_remove(&scratch_pending.launch->destroy.link);
		free(scratch_pending.launch);
		scratch_pending.launch = NULL;
	}
	scratch_pending.active = false;
	scratch_pending.name[0] = '\0';
	scratch_pending.app_id[0] = '\0';
}

static void scratch_hide_now(struct sf_toplevel *toplevel) {
	/* Off LIFO; apply_hidden owns flag/scene/foreign/defocus. */
	sf_toplevel_apply_hidden(toplevel, SF_HIDE_SCRATCH);
}

static void scratch_hide(struct sf_toplevel *toplevel) {
	if (toplevel->minimized) {
		/* Already hidden (e.g. Ctrl+Esc): reclassify off the LIFO stack. */
		sf_toplevel_apply_hidden(toplevel, SF_HIDE_SCRATCH);
		return;
	}
	if (toplevel->anim_hide) {
		/* Already sliding out (toggle or prior auto_hide). */
		return;
	}
	if (sf_anim_start_hide(toplevel, scratch_hide_now)) {
		return;
	}
	scratch_hide_now(toplevel);
}

/* Peeble OutputForWindow: center-on-head, else cursor head. */
static struct wlr_output *toplevel_output(struct sf_toplevel *toplevel) {
	if (toplevel == NULL || toplevel->server == NULL) {
		return NULL;
	}
	struct sf_server *server = toplevel->server;
	struct wlr_box geom;
	sf_toplevel_layout_geom(toplevel, &geom);
	double cx = geom.x + geom.width / 2.0;
	double cy = geom.y + geom.height / 2.0;
	struct wlr_output *out = wlr_output_layout_output_at(server->output_layout, cx, cy);
	if (out == NULL && server->cursor != NULL) {
		out = wlr_output_layout_output_at(server->output_layout, server->cursor->x,
						  server->cursor->y);
	}
	return out;
}

static void scratch_show(struct sf_toplevel *toplevel) {
	if (sf_anim_active(toplevel) && toplevel->anim_hide) {
		/* Reverse an in-flight hide (peeble beginSlide mid-flight).
		 * Do not REAPPLY edge-max here — apply_box cancels the slide. */
		sf_anim_start_show(toplevel);
		sf_toplevel_apply_shown(toplevel, true);
		return;
	}
	/*
	 * Only reassert edge-max when the pad is still edged. User resize /
	 * nudge demotes to float and must survive hide/show with that size
	 * (fresh spawn/claim still forces max-edges). Reassert while hidden so
	 * apply_box updates resting without cancelling the show slide.
	 */
	if (toplevel->minimized && sf_layout_sm_is_max_edges(&toplevel->layout)) {
		sf_toplevel_set_max_edges(toplevel, true);
	}
	/* apply_shown + focus via focus_toplevel. */
	sf_toplevel_apply_shown(toplevel, true);
}

static void scratch_finish_claim(struct sf_scratch_slot *slot, struct sf_toplevel *toplevel) {
	if (toplevel->scratchpad[0] != '\0' &&
	    strcmp(toplevel->scratchpad, slot->spec->name) != 0) {
		struct sf_scratch_slot *old = slot_by_name(toplevel->scratchpad);
		if (old != NULL && old->toplevel == toplevel) {
			old->toplevel = NULL;
		}
	}
	snprintf(toplevel->scratchpad, sizeof(toplevel->scratchpad), "%s", slot->spec->name);
	slot->toplevel = toplevel;
	/* Scratchpads stay visible across workspace switches. */
	sf_workspace_detach_scratchpad(toplevel);
	pending_clear();
	const char *app_id = sf_toplevel_app_id(toplevel);
	SF_LOGI("scratchpad claim", sf_str("name", slot->spec->name),
		sf_str("app_id", app_id != NULL ? app_id : ""));
	/*
	 * set_app_id often arrives before the first xdg commit. Configuring
	 * then aborts wlroots 0.20 (assert surface->initialized). Map finishes
	 * max+focus once the surface is ready.
	 */
	if (!sf_toplevel_is_initialized(toplevel)) {
		return;
	}
	/* First spawn: useful full-output size (peeble MaximizeWindowToEdges). */
	sf_toplevel_set_max_edges(toplevel, true);
	scratch_show(toplevel);
}

static struct sf_scratch_launch *launch_from_token(struct wlr_xdg_activation_token_v1 *token) {
	if (token == NULL || token->data == NULL) {
		return NULL;
	}
	struct sf_scratch_launch *launch = token->data;
	if (launch->magic != SF_SCRATCH_LAUNCH_MAGIC) {
		return NULL;
	}
	return launch;
}

static void scratch_spawn(struct sf_server *server, struct sf_scratch_slot *slot) {
	if (scratch_pending.active && strcmp(scratch_pending.name, slot->spec->name) == 0 &&
	    !pending_expired()) {
		SF_LOGD("scratchpad spawn already pending", sf_str("name", slot->spec->name));
		return;
	}
	pending_clear();

	snprintf(scratch_pending.name, sizeof(scratch_pending.name), "%s", slot->spec->name);
	if (slot->spec->app_id != NULL) {
		snprintf(scratch_pending.app_id, sizeof(scratch_pending.app_id), "%s",
			 slot->spec->app_id);
	} else {
		scratch_pending.app_id[0] = '\0';
	}
	clock_gettime(CLOCK_MONOTONIC, &scratch_pending.since);
	scratch_pending.active = true;

	const char *token_name = NULL;
	if (server->xdg_activation != NULL) {
		struct wlr_xdg_activation_token_v1 *token =
			wlr_xdg_activation_token_v1_create(server->xdg_activation);
		if (token != NULL) {
			struct sf_scratch_launch *launch = calloc(1, sizeof *launch);
			if (launch == NULL) {
				wlr_xdg_activation_token_v1_destroy(token);
			} else {
				launch->magic = SF_SCRATCH_LAUNCH_MAGIC;
				snprintf(launch->pad_name, sizeof(launch->pad_name), "%s",
					 slot->spec->name);
				launch->token = token;
				launch->destroy.notify = launch_destroy;
				wl_signal_add(&token->events.destroy, &launch->destroy);
				token->data = launch;
				scratch_pending.launch = launch;
				token_name = wlr_xdg_activation_token_v1_get_name(token);
			}
		}
	}

	SF_LOGI("scratchpad spawn", sf_str("name", slot->spec->name),
		sf_str("cmd", slot->spec->cmd),
		sf_str("token", token_name != NULL ? token_name : ""));
	sf_spawn_cmd_activation(slot->spec->cmd, token_name);
}

void sf_scratchpad_init(struct sf_server *server) {
	const struct sf_config *cfg = sf_cfg(server);
	scratch_nslots = cfg->n_scratchpads;
	if (scratch_nslots > sizeof(scratch_slots) / sizeof(scratch_slots[0])) {
		scratch_nslots = sizeof(scratch_slots) / sizeof(scratch_slots[0]);
	}
	for (size_t i = 0; i < scratch_nslots; i++) {
		scratch_slots[i].spec = &cfg->scratchpads[i];
		scratch_slots[i].toplevel = NULL;
	}
	pending_clear();
}

void sf_scratchpad_finish(struct sf_server *server) {
	(void)server;
	pending_clear();
	for (size_t i = 0; i < scratch_nslots; i++) {
		scratch_slots[i].toplevel = NULL;
	}
	scratch_nslots = 0;
}

void sf_scratchpad_rebind(struct sf_server *server) {
	struct {
		char name[32];
		struct sf_toplevel *toplevel;
	} kept[SF_SCRATCHPAD_MAX];
	size_t n_kept = 0;

	for (size_t i = 0; i < scratch_nslots; i++) {
		if (scratch_slots[i].toplevel == NULL || scratch_slots[i].spec == NULL ||
		    scratch_slots[i].spec->name == NULL) {
			continue;
		}
		if (n_kept >= SF_SCRATCHPAD_MAX) {
			break;
		}
		snprintf(kept[n_kept].name, sizeof kept[n_kept].name, "%s",
			 scratch_slots[i].spec->name);
		kept[n_kept].toplevel = scratch_slots[i].toplevel;
		n_kept++;
	}

	const struct sf_config *cfg = sf_cfg(server);
	scratch_nslots = cfg->n_scratchpads;
	if (scratch_nslots > SF_SCRATCHPAD_MAX) {
		scratch_nslots = SF_SCRATCHPAD_MAX;
	}
	for (size_t i = 0; i < scratch_nslots; i++) {
		scratch_slots[i].spec = &cfg->scratchpads[i];
		scratch_slots[i].toplevel = NULL;
		const char *name = cfg->scratchpads[i].name;
		if (name == NULL || name[0] == '\0') {
			continue;
		}
		for (size_t k = 0; k < n_kept; k++) {
			if (kept[k].toplevel == NULL) {
				continue;
			}
			if (strcmp(kept[k].name, name) != 0) {
				continue;
			}
			scratch_slots[i].toplevel = kept[k].toplevel;
			kept[k].toplevel = NULL;
			break;
		}
	}

	/* Pads dropped from config: clear the scratch tag; show if still hidden. */
	for (size_t k = 0; k < n_kept; k++) {
		struct sf_toplevel *t = kept[k].toplevel;
		if (t == NULL) {
			continue;
		}
		SF_LOGI("scratchpad orphaned by reload", sf_str("name", kept[k].name));
		t->scratchpad[0] = '\0';
		if (t->minimized || t->anim_hide) {
			scratch_show(t);
		}
	}

	/* Refresh pending app_id from the new spec when the pad still exists. */
	if (scratch_pending.active) {
		struct sf_scratch_slot *slot = slot_by_name(scratch_pending.name);
		if (slot == NULL) {
			pending_clear();
		} else if (slot->spec->app_id != NULL) {
			snprintf(scratch_pending.app_id, sizeof(scratch_pending.app_id), "%s",
				 slot->spec->app_id);
		} else {
			scratch_pending.app_id[0] = '\0';
		}
	}
}

bool sf_toplevel_is_scratchpad(const struct sf_toplevel *toplevel) {
	return toplevel != NULL && toplevel->scratchpad[0] != '\0';
}

void sf_scratchpad_on_destroy(struct sf_toplevel *toplevel) {
	if (toplevel == NULL || toplevel->scratchpad[0] == '\0') {
		return;
	}
	struct sf_scratch_slot *slot = slot_by_name(toplevel->scratchpad);
	if (slot != NULL && slot->toplevel == toplevel) {
		slot->toplevel = NULL;
	}
	toplevel->scratchpad[0] = '\0';
}

bool sf_scratchpad_try_claim_activation(struct sf_toplevel *toplevel,
					struct wlr_xdg_activation_token_v1 *token) {
	if (toplevel == NULL) {
		return false;
	}
	struct sf_scratch_launch *launch = launch_from_token(token);
	if (launch == NULL) {
		return false;
	}
	if (toplevel->scratchpad[0] != '\0') {
		return true;
	}
	struct sf_scratch_slot *slot = slot_by_name(launch->pad_name);
	if (slot == NULL) {
		return true;
	}
	if (slot->toplevel != NULL && slot->toplevel != toplevel) {
		SF_LOGW("scratchpad already occupied", sf_str("name", slot->spec->name));
		return true;
	}
	scratch_finish_claim(slot, toplevel);
	return true;
}

void sf_scratchpad_try_claim(struct sf_toplevel *toplevel) {
	if (toplevel == NULL || !scratch_pending.active) {
		return;
	}
	if (pending_expired()) {
		SF_LOGI("scratchpad claim timed out", sf_str("name", scratch_pending.name));
		pending_clear();
		return;
	}
	if (toplevel->scratchpad[0] != '\0') {
		return;
	}

	struct sf_scratch_slot *slot = slot_by_name(scratch_pending.name);
	if (slot == NULL) {
		pending_clear();
		return;
	}
	if (slot->toplevel != NULL && slot->toplevel != toplevel) {
		return;
	}

	/* Prefer activation token claim; app_id / first-map is the fallback. */
	if (scratch_pending.launch != NULL) {
		/* Wait for request_activate with our token when one was issued. */
		if (scratch_pending.app_id[0] == '\0') {
			return;
		}
	}

	const char *app_id = sf_toplevel_app_id(toplevel);
	if (scratch_pending.app_id[0] != '\0') {
		if (app_id == NULL || app_id[0] == '\0') {
			/* Wait for set_app_id. */
			return;
		}
		if (strcmp(app_id, scratch_pending.app_id) != 0) {
			return;
		}
	}

	scratch_finish_claim(slot, toplevel);
}

void sf_scratchpad_toggle(struct sf_server *server, const char *name) {
	struct sf_scratch_slot *slot = slot_by_name(name);
	if (slot == NULL) {
		SF_LOGW("scratchpad unknown", sf_str("name", name != NULL ? name : ""));
		return;
	}

	struct sf_toplevel *toplevel = slot->toplevel;
	if (toplevel != NULL) {
		/* Destroyed windows are cleared in on_destroy; defensive. */
		if (toplevel->minimized || (sf_anim_active(toplevel) && toplevel->anim_hide)) {
			scratch_show(toplevel);
		} else {
			/* Defocus runs in apply_hidden after minimized is set (same as
			 * Ctrl+Esc), so focus_top cannot re-pick this window mid-slide. */
			scratch_hide(toplevel);
		}
		return;
	}

	scratch_spawn(server, slot);
}

/* Drop pad membership and put the window back on the active workspace. */
static void scratch_unclaim(struct sf_scratch_slot *slot, struct sf_toplevel *toplevel) {
	if (toplevel == NULL) {
		return;
	}
	if (slot != NULL && slot->toplevel == toplevel) {
		slot->toplevel = NULL;
	}
	toplevel->scratchpad[0] = '\0';
	const char *app_id = sf_toplevel_app_id(toplevel);
	SF_LOGI("scratchpad unclaim",
		sf_str("name", slot != NULL && slot->spec != NULL ? slot->spec->name : ""),
		sf_str("app_id", app_id != NULL ? app_id : ""));

	struct sf_output *output = sf_output_for_toplevel(toplevel);
	if (output == NULL) {
		output = sf_output_at_cursor(toplevel->server);
	}
	struct sf_workspace *ws = output != NULL ? sf_workspace_active(output) : NULL;
	if (ws != NULL) {
		sf_workspace_assign(toplevel, ws);
	}
	/* Keep current geom / edge-max; just make sure it is on-screen and focused. */
	if (toplevel->minimized || toplevel->anim_hide) {
		scratch_show(toplevel);
	}
}

void sf_scratchpad_claim_focused(struct sf_server *server, const char *name) {
	struct sf_scratch_slot *slot = slot_by_name(name);
	if (slot == NULL) {
		SF_LOGW("scratchpad unknown", sf_str("name", name != NULL ? name : ""));
		return;
	}
	struct sf_toplevel *toplevel = sf_focused_toplevel(server);
	if (toplevel == NULL) {
		SF_LOGW("scratchpad claim: no focused window", sf_str("name", slot->spec->name));
		return;
	}
	/* Already this pad → unclaim (empty slot; next toggle-pad can spawn). */
	if (slot->toplevel == toplevel || (toplevel->scratchpad[0] != '\0' &&
					   strcmp(toplevel->scratchpad, slot->spec->name) == 0)) {
		scratch_unclaim(slot, toplevel);
		return;
	}
	/* Steal the pad from a previous occupant so Super+t can retarget. */
	if (slot->toplevel != NULL && slot->toplevel != toplevel) {
		scratch_unclaim(slot, slot->toplevel);
	}
	scratch_finish_claim(slot, toplevel);
}

void sf_scratchpad_hide_all(struct sf_server *server) {
	(void)server;
	for (size_t i = 0; i < scratch_nslots; i++) {
		struct sf_scratch_slot *slot = &scratch_slots[i];
		struct sf_toplevel *pad = slot->toplevel;
		if (pad == NULL) {
			continue;
		}
		if (pad->minimized || pad->anim_hide) {
			continue;
		}
		scratch_hide(pad);
	}
}

void sf_scratchpad_on_focus_change(struct sf_toplevel *focused) {
	/*
	 * Nil focus (exclusive layer, empty desktop) leaves pads shown — same as
	 * peeble. Click-outside catcher is a separate roadmap item.
	 */
	if (focused == NULL) {
		return;
	}

	struct wlr_output *focus_out = toplevel_output(focused);

	for (size_t i = 0; i < scratch_nslots; i++) {
		struct sf_scratch_slot *slot = &scratch_slots[i];
		struct sf_toplevel *pad = slot->toplevel;
		if (pad == NULL || pad == focused) {
			continue;
		}
		if (slot->spec == NULL || !slot->spec->auto_hide) {
			continue;
		}
		if (pad->minimized || pad->anim_hide) {
			continue;
		}

		struct wlr_output *pad_out = toplevel_output(pad);
		if (pad_out != NULL && focus_out != NULL && pad_out != focus_out) {
			continue;
		}

		SF_LOGD("scratchpad auto_hide", sf_str("name", slot->spec->name));
		scratch_hide(pad);
	}
}
