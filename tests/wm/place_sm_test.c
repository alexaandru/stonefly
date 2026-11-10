/* sf-suite cover=wm/place_sm.c src=wm/place_sm.c */
#include "sf_test.h"
#include "wm/place_sm.h"

static bool has_fx(const struct sf_place_result *r, enum sf_place_fx_kind kind) {
	for (size_t i = 0; i < r->nfx; i++) {
		if (r->fx[i].kind == kind) {
			return true;
		}
	}
	return false;
}

static struct sf_place_ctx ctx_hold(void) {
	return (struct sf_place_ctx){
		.has_parent = false,
		.wants_center = false,
		.center_still_possible = true,
		.partial_identity = true,
		.layout_fixed = false,
	};
}

static void test_begin_and_user_own(void) {
	struct sf_place_sm s;
	sf_place_sm_init(&s);
	struct sf_place_result out;
	struct sf_place_ev begin = {.kind = SF_PLACE_EV_BEGIN};
	sf_place_sm_step(&s, &begin, NULL, &out);
	SF_TEST_TRUE(sf_place_sm_is_managed(&out.state));
	SF_TEST_TRUE(sf_place_sm_is_soft(&out.state));
	SF_TEST_TRUE(!sf_place_sm_is_holding(&out.state));
	s = out.state;

	struct sf_place_ev own = {.kind = SF_PLACE_EV_USER_OWN};
	sf_place_sm_step(&s, &own, NULL, &out);
	SF_TEST_TRUE(!sf_place_sm_is_managed(&out.state));
	SF_TEST_TRUE(!sf_place_sm_is_soft(&out.state));
}

static void test_arm_hold_then_try_release(void) {
	struct sf_place_sm s;
	sf_place_sm_init(&s);
	struct sf_place_result out;
	struct sf_place_ev begin = {.kind = SF_PLACE_EV_BEGIN};
	sf_place_sm_step(&s, &begin, NULL, &out);
	s = out.state;

	struct sf_place_ctx ctx = ctx_hold();
	struct sf_place_ev arm = {.kind = SF_PLACE_EV_ARM_APPEAR};
	sf_place_sm_step(&s, &arm, &ctx, &out);
	SF_TEST_TRUE(sf_place_sm_is_holding(&out.state));
	SF_TEST_TRUE(has_fx(&out, SF_PLACE_FX_SCENE_DISABLE));
	SF_TEST_TRUE(!has_fx(&out, SF_PLACE_FX_START_SHOW));
	s = out.state;

	/* Still holding while SoftPlace possible. */
	struct sf_place_ev trya = {.kind = SF_PLACE_EV_TRY_APPEAR};
	sf_place_sm_step(&s, &trya, &ctx, &out);
	SF_TEST_TRUE(sf_place_sm_is_holding(&out.state));
	SF_TEST_EQ_INT((int)out.nfx, 0);
	s = out.state;

	/* Title arrived and center matched — stop holding. */
	ctx.wants_center = true;
	sf_place_sm_step(&s, &trya, &ctx, &out);
	SF_TEST_TRUE(!sf_place_sm_is_holding(&out.state));
	SF_TEST_TRUE(has_fx(&out, SF_PLACE_FX_SCENE_ENABLE));
	SF_TEST_TRUE(has_fx(&out, SF_PLACE_FX_START_SHOW));
}

static void test_parent_never_holds(void) {
	struct sf_place_sm s = {.phase = SF_PLACE_MANAGED, .soft = true};
	struct sf_place_ctx ctx = ctx_hold();
	ctx.has_parent = true;
	SF_TEST_TRUE(!sf_place_sm_should_hold(&s, &ctx));

	struct sf_place_result out;
	struct sf_place_ev arm = {.kind = SF_PLACE_EV_ARM_APPEAR};
	sf_place_sm_step(&s, &arm, &ctx, &out);
	SF_TEST_TRUE(!sf_place_sm_is_holding(&out.state));
	SF_TEST_TRUE(has_fx(&out, SF_PLACE_FX_START_SHOW));
}

