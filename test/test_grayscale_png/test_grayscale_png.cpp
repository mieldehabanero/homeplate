#include <unity.h>
#include <stdlib.h>
#include <string.h>
#include "grayscale_png.h"
#include "grayscale_png_dispatch.h"
#include "../support/png_test_helpers.h"
#include "pngle.h"
#include "png_fixtures.h"

using grayscale_png::Options;
using grayscale_png::Result;
using grayscale_png::Target;
using png_test::Bytes;

#define ASSERT_RESULT(expected, expression) \
    TEST_ASSERT_EQUAL_INT(static_cast<int>(expected), static_cast<int>(expression))

void setUp() {}
void tearDown() {}

static Target targetFor(Bytes &pixels, unsigned width, unsigned height,
                        unsigned x = 0, unsigned y = 0) {
    return {pixels.data(), pixels.size(), width, height, (width + 1) / 2, x, y};
}

static void expectLevels(unsigned depth, const Bytes &raw, const Bytes &expected) {
    auto png = png_test::image(depth, expected.size() * 2, 1, raw);
    Bytes pixels(expected.size(), 0xff);
    ASSERT_RESULT(Result::Rendered, grayscale_png::decode(
        png.data(), png.size(), targetFor(pixels, expected.size() * 2, 1)));
    TEST_ASSERT_EQUAL_MEMORY(expected.data(), pixels.data(), expected.size());
}

void test_gray4_levels() {
    expectLevels(4, {0, 0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef},
                 {0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77});
}

void test_gray8_levels() {
    Bytes raw = {0}, expected;
    for (unsigned i = 0; i < 256; ++i) raw.push_back(i);
    for (unsigned level = 0; level < 8; ++level)
        expected.insert(expected.end(), 16, (level << 4) | level);
    expectLevels(8, raw, expected);
}

void test_gray2_levels() { expectLevels(2, {0, 0x1b}, {0x02, 0x57}); }
void test_gray1_levels() { expectLevels(1, {0, 0x55}, {0x07, 0x07, 0x07, 0x07}); }

static void referencePixel(Bytes &pixels, const Target &target, unsigned x, unsigned y, uint8_t level) {
    size_t index = (target.y + y) * target.stride + (target.x + x) / 2;
    if ((target.x + x) & 1) pixels[index] = (pixels[index] & 0xf0) | level;
    else pixels[index] = (pixels[index] & 0x0f) | (level << 4);
}

static void checkImage(unsigned depth, unsigned width, unsigned height, unsigned filter,
                       size_t split = 0, unsigned x = 0, unsigned y = 0,
                       unsigned panelWidth = 0, unsigned panelHeight = 0) {
    auto samples = png_test::samples(depth, width, height);
    auto png = png_test::image(depth, width, height,
                              png_test::filtered(depth, width, height, samples, filter), split);
    if (!panelWidth) panelWidth = width + x + 3;
    if (!panelHeight) panelHeight = height + y + 2;
    Bytes pixels((panelWidth + 1) / 2 * panelHeight, 0xa9), expected = pixels;
    auto target = targetFor(pixels, panelWidth, panelHeight, x, y);
    unsigned max = (1u << depth) - 1;
    for (unsigned row = 0; row < height; ++row)
        for (unsigned col = 0; col < width; ++col) {
            // Old decoder sample scaling, independent of the optimized packing.
            uint8_t rgba = (samples[row * width + col] * 255 + max / 2) / max;
            referencePixel(expected, target, col, row, rgba / 32);
        }
    ASSERT_RESULT(Result::Rendered, grayscale_png::decode(png.data(), png.size(), target));
    TEST_ASSERT_EQUAL_MEMORY(expected.data(), pixels.data(), pixels.size());
}

void test_png_filters() {
    for (unsigned depth : {1, 2, 4, 8})
        for (unsigned filter = 0; filter <= 5; ++filter)
            checkImage(depth, 17, 9, filter);
}

void test_split_idat() {
    for (unsigned depth : {1, 2, 4, 8})
        for (size_t split : {1, 2, 7, 19}) checkImage(depth, 17, 9, 5, split);
}

void test_odd_width_and_offset() {
    for (unsigned depth : {1, 2, 4, 8})
        for (unsigned width : {1, 2, 3, 7, 8, 9, 15, 16, 17})
            for (unsigned x : {0, 1, 2, 3}) checkImage(depth, width, 3, 5, 0, x, 1);
}

