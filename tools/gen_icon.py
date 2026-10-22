"""Generates manager/app.ico (and an optional PNG preview).

Design: a dark-blue rounded square with a 2x2 grid of texture tiles, one of which is
swapped out for a bright orange tile popping out of the grid - "texture replacement".
Pure stdlib: shapes are rasterised with signed distances and 4x4 supersampling.

    python tools/gen_icon.py manager/app.ico [preview.png]
"""
import struct
import sys
import zlib

SIZES = [16, 20, 24, 32, 40, 48, 64, 128, 256]
SAMPLES = 4


def rounded_rect_sdf(px, py, x0, y0, x1, y1, r):
    cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
    hx, hy = (x1 - x0) / 2 - r, (y1 - y0) / 2 - r
    dx, dy = abs(px - cx) - hx, abs(py - cy) - hy
    ox, oy = max(dx, 0.0), max(dy, 0.0)
    return (ox * ox + oy * oy) ** 0.5 + min(max(dx, dy), 0.0) - r


def lerp(a, b, t):
    return tuple(a[i] + (b[i] - a[i]) * t for i in range(len(a)))


BG_TOP = (52, 72, 160)
BG_BOTTOM = (20, 26, 62)
TILE = (88, 104, 168)
TILE_DIM = (64, 78, 136)
ACCENT = (255, 140, 56)
ACCENT_HI = (255, 196, 110)
SHADOW = (8, 10, 26)

class Layout:
    """Geometry in unit coordinates. Small sizes (taskbar, title bar) get a bigger grid and
    less padding so the shapes stay readable instead of shrinking into the background."""

    def __init__(self, size):
        small = size <= 32
        self.inset = 0.0 if small else 0.02         # background square margin
        g0 = 0.14 if small else 0.17                # grid start (and end margin)
        gap = 0.07 if small else 0.06
        self.t = (1.0 - 2 * g0 - gap) / 2          # tile size
        self.tiles = [  # (x, y, colour) for the three original tiles
            (g0, g0, TILE_DIM),
            (g0, g0 + self.t + gap, TILE),
            (g0 + self.t + gap, g0 + self.t + gap, TILE_DIM),
        ]
        # The replacement tile: top-right slot, a little larger, centred on the slot
        # horizontally and lifted slightly so it pops out of the grid without leaning right.
        self.a_size = self.t * 1.16
        self.ax = g0 + self.t + gap - (self.a_size - self.t) / 2
        self.ay = g0 - (self.a_size - self.t) / 2 - 0.03


def shade(u, v, L):
    """Colour (r, g, b, a) at unit coordinates (u, v) in [0, 1]."""
    if rounded_rect_sdf(u, v, L.inset, L.inset, 1 - L.inset, 1 - L.inset, 0.22) > 0:
        return (0, 0, 0, 0)
    col = lerp(BG_TOP, BG_BOTTOM, min(1.0, max(0.0, (u * 0.35 + v * 0.65))))
    for x, y, c in L.tiles:
        if rounded_rect_sdf(u, v, x, y, x + L.t, y + L.t, 0.05) <= 0:
            col = c
    # Soft drop shadow under the replacement tile.
    ax, ay, s = L.ax, L.ay, L.a_size
    d = rounded_rect_sdf(u, v, ax + 0.025, ay + 0.035, ax + s + 0.025, ay + s + 0.035, 0.06)
    if d <= 0.03:
        col = lerp(col, SHADOW, 0.55 * min(1.0, (0.03 - d) / 0.03))
    if rounded_rect_sdf(u, v, ax, ay, ax + s, ay + s, 0.06) <= 0:
        col = lerp(ACCENT_HI, ACCENT, min(1.0, max(0.0, ((u - ax) + (v - ay)) / (2 * s))))
    return (*col, 255)


def render(size):
    layout = Layout(size)
    rows = []
    for py in range(size):
        row = bytearray()
        for px in range(size):
            acc = [0.0, 0.0, 0.0, 0.0]
            for sy in range(SAMPLES):
                for sx in range(SAMPLES):
                    r, g, b, a = shade((px + (sx + 0.5) / SAMPLES) / size, (py + (sy + 0.5) / SAMPLES) / size, layout)
                    acc[0] += r * a
                    acc[1] += g * a
                    acc[2] += b * a
                    acc[3] += a
            n = SAMPLES * SAMPLES
            alpha = acc[3] / n
            if alpha > 0:
                row += bytes(int(round(acc[i] / acc[3])) for i in range(3)) + bytes([int(round(alpha))])
            else:
                row += b"\0\0\0\0"
        rows.append(bytes(row))
    return rows


def png(size, rows):
    def chunk(tag, data):
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    raw = b"".join(b"\0" + r for r in rows)
    ihdr = struct.pack(">IIBBBBB", size, size, 8, 6, 0, 0, 0)
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")


def main(ico_path, preview_path=None):
    images = [(s, png(s, render(s))) for s in SIZES]
    header = struct.pack("<HHH", 0, 1, len(images))
    offset = 6 + 16 * len(images)
    entries, blobs = b"", b""
    for size, data in images:
        dim = 0 if size >= 256 else size
        entries += struct.pack("<BBBBHHII", dim, dim, 0, 0, 1, 32, len(data), offset)
        offset += len(data)
        blobs += data
    with open(ico_path, "wb") as f:
        f.write(header + entries + blobs)
    if preview_path:
        for size, data in images:
            if size in (32, 256):
                with open(preview_path.replace(".png", f"_{size}.png"), "wb") as f:
                    f.write(data)


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "manager/app.ico", sys.argv[2] if len(sys.argv) > 2 else None)
