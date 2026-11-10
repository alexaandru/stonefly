/* sf-suite cover=wm/layout_sm.c src=wm/layout_sm.c */
#include "sf_test.h"
#include "wm/layout_sm.h"

static struct wlr_box box(int x, int y, int w, int h) {
	return (struct wlr_box){.x = x, .y = y, .width = w, .height = h};
}

static struct sf_layout_ctx make_ctx(struct wlr_box current) {
	/* Usable sits below a top bar; fullscreen covers the whole output. */
	struct sf_layout_ctx ctx = {
		.current = current,
		.max_edges = box(0, 40, 1920, 1040),
		.max_gaps = box(10, 68, 1900, 992),
		.fullscreen = box(0, 0, 1920, 1080),
		.snap_left = box(10, 68, 945, 992),
		.snap_right = box(965, 68, 945, 992),
		.snap_top = box(10, 68, 1900, 486),
		.snap_bottom = box(10, 574, 1900, 486),
		.tol = 2,
	};
	return ctx;
}

static bool has_fx(const struct sf_layout_result *r, enum sf_layout_fx_kind kind) {
	for (size_t i = 0; i < r->nfx; i++) {
		if (r->fx[i].kind == kind) {
			return true;
		}
	}
	return false;
}

static bool has_xdg_max(const struct sf_layout_result *r, bool on) {
	for (size_t i = 0; i < r->nfx; i++) {
		if (r->fx[i].kind == SF_LAYOUT_FX_XDG_MAXIMIZED && r->fx[i].flag == on) {
			return true;
		}
	}
	return false;
}

static bool has_apply_near(const struct sf_layout_result *r, const struct wlr_box *want) {
	for (size_t i = 0; i < r->nfx; i++) {
		if (r->fx[i].kind == SF_LAYOUT_FX_APPLY_BOX &&
		    sf_layout_sm_box_near(&r->fx[i].box, want, 0)) {
			return true;
		}
	}
	return false;
}

static void test_box_near(void) {
	struct wlr_box a = box(10, 20, 100, 200);
	struct wlr_box b = box(11, 19, 101, 201);
	SF_TEST_TRUE(sf_layout_sm_box_near(&a, &b, 2));
	SF_TEST_TRUE(!sf_layout_sm_box_near(&a, &b, 0));
}

static void test_toggle_max_edges_enter_leave(void) {
	struct sf_layout_sm s;
	sf_layout_sm_init(&s);
	struct sf_layout_ctx ctx = make_ctx(box(100, 100, 800, 600));
	struct sf_layout_ev ev = {.kind = SF_LAYOUT_EV_TOGGLE_MAX_EDGES};
	struct sf_layout_result out;

	sf_layout_sm_step(&s, &ev, &ctx, &out);
	SF_TEST_EQ_INT((int)out.state.mode, (int)SF_LAYOUT_MODE_MAX_EDGES);
	SF_TEST_TRUE(out.state.has_saved);
	SF_TEST_EQ_INT(out.state.saved.width, 800);
	SF_TEST_TRUE(has_xdg_max(&out, true));
	SF_TEST_TRUE(has_apply_near(&out, &ctx.max_edges));
	SF_TEST_TRUE(has_fx(&out, SF_LAYOUT_FX_CLEAR_AUTO_PLACE));

	s = out.state;
	ctx.current = ctx.max_edges;
	sf_layout_sm_step(&s, &ev, &ctx, &out);
	SF_TEST_EQ_INT((int)out.state.mode, (int)SF_LAYOUT_MODE_FLOAT);
	SF_TEST_TRUE(!out.state.has_saved);
	SF_TEST_TRUE(has_xdg_max(&out, false));
	struct wlr_box restored = box(100, 100, 800, 600);
	SF_TEST_TRUE(has_apply_near(&out, &restored));
}

/*
 * User A/B: max-edges → resize away → toggle should maximize again (not
 * unmaximize). Resize back to max geometry → regain max-edges.
 */