void test_full_screen_and_centered() {
    checkImage(4, 1200, 825, 5, 0, 0, 0, 1200, 825);
    checkImage(8, 31, 17, 5, 0, (1200 - 31) / 2, (825 - 17) / 2, 1200, 825);
}

void test_eligibility() {
    Bytes pixels(64, 0xa9), before = pixels;
    auto target = targetFor(pixels, 16, 8);
    for (auto png : {
        png_test::image(8, 8, 1, {0, 0}, 0, 2),
        png_test::image(4, 8, 1, {0, 0}, 0, 3),
        png_test::image(8, 8, 1, {0, 0}, 0, 4),
        png_test::image(8, 8, 1, {0, 0}, 0, 6),
        png_test::image(16, 8, 1, {0, 0}),
        png_test::image(4, 8, 1, {0, 0}, 0, 0, 1)}) {
        ASSERT_RESULT(Result::Unsupported, grayscale_png::decode(png.data(), png.size(), target));
        TEST_ASSERT_EQUAL_MEMORY(before.data(), pixels.data(), pixels.size());
    }
    Bytes metadata;
    png_test::chunk(metadata, "tRNS", {0, 0});
    auto png = png_test::image(4, 8, 1, {0, 0, 0, 0, 0}, 0, 0, 0, metadata);
    ASSERT_RESULT(Result::Unsupported, grayscale_png::decode(png.data(), png.size(), target));
    TEST_ASSERT_EQUAL_MEMORY(before.data(), pixels.data(), pixels.size());
    metadata.clear();
    png_test::chunk(metadata, "acTL", {0, 0, 0, 1, 0, 0, 0, 0});
    png = png_test::image(4, 8, 1, {0, 0, 0, 0, 0}, 0, 0, 0, metadata);
    ASSERT_RESULT(Result::Unsupported, grayscale_png::decode(png.data(), png.size(), target));
}

void test_runtime_gate() {
    auto png = png_test::image(4, 8, 1, {0, 0, 0, 0, 0});
    Bytes pixels(8, 0xa9), before = pixels;
    auto target = targetFor(pixels, 8, 2);
    Options options;
    options.dither = true;
    ASSERT_RESULT(Result::Unsupported, grayscale_png::decode(png.data(), png.size(), target, options));
    options.dither = false;
    for (unsigned rotation = 1; rotation <= 3; ++rotation) {
        options.rotation = rotation;
        ASSERT_RESULT(Result::Unsupported, grayscale_png::decode(png.data(), png.size(), target, options));
    }
    options.rotation = 0;
    target.x = 1;
    ASSERT_RESULT(Result::Unsupported, grayscale_png::decode(png.data(), png.size(), target, options));
    target.x = 0;
    target.y = 2;
    ASSERT_RESULT(Result::Unsupported, grayscale_png::decode(png.data(), png.size(), target, options));
    TEST_ASSERT_EQUAL_MEMORY(before.data(), pixels.data(), pixels.size());
}

void test_metadata() {
    Bytes metadata;
    png_test::chunk(metadata, "gAMA", {0, 0, 177, 143});
    png_test::chunk(metadata, "sRGB", {0});
    png_test::chunk(metadata, "tEXt", {'N', 0, 'T'});
    auto png = png_test::image(4, 2, 1, {0, 0x4b}, 0, 0, 0, metadata);
    Bytes pixels(1, 0);
    ASSERT_RESULT(Result::Rendered, grayscale_png::decode(png.data(), png.size(), targetFor(pixels, 2, 1)));
    TEST_ASSERT_EQUAL_HEX8(0x25, pixels[0]);
}

static void expectInvalid(const Bytes &png) {
    Bytes pixels(16, 0xa9);
    ASSERT_RESULT(Result::Invalid, grayscale_png::decode(png.data(), png.size(), targetFor(pixels, 16, 2)));
}

void test_truncation() {
    auto png = png_test::image(4, 8, 1, {0, 0, 0, 0, 0});
    for (size_t size = 0; size < png.size(); ++size)
        expectInvalid(Bytes(png.begin(), png.begin() + size));
}

