#pragma once

#include <stddef.h>
#include <stdint.h>

namespace grayscale_png {

enum class Result { Rendered, Unsupported, Invalid, Cancelled };

struct Target {
    uint8_t *pixels;
    size_t capacity;
    uint32_t width;
    uint32_t height;
    size_t stride;
    uint32_t x;
    uint32_t y;
};

struct Options {
    bool dither = false;
    uint8_t rotation = 0;
    bool (*cancelled)(void *) = nullptr;
    void *context = nullptr;
    // Supply both hooks or neither. Unsupported is returned on allocation failure.
    void *(*allocate)(size_t) = nullptr;
    void (*release)(void *) = nullptr;
};

// Unsupported leaves the destination untouched, permitting the caller to fall back.
// Invalid/Cancelled may leave partial rows; the caller must discard that render.
Result decode(const uint8_t *png, size_t size, const Target &target,
              const Options &options = Options());

}
