#include "grayscale_png.h"

#if defined(ARDUINO_INKPLATE10V2) || defined(HOMEPLATE_NATIVE_PNG)

#include <stdlib.h>
#include <string.h>
#ifdef HOMEPLATE_NATIVE_PNG
#include "miniz.h"
#else
#include <graphics/pngle/miniz.h>
#endif

namespace grayscale_png {
namespace {

constexpr uint32_t IHDR = 0x49484452, IDAT = 0x49444154, IEND = 0x49454e44;
constexpr uint32_t PLTE = 0x504c5445, tRNS = 0x74524e53, acTL = 0x6163544c;

uint32_t read32(const uint8_t *data) {
    return (uint32_t(data[0]) << 24) | (uint32_t(data[1]) << 16) |
           (uint32_t(data[2]) << 8) | data[3];
}

bool cancelled(const Options &options) {
    return options.cancelled && options.cancelled(options.context);
}

struct Info {
    uint32_t width, height;
    uint8_t depth;
    size_t compressedSize;
};

Result inspect(const uint8_t *png, size_t size, const Target &target,
               const Options &options, Info &info) {
    static const uint8_t signature[] = {137, 80, 78, 71, 13, 10, 26, 10};
    if (!png || size < 33 || memcmp(png, signature, sizeof(signature)) ||
        read32(png + 8) != 13 || read32(png + 12) != IHDR)
        return Result::Invalid;

    if (png[25] != 0 || png[24] == 16 || png[28] == 1) return Result::Unsupported;
    info.width = read32(png + 16);
    info.height = read32(png + 20);
    info.depth = png[24];
    info.compressedSize = 0;
    if (!info.width || !info.height || info.width > 0x7fffffff || info.height > 0x7fffffff ||
        (info.depth != 1 && info.depth != 2 && info.depth != 4 && info.depth != 8) ||
        png[26] != 0 || png[27] != 0 || png[28] != 0)
        return Result::Invalid;
    if (target.x >= target.width || target.y >= target.height ||
        info.width > target.width - target.x || info.height > target.height - target.y)
        return Result::Unsupported;

    bool sawData = false, endedData = false;
    for (size_t offset = 8; size - offset >= 12;) {
        if (cancelled(options)) return Result::Cancelled;
        uint32_t length = read32(png + offset), type = read32(png + offset + 4);
        if (length > 0x7fffffff || length > size - offset - 12) return Result::Invalid;
        const uint8_t *data = png + offset + 8;
        if (mz_crc32(MZ_CRC32_INIT, png + offset + 4, size_t(length) + 4) != read32(data + length))
            return Result::Invalid;

        if (type == IHDR) {
            if (offset != 8) return Result::Invalid;
        } else if (type == IDAT) {
            if (endedData) return Result::Invalid;
            sawData = true;
            info.compressedSize += length;
        } else {
            if (sawData) endedData = true;
            if (type == IEND)
                return !length && sawData && info.compressedSize && offset + 12 == size
                    ? Result::Rendered : Result::Invalid;
            if (type == tRNS || type == acTL) return Result::Unsupported;
            if (type == PLTE) return Result::Invalid; // prohibited for grayscale
            if (!(png[offset + 4] & 0x20)) return Result::Unsupported; // unknown critical chunk
            if ((type == 0x67414d41 && length != 4) || // gAMA
                (type == 0x624b4744 && length != 2))   // bKGD
                return Result::Invalid;
        }
        offset += size_t(length) + 12;
    }
    return Result::Invalid;
}

struct Workspace {
    tinfl_decompressor inflater;
    uint8_t dictionary[TINFL_LZ_DICT_SIZE];
};

struct Allocation {
    void *memory;
    void (*release)(void *);
    ~Allocation() { if (memory) release(memory); }
};

template <unsigned Depth>
uint8_t gray(const uint8_t *row, uint32_t x) {
    unsigned sample = (row[size_t(x) * Depth / 8] >> (8 - Depth - x * Depth % 8)) & ((1u << Depth) - 1);
    if (Depth == 1) return sample ? 7 : 0;
    if (Depth == 2) {
        static const uint8_t levels[] = {0, 2, 5, 7};
        return levels[sample];
    }
    return sample >> (Depth == 4 ? 1 : 5);
}

template <unsigned Depth>
void writeRow(const uint8_t *row, uint32_t width, uint32_t y, const Target &target) {
    uint8_t *destination = target.pixels + (size_t(target.y) + y) * target.stride + target.x / 2;
    uint32_t x = 0;
    if (target.x & 1) {
        *destination = (*destination & 0xf0) | gray<Depth>(row, x++);
        ++destination;
    }
    for (; x + 1 < width; x += 2) {
        if (Depth == 4 && !(x & 1)) *destination++ = (row[x / 2] >> 1) & 0x77;
        else *destination++ = (gray<Depth>(row, x) << 4) | gray<Depth>(row, x + 1);
    }
    if (x < width) *destination = (*destination & 0x0f) | (gray<Depth>(row, x) << 4);
}

int paeth(int a, int b, int c) {
    int p = a + b - c, da = abs(p - a), db = abs(p - b), dc = abs(p - c);
    return da <= db && da <= dc ? a : db <= dc ? b : c;
}

struct Rows {
    const Info &info;
    const Target &target;
    const Options &options;
    uint8_t *current;
    uint8_t *previous;
    size_t rowSize;
    size_t filled = 0;
    uint32_t count = 0;

