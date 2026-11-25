/* sf-suite cover=wm/tiled_edges.h */
#include "sf_test.h"
#include "wm/tiled_edges.h"

static void test_tiled_edges_all(void) {
	uint32_t all = sf_tiled_edges_all();
	SF_TEST_TRUE((all & (uint32_t)WLR_EDGE_TOP) != 0);
	SF_TEST_TRUE((all & (uint32_t)WLR_EDGE_BOTTOM) != 0);
	SF_TEST_TRUE((all & (uint32_t)WLR_EDGE_LEFT) != 0);
	SF_TEST_TRUE((all & (uint32_t)WLR_EDGE_RIGHT) != 0);
	SF_TEST_EQ_INT((int)all, (int)((uint32_t)WLR_EDGE_TOP | (uint32_t)WLR_EDGE_BOTTOM |
				       (uint32_t)WLR_EDGE_LEFT | (uint32_t)WLR_EDGE_RIGHT));
}

static void test_tiled_edges_for_snap(void) {
	SF_TEST_EQ_INT((int)sf_tiled_edges_for_snap(SF_SNAP_NONE), 0);
	SF_TEST_EQ_INT((int)sf_tiled_edges_for_snap(SF_SNAP_LEFT),
		       (int)((uint32_t)WLR_EDGE_LEFT | (uint32_t)WLR_EDGE_TOP |
			     (uint32_t)WLR_EDGE_BOTTOM));
	SF_TEST_EQ_INT((int)sf_tiled_edges_for_snap(SF_SNAP_RIGHT),
		       (int)((uint32_t)WLR_EDGE_RIGHT | (uint32_t)WLR_EDGE_TOP |
			     (uint32_t)WLR_EDGE_BOTTOM));
	SF_TEST_EQ_INT(
		(int)sf_tiled_edges_for_snap(SF_SNAP_TOP),
		(int)((uint32_t)WLR_EDGE_TOP | (uint32_t)WLR_EDGE_LEFT | (uint32_t)WLR_EDGE_RIGHT));
	SF_TEST_EQ_INT((int)sf_tiled_edges_for_snap(SF_SNAP_BOTTOM),
		       (int)((uint32_t)WLR_EDGE_BOTTOM | (uint32_t)WLR_EDGE_LEFT |
			     (uint32_t)WLR_EDGE_RIGHT));
}

static const struct sf_test tests[] = {
	{"sf_tiled_edges_all", test_tiled_edges_all},
	{"sf_tiled_edges_for_snap", test_tiled_edges_for_snap},
};

int main(void) {
	return sf_test_run(tests, sizeof(tests) / sizeof(tests[0])) == 0 ? 0 : 1;
}