static void test_max_edges_resize_away_toggle_and_regain(void) {
	struct sf_layout_sm s;
	sf_layout_sm_init(&s);
	struct sf_layout_ctx ctx = make_ctx(box(40, 40, 700, 500));
	struct sf_layout_result out;

	struct sf_layout_ev enter = {.kind = SF_LAYOUT_EV_SET_MAX_EDGES, .want = true};
	sf_layout_sm_step(&s, &enter, &ctx, &out);
	SF_TEST_EQ_INT((int)out.state.mode, (int)SF_LAYOUT_MODE_MAX_EDGES);
	s = out.state;

	/* Resize leaves the max box. */
	ctx.current = box(40, 40, 700, 500);
	struct sf_layout_ev geom = {.kind = SF_LAYOUT_EV_GEOM};
	sf_layout_sm_step(&s, &geom, &ctx, &out);
	SF_TEST_EQ_INT((int)out.state.mode, (int)SF_LAYOUT_MODE_FLOAT);
	SF_TEST_TRUE(out.state.has_saved);
	SF_TEST_TRUE(has_xdg_max(&out, false));
	SF_TEST_TRUE(!has_fx(&out, SF_LAYOUT_FX_APPLY_BOX));
	s = out.state;

	/* A) Toggle while floating after break → enter max, not restore. */
	struct sf_layout_ev toggle = {.kind = SF_LAYOUT_EV_TOGGLE_MAX_EDGES};
	sf_layout_sm_step(&s, &toggle, &ctx, &out);
	SF_TEST_EQ_INT((int)out.state.mode, (int)SF_LAYOUT_MODE_MAX_EDGES);
	SF_TEST_TRUE(has_xdg_max(&out, true));
	SF_TEST_TRUE(has_apply_near(&out, &ctx.max_edges));
	s = out.state;

	/* Break again, then B) resize back to max geometry → regain. */
	ctx.current = box(50, 50, 600, 400);
	sf_layout_sm_step(&s, &geom, &ctx, &out);
	SF_TEST_EQ_INT((int)out.state.mode, (int)SF_LAYOUT_MODE_FLOAT);
	s = out.state;

	ctx.current = ctx.max_edges;
	sf_layout_sm_step(&s, &geom, &ctx, &out);
	SF_TEST_EQ_INT((int)out.state.mode, (int)SF_LAYOUT_MODE_MAX_EDGES);
	SF_TEST_TRUE(has_xdg_max(&out, true));
	SF_TEST_TRUE(out.state.has_saved);
}

static void test_snap_geom_leave_and_regain(void) {
	struct sf_layout_sm s;
	sf_layout_sm_init(&s);
	struct sf_layout_ctx ctx = make_ctx(box(200, 200, 400, 300));
	struct sf_layout_result out;
	struct sf_layout_ev snap = {.kind = SF_LAYOUT_EV_SNAP, .snap = SF_SNAP_LEFT};

	sf_layout_sm_step(&s, &snap, &ctx, &out);
	SF_TEST_EQ_INT((int)out.state.mode, (int)SF_LAYOUT_MODE_SNAP);
	SF_TEST_EQ_INT((int)out.state.snap, (int)SF_SNAP_LEFT);
	SF_TEST_TRUE(has_apply_near(&out, &ctx.snap_left));
	s = out.state;

	ctx.current = box(200, 200, 400, 300);
	struct sf_layout_ev geom = {.kind = SF_LAYOUT_EV_GEOM};
	sf_layout_sm_step(&s, &geom, &ctx, &out);
	SF_TEST_EQ_INT((int)out.state.mode, (int)SF_LAYOUT_MODE_FLOAT);
	SF_TEST_EQ_INT((int)sf_layout_sm_snap(&out.state), (int)SF_SNAP_NONE);
	s = out.state;

	ctx.current = ctx.snap_right;
	sf_layout_sm_step(&s, &geom, &ctx, &out);
	SF_TEST_EQ_INT((int)out.state.mode, (int)SF_LAYOUT_MODE_SNAP);
	SF_TEST_EQ_INT((int)out.state.snap, (int)SF_SNAP_RIGHT);
}

static void test_float_clears_saved(void) {
	struct sf_layout_sm s;
	sf_layout_sm_init(&s);
	struct sf_layout_ctx ctx = make_ctx(box(10, 10, 100, 100));
	struct sf_layout_result out;

	struct sf_layout_ev enter = {.kind = SF_LAYOUT_EV_SET_MAX_GAPS, .want = true};
	sf_layout_sm_step(&s, &enter, &ctx, &out);
	s = out.state;
	SF_TEST_TRUE(s.has_saved);

	struct sf_layout_ev flt = {.kind = SF_LAYOUT_EV_FLOAT, .clear_saved = true};
	ctx.current = ctx.max_gaps;
	sf_layout_sm_step(&s, &flt, &ctx, &out);
	SF_TEST_EQ_INT((int)out.state.mode, (int)SF_LAYOUT_MODE_FLOAT);
	SF_TEST_TRUE(!out.state.has_saved);
	SF_TEST_TRUE(!has_fx(&out, SF_LAYOUT_FX_APPLY_BOX));
}

