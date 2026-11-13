#pragma once

#include <stddef.h>
#include <stdint.h>

// Convert Inkplate's two 3-bit gray pixels per byte to FastEPD's four 2-bit
// pixels per byte. Both formats put the leftmost pixel in the high bits.
bool packGray4(const uint8_t *source, size_t sourceSize, uint8_t *destination,
               size_t destinationSize, size_t width, size_t height);

// FastEPD's CLEAR_NONE and BBEP_SUCCESS are both zero. Keeping this helper
// independent of Arduino lets the native tests verify the submitted frame.
template <typename Panel>
bool refreshGray4Image(Panel &panel, const uint8_t *source, size_t sourceSize,
                      size_t width, size_t height, bool decoded) {
    if (!decoded || width == 0 || height == 0 || width % 4 != 0 ||
        height > SIZE_MAX / width) {
        return false;
    }
    if (!packGray4(source, sourceSize, panel.currentBuffer(), width * height / 4,
                   width, height)) {
        return false;
    }
    return panel.fullUpdate(0, false) == 0;
}
