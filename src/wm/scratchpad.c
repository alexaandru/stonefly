#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/box.h>

#include "anim/anim.h"
#include "config/config.h"
#include "log/log.h"
#include "server/server.h"
#include "wm/scratchpad.h"
#include "wm/wm.h"

#define STONEFLY_SCRATCH_CLAIM_TIMEOUT_S 15

struct stonefly_scratch_slot {
	const struct stonefly_scratchpad_spec *spec;
	struct stonefly_toplevel *toplevel;
};

struct stonefly_scratch_pending {
	char name[32];
	char app_id[64];
	struct timespec since;
	bool active;
};

/* One slot per configured pad (capped at STONEFLY_SCRATCHPAD_MAX). */
static struct stonefly_scratch_slot scratch_slots[STONEFLY_SCRATCHPAD_MAX];
static size_t scratch_nslots;
static struct stonefly_scratch_pending scratch_pending;

static struct stonefly_scratch_slot *slot_by_name(const char *name) {
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
	return dt >= STONEFLY_SCRATCH_CLAIM_TIMEOUT_S;
}

static void pending_clear(void) {
	scratch_pending.active = false;
	scratch_pending.name[0] = '\0';
	scratch_pending.app_id[0] = '\0';
}

static void scratch_hide_now(struct stonefly_toplevel *toplevel) {
	/* Off LIFO; apply_hidden owns flag/scene/foreign/defocus. */
	stonefly_toplevel_apply_hidden(toplevel, STONEFLY_HIDE_SCRATCH);
}

static void scratch_hide(struct stonefly_toplevel *toplevel) {
	if (toplevel->minimized) {
		/* Already hidden (e.g. Ctrl+Esc): reclassify off the LIFO stack. */
		stonefly_toplevel_apply_hidden(toplevel, STONEFLY_HIDE_SCRATCH);
		return;
	}
	if (toplevel->anim_hide) {
		/* Already sliding out (toggle or prior auto_hide). */
		return;
	}
	if (stonefly_anim_start_hide(toplevel, scratch_hide_now)) {
		return;
	}
	scratch_hide_now(toplevel);
}

/* Peeble OutputForWindow: center-on-head, else cursor head. */
static struct wlr_output *toplevel_output(struct stonefly_toplevel *toplevel) {
	if (toplevel == NULL || toplevel->server == NULL) {
		return NULL;
	}
	struct stonefly_server *server = toplevel->server;
	struct wlr_box geom;
	stonefly_toplevel_layout_geom(toplevel, &geom);
	double cx = geom.x + geom.width / 2.0;
	double cy = geom.y + geom.height / 2.0;
	struct wlr_output *out =
		wlr_output_layout_output_at(server->output_layout, cx, cy);
	if (out == NULL && server->cursor != NULL) {
		out = wlr_output_layout_output_at(server->output_layout,
			server->cursor->x, server->cursor->y);
	}
	return out;
}

static void scratch_show(struct stonefly_toplevel *toplevel) {
	if (stonefly_anim_active(toplevel) && toplevel->anim_hide) {
		/* Reverse an in-flight hide (peeble beginSlide mid-flight). */
		stonefly_anim_start_show(toplevel);
	}
	/* apply_shown + focus via focus_toplevel. */
	stonefly_toplevel_apply_shown(toplevel, true);
}

static void scratch_claim(struct stonefly_scratch_slot *slot,
		struct stonefly_toplevel *toplevel) {
	if (toplevel->scratchpad[0] != '\0' &&
			strcmp(toplevel->scratchpad, slot->spec->name) != 0) {
		struct stonefly_scratch_slot *old = slot_by_name(toplevel->scratchpad);
		if (old != NULL && old->toplevel == toplevel) {
			old->toplevel = NULL;
		}
	}
	snprintf(toplevel->scratchpad, sizeof(toplevel->scratchpad), "%s",
		slot->spec->name);
	slot->toplevel = toplevel;
	pending_clear();
	SF_LOGI("scratchpad claim",
		sf_str("name", slot->spec->name),
		sf_str("app_id",
			toplevel->xdg_toplevel->app_id != NULL
				? toplevel->xdg_toplevel->app_id : ""));
}

static void scratch_spawn(struct stonefly_server *server,
		struct stonefly_scratch_slot *slot) {
	(void)server;
	if (scratch_pending.active &&
			strcmp(scratch_pending.name, slot->spec->name) == 0 &&
			!pending_expired()) {
		SF_LOGD("scratchpad spawn already pending",
			sf_str("name", slot->spec->name));
		return;
	}

	snprintf(scratch_pending.name, sizeof(scratch_pending.name), "%s",
		slot->spec->name);
	if (slot->spec->app_id != NULL) {
		snprintf(scratch_pending.app_id, sizeof(scratch_pending.app_id), "%s",
			slot->spec->app_id);
	} else {
		scratch_pending.app_id[0] = '\0';
	}
	clock_gettime(CLOCK_MONOTONIC, &scratch_pending.since);
	scratch_pending.active = true;

	SF_LOGI("scratchpad spawn",
		sf_str("name", slot->spec->name),
		sf_str("cmd", slot->spec->cmd));

	if (fork() == 0) {
		execl("/bin/sh", "/bin/sh", "-c", slot->spec->cmd, (void *)NULL);
		_exit(1);
	}
}