static void test_fullscreen_instant_and_restore(void) {
	struct sf_layout_sm s;
	sf_layout_sm_init(&s);
	struct sf_layout_ctx ctx = make_ctx(box(30, 30, 500, 400));
	struct sf_layout_result out;

	struct sf_layout_ev on = {.kind = SF_LAYOUT_EV_SET_FULLSCREEN, .want = true};
	sf_layout_sm_step(&s, &on, &ctx, &out);
	SF_TEST_EQ_INT((int)out.state.mode, (int)SF_LAYOUT_MODE_FULLSCREEN);
	SF_TEST_TRUE(has_fx(&out, SF_LAYOUT_FX_XDG_FULLSCREEN));
	for (size_t i = 0; i < out.nfx; i++) {
		if (out.fx[i].kind == SF_LAYOUT_FX_APPLY_BOX) {
			SF_TEST_TRUE(!out.fx[i].morph);
		}
	}
	s = out.state;

	struct sf_layout_ev off = {.kind = SF_LAYOUT_EV_SET_FULLSCREEN, .want = false};
	ctx.current = ctx.fullscreen;
	sf_layout_sm_step(&s, &off, &ctx, &out);
	SF_TEST_EQ_INT((int)out.state.mode, (int)SF_LAYOUT_MODE_FLOAT);
	struct wlr_box restored = box(30, 30, 500, 400);
	SF_TEST_TRUE(has_apply_near(&out, &restored));
}

static void test_fullscreen_toggle(void) {
	struct sf_layout_sm s;
	sf_layout_sm_init(&s);
	struct sf_layout_ctx ctx = make_ctx(box(30, 30, 500, 400));
	struct sf_layout_result out;

	struct sf_layout_ev tog = {.kind = SF_LAYOUT_EV_TOGGLE_FULLSCREEN};
	sf_layout_sm_step(&s, &tog, &ctx, &out);
	SF_TEST_EQ_INT((int)out.state.mode, (int)SF_LAYOUT_MODE_FULLSCREEN);
	SF_TEST_TRUE(has_fx(&out, SF_LAYOUT_FX_XDG_FULLSCREEN));
	s = out.state;

	ctx.current = ctx.fullscreen;
	sf_layout_sm_step(&s, &tog, &ctx, &out);
	SF_TEST_EQ_INT((int)out.state.mode, (int)SF_LAYOUT_MODE_FLOAT);
	struct wlr_box restored = box(30, 30, 500, 400);
	SF_TEST_TRUE(has_apply_near(&out, &restored));
}

/* GEOM must not demote fullscreen (sticky until explicit leave). */
static void test_fullscreen_geom_sticky(void) {
	struct sf_layout_sm s;
	sf_layout_sm_init(&s);
	struct sf_layout_ctx ctx = make_ctx(box(30, 30, 500, 400));
	struct sf_layout_result out;

	struct sf_layout_ev on = {.kind = SF_LAYOUT_EV_SET_FULLSCREEN, .want = true};
	sf_layout_sm_step(&s, &on, &ctx, &out);
	s = out.state;
	SF_TEST_EQ_INT((int)out.state.mode, (int)SF_LAYOUT_MODE_FULLSCREEN);

	/* Mid-transition buffer smaller than the output — stay fullscreen. */
	ctx.current = box(100, 100, 800, 600);
	struct sf_layout_ev geom = {.kind = SF_LAYOUT_EV_GEOM};
	sf_layout_sm_step(&s, &geom, &ctx, &out);
	SF_TEST_EQ_INT((int)out.state.mode, (int)SF_LAYOUT_MODE_FULLSCREEN);
	SF_TEST_TRUE(!has_fx(&out, SF_LAYOUT_FX_XDG_FULLSCREEN));
	SF_TEST_TRUE(!has_fx(&out, SF_LAYOUT_FX_APPLY_BOX));
}

