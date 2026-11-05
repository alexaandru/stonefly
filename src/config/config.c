#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "config/config.h"
#include "config/config_embed.h"
#include "config/config_lua.h"
#include "log/log.h"
#include "server/server.h"

static char *xstrdup(const char *s) {
	if (s == NULL) {
		return NULL;
	}
	char *d = strdup(s);
	if (d == NULL) {
		SF_LOGE("config strdup OOM");
	}
	return d;
}

static void copy4(float dst[4], const float src[4]) {
	dst[0] = src[0];
	dst[1] = src[1];
	dst[2] = src[2];
	dst[3] = src[3];
}

static int xdg_join(char *buf, size_t n, const char *env_override, const char *xdg_env,
		    const char *home_fallback_suffix, const char *app) {
	const char *override = getenv(env_override);
	if (override != NULL && override[0] != '\0') {
		if (snprintf(buf, n, "%s", override) >= (int)n) {
			return -1;
		}
		return 0;
	}
	const char *xdg = getenv(xdg_env);
	if (xdg != NULL && xdg[0] != '\0') {
		if (snprintf(buf, n, "%s/%s", xdg, app) >= (int)n) {
			return -1;
		}
		return 0;
	}
	const char *home = getenv("HOME");
	if (home == NULL || home[0] == '\0') {
		return -1;
	}
	if (snprintf(buf, n, "%s/%s/%s", home, home_fallback_suffix, app) >= (int)n) {
		return -1;
	}
	return 0;
}

int stonefly_config_dir(char *buf, size_t n) {
	return xdg_join(buf, n, "STONEFLY_CONFIG", "XDG_CONFIG_HOME", ".config", "stonefly");
}

int stonefly_data_dir(char *buf, size_t n) {
	return xdg_join(buf, n, "STONEFLY_DATA", "XDG_DATA_HOME", ".local/share", "stonefly");
}

int stonefly_cache_dir(char *buf, size_t n) {
	return xdg_join(buf, n, "STONEFLY_CACHE", "XDG_CACHE_HOME", ".cache", "stonefly");
}

int stonefly_config_init_path(char *buf, size_t n) {
	char dir[512];
	if (stonefly_config_dir(dir, sizeof dir) != 0) {
		return -1;
	}
	if (snprintf(buf, n, "%s/init.lua", dir) >= (int)n) {
		return -1;
	}
	return 0;
}

int stonefly_mkdir_p(const char *path) {
	char tmp[512];
	if (path == NULL || snprintf(tmp, sizeof tmp, "%s", path) >= (int)sizeof tmp) {
		return -1;
	}
	size_t len = strlen(tmp);
	if (len == 0) {
		return -1;
	}
	if (tmp[len - 1] == '/') {
		tmp[len - 1] = '\0';
	}
	for (char *p = tmp + 1; *p; p++) {
		if (*p != '/') {
			continue;
		}
		*p = '\0';
		if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
			return -1;
		}
		*p = '/';
	}
	if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
		return -1;
	}
	return 0;
}

static int write_bytes_excl(const char *path, const unsigned char *data, size_t len) {
	int fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0644);
	if (fd < 0) {
		return -1;
	}
	size_t off = 0;
	while (off < len) {
		ssize_t n = write(fd, data + off, len - off);
		if (n < 0) {
			if (errno == EINTR) {
				continue;
			}
			close(fd);
			unlink(path);
			return -1;
		}
		off += (size_t)n;
	}
	if (close(fd) != 0) {
		unlink(path);
		return -1;
	}
	return 0;
}

int stonefly_config_write_default(void) {
	char dir[512];
	char path[576];
	if (stonefly_config_dir(dir, sizeof dir) != 0) {
		SF_LOGE("write-config: cannot resolve config dir");
		return 1;
	}
	if (snprintf(path, sizeof path, "%s/init.lua", dir) >= (int)sizeof path) {
		return 1;
	}
	if (access(path, F_OK) == 0) {
		fprintf(stderr, "stonefly: %s already exists, refusing to overwrite\n", path);
		return 1;
	}
	if (stonefly_mkdir_p(dir) != 0) {
		SF_LOGE("write-config mkdir", sf_str("dir", dir), sf_str("err", strerror(errno)));
		return 1;
	}
	for (size_t i = 0; i < stonefly_default_files_len; i++) {
		const struct stonefly_embed_file *f = &stonefly_default_files[i];
		if (snprintf(path, sizeof path, "%s/%s", dir, f->name) >= (int)sizeof path) {
			return 1;
		}
		if (write_bytes_excl(path, f->data, f->len) != 0) {
			fprintf(stderr, "stonefly: write %s: %s\n", path, strerror(errno));
			return 1;
		}
		printf("wrote %s\n", path);
	}
	return 0;
}

