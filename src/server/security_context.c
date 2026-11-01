#include "server/security_context.h"

#include <stddef.h>
#include <string.h>

/*
 * Reviewed ordinary application protocols exposed to security-context clients
 * (Flatpak, etc.). Mirrors Umbriel's allowlist for globals stonefly advertises.
 * Privileged globals (layer-shell, capture, foreign-toplevel, session-lock,
 * output-management, data-control, virtual-keyboard, nested security-context)
 * stay blocked by omission.
 */
static const char *const sf_allowed_sandbox_globals[] = {
	"wl_shm",
	"wl_drm",
	"zwp_linux_dmabuf_v1",
	"wl_compositor",
	"wl_subcompositor",
	"wl_data_device_manager",
	"zwp_primary_selection_device_manager_v1",
	"wp_viewporter",
	"wp_fractional_scale_manager_v1",
	"wp_presentation",
	"wp_tearing_control_manager_v1",
	"ext_background_effect_manager_v1",
	"wl_output",
	"xdg_wm_base",
	"xdg_toplevel_tag_manager_v1",
	"xdg_toplevel_icon_manager_v1",
	"xdg_wm_dialog_v1",
	"zxdg_decoration_manager_v1",
	"xdg_activation_v1",
	"xdg_system_bell_v1",
	"wl_seat",
	"wp_cursor_shape_manager_v1",
	"zwp_relative_pointer_manager_v1",
	"zwp_pointer_constraints_v1",
	"zwp_pointer_gestures_v1",
	"zwp_idle_inhibit_manager_v1",
	"zwp_keyboard_shortcuts_inhibit_manager_v1",
	/* Apps compose text; input-method stays privileged (blocked by omission). */
	"zwp_text_input_manager_v3",
	"zwp_tablet_manager_v2",
};

bool sf_security_context_global_allowed(const char *interface_name) {
	if (interface_name == NULL) {
		return false;
	}
	size_t n = sizeof(sf_allowed_sandbox_globals) / sizeof(sf_allowed_sandbox_globals[0]);
	for (size_t i = 0; i < n; i++) {
		if (strcmp(interface_name, sf_allowed_sandbox_globals[i]) == 0) {
			return true;
		}
	}

	return false;
}