/* After explicit leave, a still-full-output buffer must not GEOM-regain fs. */
static void test_fullscreen_no_geom_regain(void) {
	struct sf_layout_sm s;
	sf_layout_sm_init(&s);
	struct sf_layout_ctx ctx = make_ctx(box(30, 30, 500, 400));
	struct sf_layout_result out;

	struct sf_layout_ev on = {.kind = SF_LAYOUT_EV_SET_FULLSCREEN, .want = true};
	sf_layout_sm_step(&s, &on, &ctx, &out);
	s = out.state;

	struct sf_layout_ev off = {.kind = SF_LAYOUT_EV_SET_FULLSCREEN, .want = false};
	ctx.current = ctx.fullscreen;
	sf_layout_sm_step(&s, &off, &ctx, &out);
	s = out.state;
	SF_TEST_EQ_INT((int)out.state.mode, (int)SF_LAYOUT_MODE_FLOAT);

	/* Chrome often keeps the full-output buffer for a few commits. */
	ctx.current = ctx.fullscreen;
	struct sf_layout_ev geom = {.kind = SF_LAYOUT_EV_GEOM};
	sf_layout_sm_step(&s, &geom, &ctx, &out);
	SF_TEST_EQ_INT((int)out.state.mode, (int)SF_LAYOUT_MODE_FLOAT);
	SF_TEST_TRUE(!has_fx(&out, SF_LAYOUT_FX_XDG_FULLSCREEN));
}

static void test_reapply_arranged(void) {
	struct sf_layout_sm s = {
		.mode = SF_LAYOUT_MODE_MAX_EDGES,
		.has_saved = true,
		.saved = box(1, 2, 3, 4),
	};
	struct sf_layout_ctx ctx = make_ctx(box(0, 0, 800, 600));
	ctx.max_edges = box(0, 0, 800, 600);
	ctx.current = ctx.max_edges;
	struct sf_layout_result out;
	struct sf_layout_ev ev = {.kind = SF_LAYOUT_EV_REAPPLY};
	sf_layout_sm_step(&s, &ev, &ctx, &out);
	SF_TEST_EQ_INT((int)out.state.mode, (int)SF_LAYOUT_MODE_MAX_EDGES);
	SF_TEST_TRUE(has_apply_near(&out, &ctx.max_edges));
}

static void test_match_priority(void) {
	struct sf_layout_ctx ctx = make_ctx(box(0, 0, 1920, 1080));
	enum sf_snap snap = SF_SNAP_NONE;
	SF_TEST_EQ_INT((int)sf_layout_sm_match(&ctx, &snap), (int)SF_LAYOUT_MODE_FULLSCREEN);

	ctx.current = ctx.max_edges;
	SF_TEST_EQ_INT((int)sf_layout_sm_match(&ctx, &snap), (int)SF_LAYOUT_MODE_MAX_EDGES);

	ctx.current = ctx.snap_left;
	SF_TEST_EQ_INT((int)sf_layout_sm_match(&ctx, &snap), (int)SF_LAYOUT_MODE_SNAP);
	SF_TEST_EQ_INT((int)snap, (int)SF_SNAP_LEFT);

	ctx.current = ctx.max_gaps;
	SF_TEST_EQ_INT((int)sf_layout_sm_match(&ctx, &snap), (int)SF_LAYOUT_MODE_MAX_GAPS);
}

static void test_queries(void) {
	struct sf_layout_sm s = {.mode = SF_LAYOUT_MODE_SNAP, .snap = SF_SNAP_TOP};
	SF_TEST_TRUE(sf_layout_sm_is_arranged(&s));
	SF_TEST_TRUE(sf_layout_sm_is_fixed(&s));
	SF_TEST_EQ_INT((int)sf_layout_sm_snap(&s), (int)SF_SNAP_TOP);
	SF_TEST_TRUE(!sf_layout_sm_is_max_edges(&s));
	s.mode = SF_LAYOUT_MODE_FLOAT;
	SF_TEST_TRUE(sf_layout_sm_is_float(&s));
	SF_TEST_TRUE(!sf_layout_sm_is_fixed(&s));
}

static const struct sf_test tests[] = {
	{"sf_layout_sm_box_near", test_box_near},
	{"toggle_max_edges_enter_leave", test_toggle_max_edges_enter_leave},
	{"max_edges_resize_away_toggle_and_regain", test_max_edges_resize_away_toggle_and_regain},
	{"snap_geom_leave_and_regain", test_snap_geom_leave_and_regain},
	{"float_clears_saved", test_float_clears_saved},
	{"fullscreen_instant_and_restore", test_fullscreen_instant_and_restore},
	{"fullscreen_toggle", test_fullscreen_toggle},
	{"fullscreen_geom_sticky", test_fullscreen_geom_sticky},
	{"fullscreen_no_geom_regain", test_fullscreen_no_geom_regain},
	{"reapply_arranged", test_reapply_arranged},
	{"match_priority", test_match_priority},
	{"queries", test_queries},
};

int main(void) {
	return sf_test_run(tests, sizeof(tests) / sizeof(tests[0])) == 0 ? 0 : 1;
}