void stonefly_scratchpad_init(struct stonefly_server *server) {
	const struct stonefly_config *cfg = stonefly_cfg(server);
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

void stonefly_scratchpad_finish(struct stonefly_server *server) {
	(void)server;
	pending_clear();
	for (size_t i = 0; i < scratch_nslots; i++) {
		scratch_slots[i].toplevel = NULL;
	}
}

bool stonefly_toplevel_is_scratchpad(const struct stonefly_toplevel *toplevel) {
	return toplevel != NULL && toplevel->scratchpad[0] != '\0';
}

void stonefly_scratchpad_on_destroy(struct stonefly_toplevel *toplevel) {
	if (toplevel == NULL || toplevel->scratchpad[0] == '\0') {
		return;
	}
	struct stonefly_scratch_slot *slot = slot_by_name(toplevel->scratchpad);
	if (slot != NULL && slot->toplevel == toplevel) {
		slot->toplevel = NULL;
	}
	toplevel->scratchpad[0] = '\0';
}

void stonefly_scratchpad_try_claim(struct stonefly_toplevel *toplevel) {
	if (toplevel == NULL || !scratch_pending.active) {
		return;
	}
	if (pending_expired()) {
		SF_LOGI("scratchpad claim timed out",
			sf_str("name", scratch_pending.name));
		pending_clear();
		return;
	}
	if (toplevel->scratchpad[0] != '\0') {
		return;
	}

	struct stonefly_scratch_slot *slot = slot_by_name(scratch_pending.name);
	if (slot == NULL) {
		pending_clear();
		return;
	}
	if (slot->toplevel != NULL && slot->toplevel != toplevel) {
		return;
	}

	const char *app_id = toplevel->xdg_toplevel->app_id;
	if (scratch_pending.app_id[0] != '\0') {
		if (app_id == NULL || app_id[0] == '\0') {
			/* Wait for set_app_id. */
			return;
		}
		if (strcmp(app_id, scratch_pending.app_id) != 0) {
			return;
		}
	}

	scratch_claim(slot, toplevel);
	/*
	 * set_app_id often arrives before the first xdg commit. Configuring
	 * then aborts wlroots 0.20 (assert surface->initialized). Map finishes
	 * max+focus once the surface is ready.
	 */
	if (!toplevel->xdg_toplevel->base->initialized) {
		return;
	}
	/* First spawn: useful full-output size (peeble MaximizeWindowToEdges). */
	stonefly_toplevel_set_max_edges(toplevel, true);
	scratch_show(toplevel);
}

void stonefly_scratchpad_toggle(struct stonefly_server *server, const char *name) {
	struct stonefly_scratch_slot *slot = slot_by_name(name);
	if (slot == NULL) {
		SF_LOGW("scratchpad unknown", sf_str("name", name != NULL ? name : ""));
		return;
	}

	struct stonefly_toplevel *toplevel = slot->toplevel;
	if (toplevel != NULL) {
		/* Destroyed windows are cleared in on_destroy; defensive. */
		if (toplevel->minimized ||
				(stonefly_anim_active(toplevel) && toplevel->anim_hide)) {
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

void stonefly_scratchpad_claim_focused(struct stonefly_server *server,
		const char *name) {
	struct stonefly_scratch_slot *slot = slot_by_name(name);
	if (slot == NULL) {
		SF_LOGW("scratchpad unknown", sf_str("name", name != NULL ? name : ""));
		return;
	}
	struct stonefly_toplevel *toplevel = stonefly_focused_toplevel(server);
	if (toplevel == NULL) {
		SF_LOGW("scratchpad claim: no focused window",
			sf_str("name", slot->spec->name));
		return;
	}
	if (slot->toplevel != NULL && slot->toplevel != toplevel) {
		SF_LOGW("scratchpad already occupied",
			sf_str("name", slot->spec->name));
		return;
	}
	if (slot->toplevel == toplevel) {
		scratch_show(toplevel);
		return;
	}
	scratch_claim(slot, toplevel);
	scratch_show(toplevel);
}

void stonefly_scratchpad_on_focus_change(struct stonefly_toplevel *focused) {
	/*
	 * Nil focus (exclusive layer, empty desktop) leaves pads shown — same as
	 * peeble. Click-outside catcher is a separate roadmap item.
	 */
	if (focused == NULL) {
		return;
	}

	struct wlr_output *focus_out = toplevel_output(focused);

	for (size_t i = 0; i < scratch_nslots; i++) {
		struct stonefly_scratch_slot *slot = &scratch_slots[i];
		struct stonefly_toplevel *pad = slot->toplevel;
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

		SF_LOGD("scratchpad auto_hide",
			sf_str("name", slot->spec->name));
		scratch_hide(pad);
	}
}
