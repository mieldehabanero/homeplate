#include <unity.h>
#include <stdint.h>
#include <string.h>
#include <vector>

#include "gray4.h"

void setUp(void) {}
void tearDown(void) {}

struct Panel {
    uint8_t pixels[2] = {0x55, 0x55};
    int calls = 0;
    int clearMode = -1;
    bool keepOn = true;
    int result = 0;

    uint8_t *currentBuffer() { return pixels; }
    int fullUpdate(int clear, bool power) {
        calls++;
        clearMode = clear;
        keepOn = power;
        return result;
    }
};

void test_all_eight_levels_map_to_four_shades(void) {
    const uint8_t source[] = {0x01, 0x23, 0x45, 0x67};
    uint8_t actual[2] = {};
    const uint8_t expected[] = {0x05, 0xaf};
    TEST_ASSERT_TRUE(packGray4(source, sizeof(source), actual, sizeof(actual), 8, 1));
    TEST_ASSERT_EQUAL_MEMORY(expected, actual, sizeof(expected));
}

void test_pixels_keep_their_columns_and_rows(void) {
    const uint8_t source[] = {0x07, 0x25, 0x61, 0x43};
    uint8_t actual[2] = {};
    const uint8_t expected[] = {0x36, 0xc9};
    TEST_ASSERT_TRUE(packGray4(source, sizeof(source), actual, sizeof(actual), 4, 2));
    TEST_ASSERT_EQUAL_MEMORY(expected, actual, sizeof(expected));
}

void test_inkplate10_frame_preserves_last_pixel_without_overwriting_edges(void) {
    std::vector<uint8_t> source(1200 * 825 / 2, 0x77);
    source.front() = 0x07;
    source.back() = 0x70;
    std::vector<uint8_t> actual(1200 * 825 / 4 + 2, 0xaa);
    TEST_ASSERT_TRUE(packGray4(source.data(), source.size(), actual.data() + 1,
                               actual.size() - 2, 1200, 825));
    TEST_ASSERT_EQUAL_HEX8(0xaa, actual.front());
    TEST_ASSERT_EQUAL_HEX8(0x3f, actual[1]);
    TEST_ASSERT_EQUAL_HEX8(0xff, actual[2]);
    TEST_ASSERT_EQUAL_HEX8(0xfc, actual[actual.size() - 2]);
    TEST_ASSERT_EQUAL_HEX8(0xaa, actual.back());
}

void test_incomplete_frame_does_not_modify_destination(void) {
    const uint8_t source[] = {0x07};
    uint8_t actual[] = {0xaa, 0xbb};
    TEST_ASSERT_FALSE(packGray4(source, sizeof(source), actual, sizeof(actual), 4, 2));
    TEST_ASSERT_EQUAL_HEX8(0xaa, actual[0]);
    TEST_ASSERT_EQUAL_HEX8(0xbb, actual[1]);
}

void test_successful_image_redraws_whole_frame_without_clearing(void) {
    Panel panel;
    const uint8_t source[] = {0x01, 0x23, 0x45, 0x67};
    TEST_ASSERT_TRUE(refreshGray4Image(panel, source, sizeof(source), 8, 1, true));
    TEST_ASSERT_EQUAL(1, panel.calls);
    TEST_ASSERT_EQUAL(0, panel.clearMode); // FastEPD CLEAR_NONE
    TEST_ASSERT_FALSE(panel.keepOn);
    const uint8_t expected[] = {0x05, 0xaf};
    TEST_ASSERT_EQUAL_MEMORY(expected, panel.pixels, sizeof(expected));
}

void test_failed_decode_does_not_send_or_replace_frame(void) {
    Panel panel;
    const uint8_t source[] = {0x01, 0x23, 0x45, 0x67};
    TEST_ASSERT_FALSE(refreshGray4Image(panel, source, sizeof(source), 8, 1, false));
    TEST_ASSERT_EQUAL(0, panel.calls);
    TEST_ASSERT_EQUAL_HEX8(0x55, panel.pixels[0]);
}

void test_incomplete_image_is_never_sent_to_panel(void) {
    Panel panel;
    const uint8_t source[] = {0x01};
    TEST_ASSERT_FALSE(refreshGray4Image(panel, source, sizeof(source), 8, 1, true));
    TEST_ASSERT_EQUAL(0, panel.calls);
}

void test_driver_failure_is_reported(void) {
    Panel panel;
    panel.result = 6; // FastEPD BBEP_IO_ERROR
    const uint8_t source[] = {0x01, 0x23, 0x45, 0x67};
    TEST_ASSERT_FALSE(refreshGray4Image(panel, source, sizeof(source), 8, 1, true));
    TEST_ASSERT_EQUAL(1, panel.calls);
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_all_eight_levels_map_to_four_shades);
    RUN_TEST(test_pixels_keep_their_columns_and_rows);
    RUN_TEST(test_inkplate10_frame_preserves_last_pixel_without_overwriting_edges);
    RUN_TEST(test_incomplete_frame_does_not_modify_destination);
    RUN_TEST(test_successful_image_redraws_whole_frame_without_clearing);
    RUN_TEST(test_failed_decode_does_not_send_or_replace_frame);
    RUN_TEST(test_incomplete_image_is_never_sent_to_panel);
    RUN_TEST(test_driver_failure_is_reported);
    return UNITY_END();
}