void test_invalid_chunks() {
    auto good = png_test::image(4, 8, 1, {0, 0, 0, 0, 0});
    Bytes bad = good;
    bad[29] ^= 1;
    expectInvalid(bad);
    bad = good;
    bad[8] = 0xff;
    expectInvalid(bad);
    bad = good;
    bad[16] = bad[17] = bad[18] = bad[19] = 0;
    png_test::fixCrc(bad, 8);
    expectInvalid(bad);
    bad = good;
    bad[26] = 1;
    png_test::fixCrc(bad, 8);
    expectInvalid(bad);
    bad = good;
    bad.push_back(0);
    expectInvalid(bad);
    size_t idat = png_test::findChunk(good, "IDAT");
    bad = good;
    bad[idat + 8] ^= 1;
    expectInvalid(bad);
    Bytes duplicate(good.begin() + 8, good.begin() + 33);
    bad = good;
    bad.insert(bad.begin() + 33, duplicate.begin(), duplicate.end());
    expectInvalid(bad);
}

void test_invalid_filter() { expectInvalid(png_test::image(4, 8, 1, {5, 0, 0, 0, 0})); }

void test_invalid_deflate_and_adler() {
    auto good = png_test::image(4, 8, 1, {0, 0, 0, 0, 0});
    size_t idat = png_test::findChunk(good, "IDAT");
    Bytes bad = good;
    bad[idat + 10] = 7;
    png_test::fixCrc(bad, idat);
    expectInvalid(bad);
    bad = good;
    size_t length = png_test::read32(bad.data() + idat);
    bad[idat + 8 + length - 1] ^= 1;
    png_test::fixCrc(bad, idat);
    expectInvalid(bad);
}

void test_wrong_output_length() {
    expectInvalid(png_test::image(4, 8, 1, {0, 0, 0, 0}));
    expectInvalid(png_test::image(4, 8, 1, {0, 0, 0, 0, 0, 0}));
    expectInvalid(png_test::image(4, 8, 1, {}));
}

static bool cancelAfterWrite(void *context) { return static_cast<Bytes *>(context)->front() != 0xff; }

void test_cancel_decode() {
    auto png = png_test::image(4, 8, 2, {0, 0, 0, 0, 0, 0, 0xff, 0xff, 0xff, 0xff});
    Bytes pixels(8, 0xff);
    Options options;
    options.cancelled = cancelAfterWrite;
    options.context = &pixels;
    ASSERT_RESULT(Result::Cancelled, grayscale_png::decode(png.data(), png.size(), targetFor(pixels, 8, 2), options));
    TEST_ASSERT_EQUAL_HEX8(0x00, pixels[0]);
    TEST_ASSERT_EQUAL_HEX8(0xff, pixels[4]);
}

static void *failAllocation(size_t) { return nullptr; }

void test_allocation_failure() {
    auto png = png_test::image(4, 8, 1, {0, 0, 0, 0, 0});
    Bytes pixels(4, 0xa9), before = pixels;
    Options options;
    options.allocate = failAllocation;
    options.release = free;
    ASSERT_RESULT(Result::Unsupported, grayscale_png::decode(png.data(), png.size(), targetFor(pixels, 8, 1), options));
    TEST_ASSERT_EQUAL_MEMORY(before.data(), pixels.data(), pixels.size());
}

void test_invalid_target() {
    auto png = png_test::image(4, 8, 1, {0, 0, 0, 0, 0});
    Bytes pixels(4, 0xa9);
    auto target = targetFor(pixels, 8, 1);
    target.capacity = 3;
    ASSERT_RESULT(Result::Invalid, grayscale_png::decode(png.data(), png.size(), target));
    target.capacity = 4;
    target.stride = 3;
    ASSERT_RESULT(Result::Invalid, grayscale_png::decode(png.data(), png.size(), target));
    target.pixels = nullptr;
    ASSERT_RESULT(Result::Invalid, grayscale_png::decode(png.data(), png.size(), target));
}

struct Reference {
    Bytes pixels;
    Target target;
    explicit Reference(unsigned width, unsigned height)
        : pixels((width + 1) / 2 * height, 0xff), target(targetFor(pixels, width, height)) {}
};

static void drawReference(pngle_t *png, uint32_t x, uint32_t y, uint32_t, uint32_t, const uint8_t rgba[4]) {
    auto &reference = *static_cast<Reference *>(pngle_get_user_data(png));
    uint8_t level = (54u * rgba[0] + 183u * rgba[1] + 19u * rgba[2]) >> 13;
    referencePixel(reference.pixels, reference.target, x, y, level);
}

