#include "gray4.h"

bool packGray4(const uint8_t *source, size_t sourceSize, uint8_t *destination,
               size_t destinationSize, size_t width, size_t height) {
    if (!source || !destination || width == 0 || height == 0 || width % 4 != 0 ||
        height > SIZE_MAX / width) {
        return false;
    }
    const size_t pixels = width * height;
    if (sourceSize < pixels / 2 || destinationSize < pixels / 4) {
        return false;
    }
    for (size_t i = 0; i < pixels / 4; i++) {
        const uint8_t first = source[i * 2];
        const uint8_t second = source[i * 2 + 1];
        // Discard the unused top bit of each nibble, then quantize pairs of
        // adjacent gray levels: 0/1 -> 0, 2/3 -> 1, 4/5 -> 2, 6/7 -> 3.
        destination[i] = ((first & 0x60) << 1) | ((first & 0x06) << 3) |
                         ((second & 0x60) >> 3) | ((second & 0x06) >> 1);
    }
    return true;
}
