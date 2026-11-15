// Decode the supplied pair with the pinned pngle implementation, without a panel.
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <vector>
#include "pngle.h"

static constexpr unsigned width = 1200, height = 825;

static void draw(pngle_t *png, uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                 const uint8_t rgba[4]) {
    auto &pixels = *static_cast<std::vector<uint8_t> *>(pngle_get_user_data(png));
    uint8_t gray = (54u * rgba[0] + 183u * rgba[1] + 19u * rgba[2]) >> 13;
    for (uint32_t row = y; row < y + h && row < height; ++row)
        for (uint32_t col = x; col < x + w && col < width; ++col)
            if (rgba[3]) pixels[row * width + col] = gray;
}

static bool decode(const char *path, std::vector<uint8_t> &pixels) {
    std::ifstream file(path, std::ios::binary);
    std::vector<uint8_t> input((std::istreambuf_iterator<char>(file)), {});
    pngle_t *png = pngle_new();
    if (!file || input.empty() || !png) { if (png) pngle_destroy(png); return false; }
    pngle_set_user_data(png, &pixels);
    pngle_set_draw_callback(png, draw);
    int consumed = pngle_feed(png, input.data(), input.size());
    bool good = consumed >= 0 && size_t(consumed) == input.size() &&
                pngle_get_width(png) == width && pngle_get_height(png) == height;
    pngle_destroy(png);
    return good;
}

int main(int argc, char **argv) {
    if (argc != 3) return 2;
    std::vector<uint8_t> color(width * height, 7), gray(width * height, 7);
    if (!decode(argv[1], color) || !decode(argv[2], gray)) return 1;
    size_t different = 0;
    unsigned maximum = 0;
    for (size_t i = 0; i < color.size(); ++i) {
        unsigned delta = unsigned(std::abs(int(color[i]) - int(gray[i])));
        different += delta != 0;
        if (delta > maximum) maximum = delta;
    }
    std::printf("{\"pixels\":%zu,\"different_pixels\":%zu,\"maximum_gray_delta\":%u}\n",
                color.size(), different, maximum);
}
