#include "seat/input_config.h"

#include <ctype.h>
#include <libinput.h>
#include <stdbool.h>
#include <string.h>
#include <strings.h>
#include <wlr/backend/libinput.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_keyboard.h>

#include "config/config.h"
#include "log/log.h"

static bool ascii_ieq(const char *a, const char *b) {
	if (a == NULL || b == NULL) {
		return false;
	}
	return strcasecmp(a, b) == 0;
}

static bool ascii_icontains(const char *hay, const char *needle) {
	if (hay == NULL || needle == NULL || needle[0] == '\0') {
		return false;
	}
	size_t nlen = strlen(needle);
	for (const char *p = hay; *p != '\0'; p++) {
		size_t i = 0;
		while (i < nlen && p[i] != '\0' &&
		       tolower((unsigned char)p[i]) == tolower((unsigned char)needle[i])) {
			i++;
		}
		if (i == nlen) {
			return true;
		}
	}
	return false;
}

static bool is_touchpad(struct libinput_device *libdev) {
	return libdev != NULL && libinput_device_config_tap_get_finger_count(libdev) > 0;
}

static bool input_rule_matches(const struct sf_input_rule *rule, struct wlr_input_device *device,
			       struct libinput_device *libdev) {
	if (rule == NULL || rule->match == NULL || device == NULL) {
		return false;
	}
	if (ascii_ieq(rule->match, "keyboard")) {
		return device->type == WLR_INPUT_DEVICE_KEYBOARD;
	}
	if (ascii_ieq(rule->match, "pointer") || ascii_ieq(rule->match, "mouse")) {
		return device->type == WLR_INPUT_DEVICE_POINTER;
	}
	if (ascii_ieq(rule->match, "touchpad")) {
		return device->type == WLR_INPUT_DEVICE_POINTER && is_touchpad(libdev);
	}
	if (ascii_ieq(rule->match, "touch")) {
		return device->type == WLR_INPUT_DEVICE_TOUCH;
	}
	return ascii_icontains(device->name, rule->match);
}

static void merge_rule(struct sf_input_rule *dst, const struct sf_input_rule *src) {
	if (src->tap_to_click >= 0) {
		dst->tap_to_click = src->tap_to_click;
	}
	if (src->natural_scroll >= 0) {
		dst->natural_scroll = src->natural_scroll;
	}
	if (src->left_handed >= 0) {
		dst->left_handed = src->left_handed;
	}
	if (src->dwt >= 0) {
		dst->dwt = src->dwt;
	}
	if (src->accel_speed_set) {
		dst->accel_speed_set = true;
		dst->accel_speed = src->accel_speed;
	}
	if (src->accel_profile != SF_INPUT_ACCEL_UNSET) {
		dst->accel_profile = src->accel_profile;
	}
	if (src->repeat_rate_set) {
		dst->repeat_rate_set = true;
		dst->repeat_rate = src->repeat_rate;
	}
	if (src->repeat_delay_set) {
		dst->repeat_delay_set = true;
		dst->repeat_delay = src->repeat_delay;
	}
}

static void apply_libinput(struct libinput_device *libdev, const struct sf_input_rule *cfg) {
	if (libdev == NULL || cfg == NULL) {
		return;
	}
	if (cfg->tap_to_click >= 0 && libinput_device_config_tap_get_finger_count(libdev) > 0) {
		libinput_device_config_tap_set_enabled(
			libdev, cfg->tap_to_click ? LIBINPUT_CONFIG_TAP_ENABLED
						  : LIBINPUT_CONFIG_TAP_DISABLED);
	}
	if (cfg->natural_scroll >= 0 &&
	    libinput_device_config_scroll_has_natural_scroll(libdev) != 0) {
		libinput_device_config_scroll_set_natural_scroll_enabled(libdev,
									 cfg->natural_scroll);
	}
	if (cfg->left_handed >= 0 && libinput_device_config_left_handed_is_available(libdev)) {
		libinput_device_config_left_handed_set(libdev, cfg->left_handed);
	}
	if (cfg->dwt >= 0 && libinput_device_config_dwt_is_available(libdev)) {
		libinput_device_config_dwt_set_enabled(libdev,
						       cfg->dwt ? LIBINPUT_CONFIG_DWT_ENABLED
								: LIBINPUT_CONFIG_DWT_DISABLED);
	}
	if (cfg->accel_profile != SF_INPUT_ACCEL_UNSET &&
	    libinput_device_config_accel_is_available(libdev)) {
		uint32_t profile = LIBINPUT_CONFIG_ACCEL_PROFILE_ADAPTIVE;
		switch (cfg->accel_profile) {
		case SF_INPUT_ACCEL_NONE:
		case SF_INPUT_ACCEL_FLAT:
			profile = LIBINPUT_CONFIG_ACCEL_PROFILE_FLAT;
			break;
		case SF_INPUT_ACCEL_ADAPTIVE:
			profile = LIBINPUT_CONFIG_ACCEL_PROFILE_ADAPTIVE;
			break;
		case SF_INPUT_ACCEL_UNSET:
			break;
		}
		libinput_device_config_accel_set_profile(libdev, profile);
	}
	if (cfg->accel_speed_set && libinput_device_config_accel_is_available(libdev)) {
		libinput_device_config_accel_set_speed(libdev, cfg->accel_speed);
	}
}

void sf_input_apply_device(struct sf_server *server, struct wlr_input_device *device) {
	if (server == NULL || device == NULL) {
		return;
	}
	const struct sf_config *cfg = sf_cfg(server);
	if (cfg == NULL || cfg->n_input_rules == 0) {
		return;
	}

	struct libinput_device *libdev = NULL;
	if (wlr_input_device_is_libinput(device)) {
		libdev = wlr_libinput_get_device_handle(device);
	}

	struct sf_input_rule merged = {
		.tap_to_click = -1,
		.natural_scroll = -1,
		.left_handed = -1,
		.dwt = -1,
		.accel_profile = SF_INPUT_ACCEL_UNSET,
	};
	bool any = false;
	for (size_t i = 0; i < cfg->n_input_rules; i++) {
		const struct sf_input_rule *rule = &cfg->input_rules[i];
		if (!input_rule_matches(rule, device, libdev)) {
			continue;
		}
		merge_rule(&merged, rule);
		any = true;
	}
	if (!any) {
		return;
	}

	if (libdev != NULL) {
		apply_libinput(libdev, &merged);
	}

	if (device->type == WLR_INPUT_DEVICE_KEYBOARD &&
	    (merged.repeat_rate_set || merged.repeat_delay_set)) {
		struct wlr_keyboard *kb = wlr_keyboard_from_input_device(device);
		if (kb != NULL) {
			int rate =
				merged.repeat_rate_set ? merged.repeat_rate : kb->repeat_info.rate;
			int delay = merged.repeat_delay_set ? merged.repeat_delay
							    : kb->repeat_info.delay;
			wlr_keyboard_set_repeat_info(kb, rate, delay);
		}
	}

	SF_LOGI("input apply", sf_str("name", device->name != NULL ? device->name : ""),
		sf_i64("tap", (int64_t)merged.tap_to_click),
		sf_i64("natural_scroll", (int64_t)merged.natural_scroll),
		sf_bool("libinput", libdev != NULL));
}
