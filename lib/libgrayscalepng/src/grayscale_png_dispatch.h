#pragma once

#include "grayscale_png.h"

namespace grayscale_png {

template <typename Fallback>
bool render(const uint8_t *png, size_t size, const Target &target, const Options &options,
            Fallback fallback, Result *decoderResult = nullptr) {
    Result result = decode(png, size, target, options);
    if (decoderResult) *decoderResult = result;
    if (result == Result::Unsupported) return fallback();
    return result == Result::Rendered;
}

}