static void test_layout_owned_releases_hold(void) {
	struct sf_place_sm s = {.phase = SF_PLACE_MANAGED, .soft = true, .appear_hold = true};
	struct sf_place_result out;
	struct sf_place_ev ev = {.kind = SF_PLACE_EV_LAYOUT_OWNED};
	sf_place_sm_step(&s, &ev, NULL, &out);
	SF_TEST_TRUE(!sf_place_sm_is_managed(&out.state));
	SF_TEST_TRUE(!sf_place_sm_is_soft(&out.state));
	SF_TEST_TRUE(!sf_place_sm_is_holding(&out.state));
	SF_TEST_TRUE(has_fx(&out, SF_PLACE_FX_START_SHOW));
}

static void test_drop_soft_releases_when_holding(void) {
	struct sf_place_sm s = {.phase = SF_PLACE_MANAGED, .soft = true, .appear_hold = true};
	struct sf_place_ctx ctx = ctx_hold();
	struct sf_place_result out;
	struct sf_place_ev ev = {.kind = SF_PLACE_EV_DROP_SOFT};
	sf_place_sm_step(&s, &ev, &ctx, &out);
	SF_TEST_TRUE(!sf_place_sm_is_soft(&out.state));
	SF_TEST_TRUE(!sf_place_sm_is_holding(&out.state));
	SF_TEST_TRUE(sf_place_sm_is_managed(&out.state));
	SF_TEST_TRUE(has_fx(&out, SF_PLACE_FX_SCENE_ENABLE));
}

static void test_soft_center_reasserts_managed(void) {
	struct sf_place_sm s = {.phase = SF_PLACE_SETTLED, .soft = false};
	struct sf_place_ctx ctx = {.wants_center = true};
	struct sf_place_result out;
	struct sf_place_ev ev = {.kind = SF_PLACE_EV_SOFT_CENTER};
	sf_place_sm_step(&s, &ev, &ctx, &out);
	SF_TEST_TRUE(sf_place_sm_is_managed(&out.state));
	SF_TEST_TRUE(sf_place_sm_is_soft(&out.state));
}

static void test_blank_identity_does_not_hold(void) {
	struct sf_place_sm s = {.phase = SF_PLACE_MANAGED, .soft = true};
	struct sf_place_ctx ctx = ctx_hold();
	ctx.partial_identity = false;
	SF_TEST_TRUE(!sf_place_sm_should_hold(&s, &ctx));
}

static void test_may_morph(void) {
	struct sf_place_sm s = {.phase = SF_PLACE_MANAGED, .soft = false};
	SF_TEST_TRUE(sf_place_sm_may_morph(&s));
	s.phase = SF_PLACE_SETTLED;
	s.soft = true;
	SF_TEST_TRUE(sf_place_sm_may_morph(&s));
	s.soft = false;
	SF_TEST_TRUE(!sf_place_sm_may_morph(&s));
}

static void test_reset_clears_without_fx(void) {
	struct sf_place_sm s = {.phase = SF_PLACE_MANAGED, .soft = true, .appear_hold = true};
	struct sf_place_result out;
	struct sf_place_ev ev = {.kind = SF_PLACE_EV_RESET};
	sf_place_sm_step(&s, &ev, NULL, &out);
	SF_TEST_TRUE(!sf_place_sm_is_managed(&out.state));
	SF_TEST_TRUE(!sf_place_sm_is_holding(&out.state));
	SF_TEST_EQ_INT((int)out.nfx, 0);
}

static const struct sf_test tests[] = {
	{"begin_and_user_own", test_begin_and_user_own},
	{"arm_hold_then_try_release", test_arm_hold_then_try_release},
	{"parent_never_holds", test_parent_never_holds},
	{"layout_owned_releases_hold", test_layout_owned_releases_hold},
	{"drop_soft_releases_when_holding", test_drop_soft_releases_when_holding},
	{"soft_center_reasserts_managed", test_soft_center_reasserts_managed},
	{"blank_identity_does_not_hold", test_blank_identity_does_not_hold},
	{"may_morph", test_may_morph},
	{"reset_clears_without_fx", test_reset_clears_without_fx},
};

int main(void) {
	return sf_test_run(tests, sizeof(tests) / sizeof(tests[0])) == 0 ? 0 : 1;
}
