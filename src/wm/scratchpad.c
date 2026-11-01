#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <scenefx/types/wlr_scene.h>
#include <wlr/types/wlr_xdg_shell.h>

#include "anim/anim.h"
#include "config/hardcoded.h"
#include "log/log.h"
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

/* Day-zero: one slot per hardcoded spec (small, fixed). */
static struct stonefly_scratch_slot scratch_slots[8];
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
	if (toplevel->minimized) {
		return;
	}
	toplevel->minimized = true;
	wlr_scene_node_set_enabled(&toplevel->scene_tree->node, false);
	/* Named pads stay off the unnamed Ctrl+Esc LIFO stack. */
	wl_list_init(&toplevel->minimize_link);
}

static void scratch_hide(struct stonefly_toplevel *toplevel) {
	if (toplevel->minimized) {
		return;
	}
	if (stonefly_anim_start_hide(toplevel, scratch_hide_now)) {
		return;
	}
	scratch_hide_now(toplevel);
}

static void scratch_show(struct stonefly_toplevel *toplevel) {
	if (stonefly_anim_active(toplevel) && toplevel->anim_hide) {
		/* Reverse an in-flight hide (peeble StartShow mid-flight). */
		stonefly_anim_start_show(toplevel);
	}
	/* focus_toplevel → unminimize starts the show slide when hidden. */
	stonefly_focus_toplevel(toplevel);
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
	(void)server;
	scratch_nslots = stonefly_scratchpad_count();
	if (scratch_nslots > sizeof(scratch_slots) / sizeof(scratch_slots[0])) {
		scratch_nslots = sizeof(scratch_slots) / sizeof(scratch_slots[0]);
	}
	for (size_t i = 0; i < scratch_nslots; i++) {
		scratch_slots[i].spec = &stonefly_scratchpads[i];
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
			bool was_focused = stonefly_focused_toplevel(server) == toplevel;
			scratch_hide(toplevel);
			if (was_focused) {
				stonefly_focus_top(server);
			}
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
