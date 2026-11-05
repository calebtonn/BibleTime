#!/usr/bin/env python3
"""Generate BibleTime.ico (16-256 px) for the Windows screen saver.

A warm paper clock dial on the Night palette's near-black, with ink hands and a gold accent ring, using the colors of
Sources/BibleTime/Theme.swift. Each size is drawn separately (8x supersampled) so strokes stay crisp at 16 px.
Usage: python3 make_icon.py [out.ico]   (requires Pillow)
"""
import math
import sys

from PIL import Image, ImageDraw

NIGHT = (0x0E, 0x0E, 0x10, 255)
EDGE = (0x2A, 0x2A, 0x2E, 255)
PAPER = (0xF3, 0xED, 0xE6, 255)
INK = (0x1C, 0x1A, 0x17, 255)
ACCENT = (0xC9, 0xA9, 0x6A, 255)
SIZES = [16, 20, 24, 32, 40, 48, 64, 96, 128, 256]


def draw(size: int) -> Image.Image:
    ss = 8
    n = size * ss
    img = Image.new("RGBA", (n, n), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    # Small sizes use the whole square; large ones keep a little air like Windows 11 app icons.
    inset = 0 if size <= 24 else n * 0.04
    radius = n * 0.22
    d.rounded_rectangle([inset, inset, n - 1 - inset, n - 1 - inset], radius=radius, fill=NIGHT,
                        outline=EDGE, width=max(ss, int(n * 0.012)))
    c = n / 2
    ring_r = n * (0.36 if size > 24 else 0.40)
    ring_w = max(ss, n * (0.035 if size > 32 else 0.06))
    d.ellipse([c - ring_r, c - ring_r, c + ring_r, c + ring_r], fill=ACCENT)
    dial_r = ring_r - ring_w
    d.ellipse([c - dial_r, c - dial_r, c + dial_r, c + dial_r], fill=PAPER)
    # Hour ticks at 12, 3, 6, 9 on larger sizes.
    if size >= 64:
        for k in range(4):
            a = math.radians(k * 90)
            r1, r2 = dial_r * 0.80, dial_r * 0.92
            w = n * 0.018
            d.line([(c + r1 * math.sin(a), c - r1 * math.cos(a)), (c + r2 * math.sin(a), c - r2 * math.cos(a))],
                   fill=INK, width=int(w))
    # 10:10, hands in ink with round caps.
    hand_w = max(ss * 1.2, n * (0.045 if size > 32 else 0.075))

    def hand(angle_deg: float, length: float) -> None:
        a = math.radians(angle_deg)
        x, y = c + length * math.sin(a), c - length * math.cos(a)
        d.line([(c, c), (x, y)], fill=INK, width=int(hand_w))
        r = hand_w / 2
        d.ellipse([x - r, y - r, x + r, y + r], fill=INK)

    hand(10 * 30 + 5, dial_r * 0.52)
    hand(2 * 30, dial_r * 0.76)
    cr = hand_w * 0.9
    d.ellipse([c - cr, c - cr, c + cr, c + cr], fill=ACCENT)
    return img.resize((size, size), Image.LANCZOS)


def main() -> None:
    out = sys.argv[1] if len(sys.argv) > 1 else "BibleTime.ico"
    images = [draw(s) for s in SIZES]
    images[-1].save(out, format="ICO", sizes=[(s, s) for s in SIZES], append_images=images[:-1])



if __name__ == "__main__":
    main()
