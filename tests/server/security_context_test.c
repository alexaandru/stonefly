/* sf-suite cover=server/security_context.c src=server/security_context.c */
#include "sf_test.h"
#include "server/security_context.h"

#include <stddef.h>

static void test_allowed_app_globals(void) {
	SF_TEST_TRUE(sf_security_context_global_allowed("wl_compositor"));
	SF_TEST_TRUE(sf_security_context_global_allowed("wl_shm"));
	SF_TEST_TRUE(sf_security_context_global_allowed("zwp_linux_dmabuf_v1"));
	SF_TEST_TRUE(sf_security_context_global_allowed("wl_seat"));
	SF_TEST_TRUE(sf_security_context_global_allowed("wl_output"));
	SF_TEST_TRUE(sf_security_context_global_allowed("xdg_wm_base"));
	SF_TEST_TRUE(sf_security_context_global_allowed("wp_fractional_scale_manager_v1"));
	SF_TEST_TRUE(sf_security_context_global_allowed("zwp_pointer_constraints_v1"));
	const char *inh = "zwp_keyboard_shortcuts_inhibit_manager_v1";
	SF_TEST_TRUE(sf_security_context_global_allowed(inh));
	SF_TEST_TRUE(sf_security_context_global_allowed("ext_background_effect_manager_v1"));
}

static void test_blocked_privileged_globals(void) {
	SF_TEST_TRUE(!sf_security_context_global_allowed(NULL));
	SF_TEST_TRUE(!sf_security_context_global_allowed(""));
	/* Nested security contexts must stay blocked. */
	SF_TEST_TRUE(!sf_security_context_global_allowed("wp_security_context_manager_v1"));
	/* Shell, capture, session, output, clipboard-mgr, virtual-input. */
	SF_TEST_TRUE(!sf_security_context_global_allowed("zwlr_layer_shell_v1"));
	SF_TEST_TRUE(!sf_security_context_global_allowed("zwlr_foreign_toplevel_manager_v1"));
	SF_TEST_TRUE(!sf_security_context_global_allowed("ext_foreign_toplevel_list_v1"));
	SF_TEST_TRUE(!sf_security_context_global_allowed("ext_workspace_manager_v1"));
	SF_TEST_TRUE(!sf_security_context_global_allowed("ext_session_lock_manager_v1"));
	SF_TEST_TRUE(!sf_security_context_global_allowed("zwlr_output_manager_v1"));
	SF_TEST_TRUE(!sf_security_context_global_allowed("zwlr_output_power_manager_v1"));
	SF_TEST_TRUE(!sf_security_context_global_allowed("zwlr_gamma_control_manager_v1"));
	SF_TEST_TRUE(!sf_security_context_global_allowed("zwlr_screencopy_manager_v1"));
	SF_TEST_TRUE(!sf_security_context_global_allowed("ext_image_copy_capture_manager_v1"));
	SF_TEST_TRUE(!sf_security_context_global_allowed("ext_data_control_manager_v1"));
	SF_TEST_TRUE(!sf_security_context_global_allowed("zwlr_data_control_manager_v1"));
	SF_TEST_TRUE(!sf_security_context_global_allowed("zwp_virtual_keyboard_manager_v1"));
}

static const struct sf_test tests[] = {
	{"allowed_app_globals", test_allowed_app_globals},
	{"blocked_privileged_globals", test_blocked_privileged_globals},
};

int main(void) {
	return sf_test_run(tests, sizeof(tests) / sizeof(tests[0])) == 0 ? 0 : 1;
}
