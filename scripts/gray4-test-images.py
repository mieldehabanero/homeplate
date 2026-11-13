"""Generate four-shade PNGs for the Inkplate 10 v2 hardware verification."""

from pathlib import Path
import struct
import zlib


WIDTH, HEIGHT = 1200, 825
DIGITS = (
    ("111", "101", "101", "101", "111"),
    ("010", "110", "010", "010", "111"),
    ("111", "001", "111", "100", "111"),
    ("111", "001", "111", "001", "111"),
)


def make_image(inverted):
    pixels = bytearray(WIDTH * HEIGHT)
    for y in range(HEIGHT):
        for x in range(WIDTH):
            level = x // 300
            pixels[y * WIDTH + x] = (3 - level if inverted else level) * 85

    def rectangle(x, y, w, h, value):
        for row in range(y, y + h):
            pixels[row * WIDTH + x : row * WIDTH + x + w] = bytes([value]) * w

    for band in range(4):
        level = 3 - band if inverted else band
        ink = 255 if level < 2 else 0
        for y, row in enumerate(DIGITS[level]):
            for x, bit in enumerate(row):
                if bit == "1":
                    rectangle(band * 300 + 135 + x * 10, 80 + y * 10, 10, 10, ink)

    for y in range(350, 470, 10):
        for x in range(500, 700, 10):
            value = 255 if ((x // 10 + y // 10) % 2) ^ inverted else 0
            rectangle(x, y, 10, 10, value)

    for x, y in ((4, 4), (WIDTH - 24, 4), (4, HEIGHT - 24), (WIDTH - 24, HEIGHT - 24)):
        rectangle(x, y, 20, 20, 255)
        rectangle(x + 4, y + 4, 12, 12, 0)

    assert set(pixels) == {0, 85, 170, 255}

    def chunk(kind, data):
        return (
            struct.pack(">I", len(data))
            + kind
            + data
            + struct.pack(">I", zlib.crc32(kind + data))
        )

    scanlines = b"".join(
        b"\0" + pixels[y * WIDTH : (y + 1) * WIDTH] for y in range(HEIGHT)
    )
    return (
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", struct.pack(">IIBBBBB", WIDTH, HEIGHT, 8, 0, 0, 0, 0))
        + chunk(b"IDAT", zlib.compress(scanlines))
        + chunk(b"IEND", b"")
    )


if __name__ == "__main__":
    output = Path(".pio/gray4-test")
    output.mkdir(parents=True, exist_ok=True)
    for inverted, name in ((False, "shades.png"), (True, "shades-inverted.png")):
        path = output / name
        path.write_bytes(make_image(inverted))
        print(path)
