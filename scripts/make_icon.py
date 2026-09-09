#!/usr/bin/env python3
"""Generate the original FastPDF application icon (multi-size .ico).

Produces a self-contained, original "PDF document" motif that stays legible
at small sizes: a red rounded-square badge holding a white, folded-corner
document page with a red "PDF" banner. Rendered at high resolution and
down-sampled into a standard Windows .ico container.

No third-party artwork is used; the drawing is generated from primitives.

Usage:
    python scripts/make_icon.py [output_path]
Default output: src/app/resources/fastpdf.ico
"""
from __future__ import annotations

import os
import sys

from PIL import Image, ImageDraw, ImageFilter, ImageFont

S = 1024  # master render size (square)

# Palette (original, not derived from any branded icon).
RED_TOP = (240, 82, 64)
RED_BOTTOM = (193, 22, 12)
PAGE_WHITE = (252, 252, 250)
FOLD_GRAY = (206, 211, 220)
LINE_GRAY = (176, 182, 193)
BANNER_RED = (198, 26, 15)


def _font(path_candidates, size):
    for p in path_candidates:
        try:
            return ImageFont.truetype(p, size)
        except OSError:
            continue
    return ImageFont.load_default()


def render_master() -> Image.Image:
    img = Image.new("RGBA", (S, S), (0, 0, 0, 0))

    # --- Red rounded-square badge with a subtle vertical gradient. ---
    grad = Image.new("RGB", (S, S), RED_TOP)
    g = ImageDraw.Draw(grad)
    for y in range(S):
        t = y / (S - 1)
        r = int(RED_TOP[0] + (RED_BOTTOM[0] - RED_TOP[0]) * t)
        gg = int(RED_TOP[1] + (RED_BOTTOM[1] - RED_TOP[1]) * t)
        b = int(RED_TOP[2] + (RED_BOTTOM[2] - RED_TOP[2]) * t)
        g.line([(0, y), (S, y)], fill=(r, gg, b))
    radius = int(0.215 * S)
    mask = Image.new("L", (S, S), 0)
    ImageDraw.Draw(mask).rounded_rectangle([0, 0, S - 1, S - 1], radius=radius, fill=255)
    img.paste(grad, (0, 0), mask)

    d = ImageDraw.Draw(img)

    # --- White document page with a folded top-right corner. ---
    px0, py0, px1, py1 = int(0.245 * S), int(0.155 * S), int(0.755 * S), int(0.845 * S)
    fold = int(0.175 * S)
    page_poly = [
        (px0, py0),
        (px1 - fold, py0),
        (px1, py0 + fold),
        (px1, py1),
        (px0, py1),
    ]
    # Soft drop shadow under the page for depth.
    shadow = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    sd = ImageDraw.Draw(shadow)
    sd.polygon([(x + 10, y + 16) for x, y in page_poly], fill=(60, 8, 4, 150))
    shadow = shadow.filter(ImageFilter.GaussianBlur(14))
    img.alpha_composite(shadow)
    d = ImageDraw.Draw(img)  # refresh after composite

    d.polygon(page_poly, fill=PAGE_WHITE)

    # Folded corner flap (light gray triangle).
    d.polygon(
        [(px1 - fold, py0), (px1, py0 + fold), (px1 - fold, py0 + fold)],
        fill=FOLD_GRAY,
    )

    # --- Faint text lines on the page. ---
    lx0 = px0 + int(0.06 * S)
    lx1 = px1 - int(0.06 * S)
    line_h = int(0.028 * S)
    gap = int(0.055 * S)
    y = py0 + fold + int(0.06 * S)
    widths = [1.0, 0.86, 0.94, 0.72]
    for i, wfrac in enumerate(widths):
        # leave room for the banner lower on the page
        if y > py1 - int(0.26 * S):
            break
        x_end = lx0 + int((lx1 - lx0) * wfrac)
        d.rounded_rectangle([lx0, y, x_end, y + line_h], radius=line_h // 2, fill=LINE_GRAY)
        y += gap

    # --- Red "PDF" banner near the lower third of the page. ---
    bx0 = px0 + int(0.05 * S)
    bx1 = px1 - int(0.05 * S)
    by0 = py1 - int(0.235 * S)
    by1 = py1 - int(0.055 * S)
    d.rounded_rectangle([bx0, by0, bx1, by1], radius=int(0.03 * S), fill=BANNER_RED)
    font = _font(
        [
            os.path.join(os.environ.get("WINDIR", r"C:\\Windows"), "Fonts", "arialbd.ttf"),
            os.path.join(os.environ.get("WINDIR", r"C:\\Windows"), "Fonts", "segoeuib.ttf"),
        ],
        int(0.135 * S),
    )
    text = "PDF"
    bbox = d.textbbox((0, 0), text, font=font)
    tw, th = bbox[2] - bbox[0], bbox[3] - bbox[1]
    tx = bx0 + ((bx1 - bx0) - tw) // 2 - bbox[0]
    ty = by0 + ((by1 - by0) - th) // 2 - bbox[1]
    d.text((tx, ty), text, font=font, fill=(255, 255, 255))

    return img


def main() -> int:
    here = os.path.dirname(os.path.abspath(__file__))
    root = os.path.dirname(here)
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(
        root, "src", "app", "resources", "fastpdf.ico"
    )
    os.makedirs(os.path.dirname(out), exist_ok=True)

    master = render_master()
    sizes = [(256, 256), (128, 128), (64, 64), (48, 48), (32, 32), (24, 24), (16, 16)]
    master.save(out, format="ICO", sizes=sizes)
    print(f"wrote {out} ({os.path.getsize(out)} bytes) sizes={sizes}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
