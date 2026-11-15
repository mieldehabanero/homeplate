#pragma once

#include <algorithm>
#include <cstdlib>
#include <stdint.h>
#include <vector>

namespace png_test {
using Bytes = std::vector<uint8_t>;

inline void append32(Bytes &out, uint32_t value) {
    for (int shift = 24; shift >= 0; shift -= 8) out.push_back(value >> shift);
}

inline uint32_t crc32(const uint8_t *data, size_t size) {
    uint32_t crc = 0xffffffff;
    for (size_t i = 0; i < size; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320 : 0);
    }
    return ~crc;
}

inline void chunk(Bytes &out, const char *type, const Bytes &data) {
    append32(out, data.size());
    size_t start = out.size();
    out.insert(out.end(), type, type + 4);
    out.insert(out.end(), data.begin(), data.end());
    append32(out, crc32(out.data() + start, data.size() + 4));
}

// Stored DEFLATE blocks keep hand-built fixtures independent of the decoder.
inline Bytes deflate(const Bytes &raw) {
    Bytes out = {0x78, 0x01};
    size_t offset = 0;
    do {
        size_t length = std::min<size_t>(65535, raw.size() - offset);
        out.push_back(offset + length == raw.size() ? 1 : 0);
        out.push_back(length);
        out.push_back(length >> 8);
        out.push_back(~length);
        out.push_back((~length) >> 8);
        out.insert(out.end(), raw.begin() + offset, raw.begin() + offset + length);
        offset += length;
    } while (offset < raw.size());
    uint32_t a = 1, b = 0;
    for (uint8_t value : raw) {
        a = (a + value) % 65521;
        b = (b + a) % 65521;
    }
    append32(out, (b << 16) | a);
    return out;
}

inline Bytes image(unsigned depth, unsigned width, unsigned height, const Bytes &raw,
                   size_t split = 0, unsigned color = 0, unsigned interlace = 0,
                   const Bytes &metadata = {}) {
    Bytes out = {137, 80, 78, 71, 13, 10, 26, 10};
    Bytes header;
    append32(header, width);
    append32(header, height);
    header.insert(header.end(), {static_cast<uint8_t>(depth), static_cast<uint8_t>(color),
                                 0, 0, static_cast<uint8_t>(interlace)});
    chunk(out, "IHDR", header);
    out.insert(out.end(), metadata.begin(), metadata.end());
    Bytes compressed = deflate(raw);
    if (!split) split = compressed.size();
    for (size_t offset = 0; offset < compressed.size(); offset += split) {
        size_t end = std::min(offset + split, compressed.size());
        chunk(out, "IDAT", Bytes(compressed.begin() + offset, compressed.begin() + end));
    }
    chunk(out, "IEND", {});
    return out;
}

inline uint32_t read32(const uint8_t *data) {
    return (uint32_t(data[0]) << 24) | (uint32_t(data[1]) << 16) |
           (uint32_t(data[2]) << 8) | data[3];
}

inline size_t findChunk(const Bytes &png, const char *type) {
    for (size_t offset = 8; offset + 12 <= png.size();
         offset += read32(png.data() + offset) + 12) {
        if (std::equal(type, type + 4, png.begin() + offset + 4)) return offset;
    }
    return png.size();
}

inline void fixCrc(Bytes &png, size_t offset) {
    size_t length = read32(png.data() + offset);
    uint32_t crc = crc32(png.data() + offset + 4, length + 4);
    for (unsigned i = 0; i < 4; ++i) png[offset + 8 + length + i] = crc >> (24 - i * 8);
}

inline int paeth(int a, int b, int c) {
    int p = a + b - c;
    int da = std::abs(p - a), db = std::abs(p - b), dc = std::abs(p - c);
    return da <= db && da <= dc ? a : db <= dc ? b : c;
}

inline Bytes samples(unsigned depth, unsigned width, unsigned height) {
    Bytes out(width * height);
    for (unsigned y = 0; y < height; ++y)
        for (unsigned x = 0; x < width; ++x)
            out[y * width + x] = (x + y * 3) & ((1u << depth) - 1);
    return out;
}

inline Bytes filtered(unsigned depth, unsigned width, unsigned height,
                      const Bytes &values, unsigned filter) {
    size_t pitch = (width * depth + 7) / 8;
    Bytes raw, previous(pitch, 0);
    for (unsigned y = 0; y < height; ++y) {
        Bytes row(pitch, 0);
        for (unsigned x = 0; x < width; ++x)
            row[x * depth / 8] |= values[y * width + x] << (8 - depth - x * depth % 8);
        unsigned kind = filter == 5 ? y % 5 : filter;
        raw.push_back(kind);
        for (size_t i = 0; i < pitch; ++i) {
            int left = i ? row[i - 1] : 0;
            int up = previous[i], corner = i ? previous[i - 1] : 0;
            int predictions[] = {0, left, up, (left + up) / 2, paeth(left, up, corner)};
            raw.push_back(row[i] - predictions[kind]);
        }
        previous = row;
    }
    return raw;
}
}