void test_reference_equivalence() {
    for (unsigned depth : {1, 2, 4, 8}) {
        unsigned width = 33, height = 19;
        auto values = png_test::samples(depth, width, height);
        auto png = png_test::image(depth, width, height,
                                  png_test::filtered(depth, width, height, values, 5));
        Reference reference(width, height);
        pngle_t *decoder = pngle_new();
        TEST_ASSERT_NOT_NULL(decoder);
        pngle_set_user_data(decoder, &reference);
        pngle_set_draw_callback(decoder, drawReference);
        int consumed = pngle_feed(decoder, png.data(), png.size());
        pngle_destroy(decoder);
        TEST_ASSERT_EQUAL_INT(png.size(), consumed);
        Bytes pixels(reference.pixels.size(), 0xff);
        ASSERT_RESULT(Result::Rendered, grayscale_png::decode(png.data(), png.size(), targetFor(pixels, width, height)));
        TEST_ASSERT_EQUAL_MEMORY(reference.pixels.data(), pixels.data(), pixels.size());
    }
}

void test_compressed_reference_equivalence() {
    for (const auto &fixture : pngFixtures) {
        Reference reference(fixture.width, fixture.height);
        pngle_t *decoder = pngle_new();
        TEST_ASSERT_NOT_NULL(decoder);
        pngle_set_user_data(decoder, &reference);
        pngle_set_draw_callback(decoder, drawReference);
        int consumed = pngle_feed(decoder, fixture.data, fixture.size);
        pngle_destroy(decoder);
        TEST_ASSERT_EQUAL_INT(fixture.size, consumed);
        Bytes pixels(reference.pixels.size(), 0xff);
        ASSERT_RESULT(Result::Rendered, grayscale_png::decode(fixture.data, fixture.size,
            targetFor(pixels, fixture.width, fixture.height)));
        TEST_ASSERT_EQUAL_MEMORY_MESSAGE(reference.pixels.data(), pixels.data(), pixels.size(), fixture.name);
    }
}

void test_chunk_order_and_extra_stream_data() {
    auto good = png_test::image(4, 8, 1, {0, 0, 0, 0, 0}, 3);
    Bytes bad = good, metadata;
    png_test::chunk(metadata, "tEXt", {'N', 0, 'T'});
    size_t idat = png_test::findChunk(good, "IDAT");
    bad.insert(bad.begin() + idat + 15, metadata.begin(), metadata.end());
    expectInvalid(bad);
    good = png_test::image(4, 8, 1, {0, 0, 0, 0, 0});
    bad = good;
    metadata.clear();
    png_test::chunk(metadata, "IDAT", {0});
    bad.insert(bad.end() - 12, metadata.begin(), metadata.end());
    expectInvalid(bad);
}

static unsigned allocations, releases;
static void *countAllocation(size_t size) { ++allocations; return malloc(size); }
static void countRelease(void *memory) { ++releases; free(memory); }

void test_workspace_release() {
    allocations = releases = 0;
    Options options;
    options.allocate = countAllocation;
    options.release = countRelease;
    Bytes pixels(8, 0xff);
    for (const auto &raw : {Bytes{0, 0, 0, 0, 0}, Bytes{5, 0, 0, 0, 0}, Bytes{0, 0}}) {
        auto png = png_test::image(4, 8, 1, raw);
        grayscale_png::decode(png.data(), png.size(), targetFor(pixels, 8, 2), options);
        TEST_ASSERT_EQUAL_UINT(allocations, releases);
    }
    TEST_ASSERT_EQUAL_UINT(3, allocations);
    options.allocate = failAllocation;
    auto png = png_test::image(4, 8, 1, {0, 0, 0, 0, 0});
    ASSERT_RESULT(Result::Unsupported, grayscale_png::decode(png.data(), png.size(),
        targetFor(pixels, 8, 2), options));
    TEST_ASSERT_EQUAL_UINT(3, releases);
    options.allocate = countAllocation;
    options.cancelled = cancelAfterWrite;
    options.context = &pixels;
    std::fill(pixels.begin(), pixels.end(), 0xff);
    png = png_test::image(4, 8, 2, {0, 0, 0, 0, 0, 0, 0, 0, 0, 0});
    ASSERT_RESULT(Result::Cancelled, grayscale_png::decode(png.data(), png.size(),
        targetFor(pixels, 8, 2), options));
    TEST_ASSERT_EQUAL_UINT(4, allocations);
    TEST_ASSERT_EQUAL_UINT(4, releases);
}

