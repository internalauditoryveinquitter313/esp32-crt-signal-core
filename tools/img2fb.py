#!/usr/bin/env python3
"""Convert image to C array for crt_fb framebuffer (INDEXED8 grayscale)."""

import sys
from PIL import Image, ImageEnhance, ImageOps
from pathlib import Path

FB_WIDTH = 256
FB_HEIGHT = 240  # NTSC active lines (must match crt_timing profile)

def process_image(src: str) -> Image.Image:
    """Load image, resize to framebuffer, apply CRT gamma + enhance. Returns L-mode canvas."""
    import numpy as np

    img = Image.open(src)

    # Fit to framebuffer maintaining aspect ratio, fill black
    img.thumbnail((FB_WIDTH, FB_HEIGHT), Image.LANCZOS)
    canvas = Image.new("L", (FB_WIDTH, FB_HEIGHT), 0)

    # Center the image
    ox = (FB_WIDTH - img.width) // 2
    oy = (FB_HEIGHT - img.height) // 2

    # Convert to grayscale with perceptual weights
    gray = img.convert("L")

    # CRT gamma correction: compress highlights, expand shadows.
    # CRT phosphor has ~2.2 gamma; webcam clips whites easily.
    # Apply inverse gamma (darken overall) so CRT renders with full range.
    arr = np.array(gray, dtype=np.float32) / 255.0
    arr = np.power(arr, 1.4)  # gamma > 1 = darken, preserves detail in highlights
    arr = np.clip(arr * 255, 0, 255).astype(np.uint8)
    gray = Image.fromarray(arr, mode="L")

    gray = ImageOps.autocontrast(gray, cutoff=2)
    gray = ImageEnhance.Contrast(gray).enhance(1.4)
    gray = ImageEnhance.Sharpness(gray).enhance(1.8)

    canvas.paste(gray, (ox, oy))
    return canvas


def convert_raw(src: str, out: str):
    """Convert image to raw binary pixels for serial upload to ESP32."""
    canvas = process_image(src)
    pixels = bytes(canvas.tobytes())
    Path(out).write_bytes(pixels)
    print(f"Generated {out}: {FB_WIDTH}x{FB_HEIGHT} = {len(pixels)} bytes", file=sys.stderr)


def convert(src: str, out: str, name: str = "godzilla"):
    canvas = process_image(src)

    # Generate C header
    pixels = list(canvas.tobytes())
    lines = [
        "#pragma once",
        f"/* Auto-generated from {Path(src).name} — {FB_WIDTH}x{FB_HEIGHT} grayscale */",
        f"#define {name.upper()}_WIDTH  {FB_WIDTH}",
        f"#define {name.upper()}_HEIGHT {FB_HEIGHT}",
        "",
        f"static const uint8_t {name}_pixels[{FB_WIDTH * FB_HEIGHT}] = {{",
    ]

    # Write rows with comment markers for readability
    for y in range(FB_HEIGHT):
        row = pixels[y * FB_WIDTH : (y + 1) * FB_WIDTH]
        hex_vals = ", ".join(f"0x{p:02x}" for p in row)
        lines.append(f"    /* y={y:3d} */ {hex_vals},")

    lines.append("};")
    lines.append("")

    Path(out).write_text("\n".join(lines))
    print(f"Generated {out}: {FB_WIDTH}x{FB_HEIGHT} = {len(pixels)} bytes")
    print(f"  Luminance range: {min(pixels)}..{max(pixels)}")

    # Also save preview
    preview = Path(out).with_suffix(".png")
    canvas.save(str(preview))
    print(f"  Preview: {preview}")

if __name__ == "__main__":
    if "--raw" in sys.argv:
        sys.argv.remove("--raw")
        src = sys.argv[1] if len(sys.argv) > 1 else "docs/godzilla-8bit.jpg"
        out = sys.argv[2] if len(sys.argv) > 2 else src.rsplit(".", 1)[0] + ".bin"
        convert_raw(src, out)
    else:
        src = sys.argv[1] if len(sys.argv) > 1 else "docs/godzilla-8bit.jpg"
        out = sys.argv[2] if len(sys.argv) > 2 else "main/godzilla_img.h"
        name = sys.argv[3] if len(sys.argv) > 3 else "godzilla"
        convert(src, out, name)