    Rows(const Info &imageInfo, const Target &destination, const Options &decodeOptions,
         uint8_t *storage, size_t bytesPerRow)
        : info(imageInfo), target(destination), options(decodeOptions), current(storage),
          previous(storage + bytesPerRow), rowSize(bytesPerRow) {
        memset(previous, 0, rowSize);
    }

    Result consume(const uint8_t *data, size_t size) {
        while (size) {
            if (cancelled(options)) return Result::Cancelled;
            if (count >= info.height) return Result::Invalid;
            size_t length = size < rowSize - filled ? size : rowSize - filled;
            memcpy(current + filled, data, length);
            filled += length;
            data += length;
            size -= length;
            if (filled != rowSize) continue;

            unsigned filter = current[0];
            if (filter > 4) return Result::Invalid;
            // Keep these original-precision bytes for the next row's PNG filter.
            for (size_t i = 1; i < rowSize; ++i) {
                int left = i > 1 ? current[i - 1] : 0;
                int up = previous[i], corner = i > 1 ? previous[i - 1] : 0;
                switch (filter) {
                    case 1: current[i] += left; break;
                    case 2: current[i] += up; break;
                    case 3: current[i] += (left + up) / 2; break;
                    case 4: current[i] += paeth(left, up, corner); break;
                }
            }
            switch (info.depth) {
                case 1: writeRow<1>(current + 1, info.width, count, target); break;
                case 2: writeRow<2>(current + 1, info.width, count, target); break;
                case 4: writeRow<4>(current + 1, info.width, count, target); break;
                case 8: writeRow<8>(current + 1, info.width, count, target); break;
            }
            ++count;
            uint8_t *temporary = previous;
            previous = current;
            current = temporary;
            filled = 0;
        }
        return cancelled(options) ? Result::Cancelled : Result::Rendered;
    }
};

}

Result decode(const uint8_t *png, size_t size, const Target &target, const Options &options) {
    if (options.dither || options.rotation) return Result::Unsupported;
    size_t minimumStride = size_t(target.width / 2) + (target.width & 1);
    if (!target.pixels || !target.width || !target.height || !target.stride ||
        target.stride < minimumStride || target.height > target.capacity / target.stride ||
        bool(options.allocate) != bool(options.release))
        return Result::Invalid;
    Info info;
    Result inspected = inspect(png, size, target, options, info);
    if (inspected != Result::Rendered) return inspected;

    // Two packed source rows plus the filter bytes, never an expanded RGB image.
    if (info.width > (SIZE_MAX - 7) / info.depth) return Result::Invalid;
    size_t rowSize = (size_t(info.width) * info.depth + 7) / 8 + 1;
    if (rowSize > (SIZE_MAX - sizeof(Workspace)) / 2) return Result::Invalid;
    auto allocate = options.allocate ? options.allocate : malloc;
    auto release = options.release ? options.release : free;
    Allocation storage = {allocate(sizeof(Workspace) + 2 * rowSize), release};
    if (!storage.memory) return Result::Unsupported;
    auto *workspace = static_cast<Workspace *>(storage.memory);
    tinfl_init(&workspace->inflater);
    Rows rows(info, target, options, reinterpret_cast<uint8_t *>(workspace + 1), rowSize);
    size_t dictionaryOffset = 0, remaining = info.compressedSize;

    for (size_t offset = 33; size - offset >= 12;) {
        size_t length = read32(png + offset);
        uint32_t type = read32(png + offset + 4);
        const uint8_t *input = png + offset + 8;
        offset += length + 12;
        if (type != IDAT) continue;
        tinfl_status status = TINFL_STATUS_NEEDS_MORE_INPUT;
        while (length || status == TINFL_STATUS_HAS_MORE_OUTPUT) {
            if (cancelled(options)) return Result::Cancelled;
            size_t consumed = length, produced = TINFL_LZ_DICT_SIZE - dictionaryOffset;
            uint32_t flags = TINFL_FLAG_PARSE_ZLIB_HEADER;
            if (remaining > length) flags |= TINFL_FLAG_HAS_MORE_INPUT;
            status = tinfl_decompress(&workspace->inflater, input, &consumed,
                                     workspace->dictionary, workspace->dictionary + dictionaryOffset,
                                     &produced, flags);
            if (status < 0) return Result::Invalid;
            Result result = rows.consume(workspace->dictionary + dictionaryOffset, produced);
            if (result != Result::Rendered) return result;
            dictionaryOffset = (dictionaryOffset + produced) & (TINFL_LZ_DICT_SIZE - 1);
            input += consumed;
            length -= consumed;
            remaining -= consumed;
            if (status == TINFL_STATUS_DONE)
                return !remaining && rows.count == info.height && !rows.filled
                    ? Result::Rendered : Result::Invalid;
            if (!consumed && !produced) return Result::Invalid;
        }
    }
    return Result::Invalid;
}
}
#endif