const struct stonefly_config *stonefly_cfg(const struct stonefly_server *server) {
	return &server->config;
}

void stonefly_config_finish(struct stonefly_config *cfg) {
	if (cfg == NULL) {
		return;
	}
	stonefly_config_lua_close(cfg);

	free(cfg->cmd_foot);
	free(cfg->cmd_nautilus);
	free(cfg->cmd_ghostty);
	free(cfg->cmd_ghostty_qt);
	free(cfg->cmd_editor);
	free(cfg->cmd_launcher);
	free(cfg->capture_block_app_ids);
	for (size_t i = 0; i < cfg->n_keybinds; i++) {
		free(cfg->keybinds[i].cmd);
	}
	free(cfg->keybinds);
	for (size_t i = 0; i < cfg->n_tapbinds; i++) {
		free(cfg->tapbinds[i].single_cmd);
		free(cfg->tapbinds[i].double_cmd);
	}
	free(cfg->tapbinds);
	for (size_t i = 0; i < cfg->n_scratchpads; i++) {
		free(cfg->scratchpads[i].name);
		free(cfg->scratchpads[i].cmd);
		free(cfg->scratchpads[i].app_id);
	}
	free(cfg->scratchpads);
	memset(cfg, 0, sizeof *cfg);
}

void stonefly_config_set_defaults(struct stonefly_config *cfg) {
	memset(cfg, 0, sizeof *cfg);

	cfg->output_scale = 1.5f;
	cfg->cursor_size_default = 24;
	cfg->gap_size = 15;
	cfg->cascade_step = 25;
	cfg->cascade_wrap = 10;
	cfg->slide_duration_ms = 200;
	cfg->slide_dir_closest = 1;
	cfg->morph_duration_ms = 200;
	cfg->client_opacity = 0.96f;
	cfg->inactive_opacity = 0.96f; /* match client_opacity → opt-in dim */
	cfg->corner_radius = 12;
	cfg->border_thickness = 3;
	cfg->shadow_blur_sigma = 20.0f;
	copy4(cfg->border_active, (float[4]){0.10f, 0.75f, 1.00f, 1.00f});
	copy4(cfg->border_inactive, (float[4]){0.22f, 0.22f, 0.24f, 1.00f});
	copy4(cfg->border_urgent, (float[4]){1.00f, 0.45f, 0.10f, 1.00f});
	copy4(cfg->shadow_color, (float[4]){0.0f, 0.0f, 0.0f, 0.55f});
	cfg->window_blur = 1;
	/* false: blur real backdrop including other clients (hypr/niri-ish). */
	cfg->blur_xray = 0;
	/* mango-ish SceneFX defaults */
	cfg->blur_num_passes = 2;
	cfg->blur_radius = 5;
	cfg->blur_noise = 0.02f;
	cfg->blur_brightness = 0.9f;
	cfg->blur_contrast = 0.9f;
	cfg->blur_saturation = 1.2f;
	cfg->bell_flash_ms = 200;
	cfg->snap_threshold = 26;
	cfg->snap_preview_border = 1;
	cfg->snap_preview_opacity = 0.40f;
	cfg->tap_max_hold_ms = 500;
	cfg->double_tap_gap_ms = 300;
	cfg->layout = 0; /* STONEFLY_LAYOUT_STACKING */
	cfg->swipe_workspace_threshold = 80;
	cfg->swipe_workspace_natural = 1;
	cfg->ssd_mode = 1; /* STONEFLY_SSD_AUTO */
	cfg->ssd_height = 28;
	cfg->ssd_font_size = 17;
	cfg->resize_border_in = 3;
	cfg->resize_border_out = 1;

	cfg->cmd_foot = xstrdup("foot");
	cfg->cmd_nautilus = xstrdup("nautilus");
	cfg->cmd_ghostty = xstrdup("ghostty +new-window");
	cfg->cmd_ghostty_qt = xstrdup("ghostty +toggle-quick-terminal");
	cfg->cmd_editor = xstrdup("gtk-launch org.gnome.TextEditor");
	cfg->cmd_launcher = xstrdup("noctalia msg panel-toggle launcher");
	cfg->capture_block_app_ids = xstrdup("");

	static const struct {
		uint32_t mods;
		xkb_keysym_t sym;
		xkb_keysym_t sym_alt;
		enum stonefly_action action;
		const char *cmd;
	} keys[] = {
		{STONEFLY_SUPER, XKB_KEY_Escape, 0, STONEFLY_ACTION_QUIT, NULL},
		{STONEFLY_SUPER | STONEFLY_SHIFT, XKB_KEY_Q, 0, STONEFLY_ACTION_QUIT, NULL},
		{STONEFLY_SUPER, XKB_KEY_q, 0, STONEFLY_ACTION_CLOSE, NULL},
		{STONEFLY_SUPER, XKB_KEY_Tab, 0, STONEFLY_ACTION_FOCUS_NEXT, NULL},
		{STONEFLY_SUPER | STONEFLY_SHIFT, XKB_KEY_Tab, XKB_KEY_ISO_Left_Tab,
		 STONEFLY_ACTION_FOCUS_PREV, NULL},
		{STONEFLY_ALT, XKB_KEY_Tab, 0, STONEFLY_ACTION_EXEC,
		 "noctalia msg window-switcher"},
		{STONEFLY_ALT | STONEFLY_SHIFT, XKB_KEY_Tab, XKB_KEY_ISO_Left_Tab,
		 STONEFLY_ACTION_FOCUS_PREV_ALL, NULL},
		{STONEFLY_SUPER, XKB_KEY_f, 0, STONEFLY_ACTION_TOGGLE_MAX_EDGES, NULL},
		{STONEFLY_SUPER, XKB_KEY_m, 0, STONEFLY_ACTION_TOGGLE_MAX_GAPS, NULL},
		{STONEFLY_CTRL, XKB_KEY_Escape, 0, STONEFLY_ACTION_MINIMIZE, NULL},
		{STONEFLY_CTRL | STONEFLY_SHIFT, XKB_KEY_Escape, 0, STONEFLY_ACTION_UNMINIMIZE,
		 NULL},
		{STONEFLY_SUPER, XKB_KEY_Left, 0, STONEFLY_ACTION_SNAP_LEFT, NULL},
		{STONEFLY_SUPER, XKB_KEY_Right, 0, STONEFLY_ACTION_SNAP_RIGHT, NULL},
		{STONEFLY_SUPER, XKB_KEY_Up, 0, STONEFLY_ACTION_SNAP_TOP, NULL},
		{STONEFLY_SUPER, XKB_KEY_Down, 0, STONEFLY_ACTION_SNAP_BOTTOM, NULL},
		{STONEFLY_SUPER, XKB_KEY_u, 0, STONEFLY_ACTION_UNSNAP, NULL},
		{STONEFLY_SUPER, XKB_KEY_space, 0, STONEFLY_ACTION_SPAWN_FOOT, NULL},
		{STONEFLY_SUPER, XKB_KEY_z, 0, STONEFLY_ACTION_SPAWN_NAUTILUS, NULL},
		{STONEFLY_SUPER, XKB_KEY_w, 0, STONEFLY_ACTION_SPAWN_GHOSTTY, NULL},
		{STONEFLY_ALT, XKB_KEY_Return, 0, STONEFLY_ACTION_SPAWN_GHOSTTY_QT, NULL},
		{STONEFLY_SUPER, XKB_KEY_e, 0, STONEFLY_ACTION_SPAWN_EDITOR, NULL},
		{STONEFLY_SUPER, XKB_KEY_t, 0, STONEFLY_ACTION_SCRATCH_CLAIM_TERM, NULL},
		{0, XKB_KEY_Print, 0, STONEFLY_ACTION_EXEC, "noctalia msg screenshot-fullscreen"},
		{STONEFLY_SHIFT, XKB_KEY_Print, 0, STONEFLY_ACTION_EXEC,
		 "noctalia msg screenshot-region"},
		{STONEFLY_CTRL | STONEFLY_SHIFT, XKB_KEY_Print, 0, STONEFLY_ACTION_EXEC,
		 "noctalia msg screenshot-window"},
		{0, XKB_KEY_XF86AudioRaiseVolume, 0, STONEFLY_ACTION_EXEC,
		 "noctalia msg volume-up"},
		{0, XKB_KEY_XF86AudioLowerVolume, 0, STONEFLY_ACTION_EXEC,
		 "noctalia msg volume-down"},
		{0, XKB_KEY_XF86AudioMute, 0, STONEFLY_ACTION_EXEC, "noctalia msg volume-mute"},
		{0, XKB_KEY_XF86AudioMicMute, 0, STONEFLY_ACTION_EXEC, "noctalia msg mic-mute"},
		{0, XKB_KEY_XF86Launch5, 0, STONEFLY_ACTION_EXEC, "noctalia msg mic-volume-up"},
		{STONEFLY_SHIFT, XKB_KEY_XF86Launch5, 0, STONEFLY_ACTION_EXEC,
		 "noctalia msg mic-volume-down"},
		{0, XKB_KEY_XF86MonBrightnessUp, 0, STONEFLY_ACTION_EXEC,
		 "noctalia msg brightness-up"},
		{0, XKB_KEY_XF86MonBrightnessDown, 0, STONEFLY_ACTION_EXEC,
		 "noctalia msg brightness-down"},
		{0, XKB_KEY_XF86KbdBrightnessUp, 0, STONEFLY_ACTION_EXEC,
		 "noctalia msg keyboard-backlight-up"},
		{0, XKB_KEY_XF86KbdBrightnessDown, 0, STONEFLY_ACTION_EXEC,
		 "noctalia msg keyboard-backlight-down"},
		{0, XKB_KEY_XF86AudioPlay, XKB_KEY_XF86AudioPause, STONEFLY_ACTION_EXEC,
		 "noctalia msg media toggle"},
		{0, XKB_KEY_XF86AudioNext, 0, STONEFLY_ACTION_EXEC, "noctalia msg media next"},
		{0, XKB_KEY_XF86AudioPrev, 0, STONEFLY_ACTION_EXEC, "noctalia msg media previous"},
	};

	cfg->n_keybinds = sizeof keys / sizeof keys[0];
	cfg->keybinds = calloc(cfg->n_keybinds, sizeof *cfg->keybinds);
	if (cfg->keybinds == NULL) {
		SF_LOGE("config keybinds OOM");
		cfg->n_keybinds = 0;
		return;
	}
	for (size_t i = 0; i < cfg->n_keybinds; i++) {
		cfg->keybinds[i].mods = keys[i].mods;
		cfg->keybinds[i].sym = keys[i].sym;
		cfg->keybinds[i].sym_alt = keys[i].sym_alt;
		cfg->keybinds[i].action = keys[i].action;
		cfg->keybinds[i].cmd = xstrdup(keys[i].cmd);
	}

	cfg->n_tapbinds = 1;
	cfg->tapbinds = calloc(1, sizeof *cfg->tapbinds);
	if (cfg->tapbinds != NULL) {
		cfg->tapbinds[0].sym = XKB_KEY_Super_L;
		cfg->tapbinds[0].sym_alt = XKB_KEY_Super_R;
		cfg->tapbinds[0].single = STONEFLY_ACTION_SPAWN_LAUNCHER;
		cfg->tapbinds[0].double_tap = STONEFLY_ACTION_SCRATCH_TOGGLE_TERM;
	} else {
		cfg->n_tapbinds = 0;
	}

	cfg->n_scratchpads = 1;
	cfg->scratchpads = calloc(1, sizeof *cfg->scratchpads);
	if (cfg->scratchpads != NULL) {
		cfg->scratchpads[0].name = xstrdup("term");
		cfg->scratchpads[0].cmd = xstrdup("ghostty --class=stonefly.scratch.term");
		cfg->scratchpads[0].app_id = xstrdup("stonefly.scratch.term");
		cfg->scratchpads[0].auto_hide = true;
	} else {
		cfg->n_scratchpads = 0;
	}
}