void test_mutations_preserve_guards() {
    auto good = png_test::image(4, 8, 2, {0, 0, 0, 0, 0, 0, 0xff, 0xff, 0xff, 0xff});
    for (size_t i = 0; i < good.size(); ++i) {
        Bytes mutated = good, pixels(10, 0xa9);
        mutated[i] ^= 0xff;
        Target target = {pixels.data() + 1, 8, 8, 2, 4, 0, 0};
        grayscale_png::decode(mutated.data(), mutated.size(), target);
        TEST_ASSERT_EQUAL_HEX8(0xa9, pixels.front());
        TEST_ASSERT_EQUAL_HEX8(0xa9, pixels.back());
    }
}

void test_render_dispatch() {
    auto png = png_test::image(4, 8, 1, {0, 0, 0, 0, 0});
    Bytes pixels(4, 0xff);
    auto target = targetFor(pixels, 8, 1);
    unsigned fallbackCalls = 0;
    auto fallback = [&]() { ++fallbackCalls; return true; };
    Options options;
    Result result;
    TEST_ASSERT_TRUE(grayscale_png::render(png.data(), png.size(), target, options, fallback, &result));
    TEST_ASSERT_EQUAL_UINT(0, fallbackCalls);
    ASSERT_RESULT(Result::Rendered, result);
    options.dither = true;
    TEST_ASSERT_TRUE(grayscale_png::render(png.data(), png.size(), target, options, fallback, &result));
    TEST_ASSERT_EQUAL_UINT(1, fallbackCalls);
    ASSERT_RESULT(Result::Unsupported, result);
    options.dither = false;
    png[29] ^= 1;
    TEST_ASSERT_FALSE(grayscale_png::render(png.data(), png.size(), target, options, fallback, &result));
    TEST_ASSERT_EQUAL_UINT(1, fallbackCalls);
    ASSERT_RESULT(Result::Invalid, result);
    png[29] ^= 1;
    options.dither = true;
    TEST_ASSERT_FALSE(grayscale_png::render(png.data(), png.size(), target, options,
        [&]() { ++fallbackCalls; return false; }, &result));
    TEST_ASSERT_EQUAL_UINT(2, fallbackCalls);
    options.dither = false;
    options.allocate = failAllocation;
    options.release = free;
    TEST_ASSERT_TRUE(grayscale_png::render(png.data(), png.size(), target, options, fallback, &result));
    TEST_ASSERT_EQUAL_UINT(3, fallbackCalls);
    options.cancelled = [](void *) { return true; };
    TEST_ASSERT_FALSE(grayscale_png::render(png.data(), png.size(), target, options, fallback, &result));
    ASSERT_RESULT(Result::Cancelled, result);
    TEST_ASSERT_EQUAL_UINT(3, fallbackCalls);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_gray4_levels);
    RUN_TEST(test_gray8_levels);
    RUN_TEST(test_gray2_levels);
    RUN_TEST(test_gray1_levels);
    RUN_TEST(test_png_filters);
    RUN_TEST(test_split_idat);
    RUN_TEST(test_odd_width_and_offset);
    RUN_TEST(test_full_screen_and_centered);
    RUN_TEST(test_eligibility);
    RUN_TEST(test_runtime_gate);
    RUN_TEST(test_metadata);
    RUN_TEST(test_truncation);
    RUN_TEST(test_invalid_chunks);
    RUN_TEST(test_invalid_filter);
    RUN_TEST(test_invalid_deflate_and_adler);
    RUN_TEST(test_wrong_output_length);
    RUN_TEST(test_cancel_decode);
    RUN_TEST(test_allocation_failure);
    RUN_TEST(test_invalid_target);
    RUN_TEST(test_reference_equivalence);
    RUN_TEST(test_compressed_reference_equivalence);
    RUN_TEST(test_chunk_order_and_extra_stream_data);
    RUN_TEST(test_workspace_release);
    RUN_TEST(test_mutations_preserve_guards);
    RUN_TEST(test_render_dispatch);
    return UNITY_END();
}
