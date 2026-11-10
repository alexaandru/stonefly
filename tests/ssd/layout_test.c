#include "sf_test.h"
#include "ssd/ssd_layout.h"

static void test_layout_title_basic(void) {
	struct sf_ssd_layout lay;

	sf_ssd_layout_title(400, 28, &lay);
	SF_TEST_EQ_INT(lay.width, 400);
	SF_TEST_EQ_INT(lay.height, 28);
	SF_TEST_EQ_INT(lay.title_left, 10);
	/* close at width - pad - box = 400 - 4 - 28 */
	SF_TEST_EQ_INT(lay.close_x, 368);
	SF_TEST_EQ_INT(lay.max_x, 338);
	SF_TEST_EQ_INT(lay.min_x, 308);
	SF_TEST_TRUE(lay.title_right <= lay.min_x);
	SF_TEST_TRUE(lay.title_right >= lay.title_left);
}

static void test_layout_title_clamps(void) {
	struct sf_ssd_layout lay;

	sf_ssd_layout_title(0, 0, &lay);
	SF_TEST_EQ_INT(lay.width, 1);
	SF_TEST_EQ_INT(lay.height, 1);
}

static void test_layout_hit(void) {
	struct sf_ssd_layout lay;

	sf_ssd_layout_title(400, 28, &lay);
	SF_TEST_EQ_INT((int)sf_ssd_layout_hit(NULL, 0, 0), (int)SF_SSD_BTN_NONE);
	SF_TEST_EQ_INT((int)sf_ssd_layout_hit(&lay, -1, 0), (int)SF_SSD_BTN_NONE);
	SF_TEST_EQ_INT((int)sf_ssd_layout_hit(&lay, 0, -1), (int)SF_SSD_BTN_NONE);
	SF_TEST_EQ_INT((int)sf_ssd_layout_hit(&lay, 400, 0), (int)SF_SSD_BTN_NONE);
	SF_TEST_EQ_INT((int)sf_ssd_layout_hit(&lay, 0, 28), (int)SF_SSD_BTN_NONE);

	SF_TEST_EQ_INT((int)sf_ssd_layout_hit(&lay, lay.close_x, 1), (int)SF_SSD_BTN_CLOSE);
	SF_TEST_EQ_INT((int)sf_ssd_layout_hit(&lay, lay.max_x, 1), (int)SF_SSD_BTN_MAXIMIZE);
	SF_TEST_EQ_INT((int)sf_ssd_layout_hit(&lay, lay.min_x, 1), (int)SF_SSD_BTN_MINIMIZE);
	SF_TEST_EQ_INT((int)sf_ssd_layout_hit(&lay, lay.title_left, 1), (int)SF_SSD_BTN_NONE);
}

static const struct sf_test tests[] = {
	{ "sf_ssd_layout_title_basic", test_layout_title_basic },
	{ "sf_ssd_layout_title_clamps", test_layout_title_clamps },
	{ "sf_ssd_layout_hit", test_layout_hit },
};

int main(void) {
	return sf_test_run(tests, sizeof(tests) / sizeof(tests[0])) == 0 ? 0 : 1;
}
