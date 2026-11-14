#!/usr/bin/env python3
"""Builds the Vita LiveArea art from frames of the game's own attract mode.

Reads the frames that capture.sh recorded (the title screen),
cuts the logo out of it, puts it on black and writes 8-bit indexed PNGs, the format the Vita needs, to
tools/vita/sce_sys_local/. CMake packs that folder into the .vpk instead of tools/vita/sce_sys/ when
it exists.

The output is copyrighted game art: sce_sys_local/ is in .gitignore and must never be committed.

Usage: python3 -I tools/vita/livearea/compose.py [frames_dir] [out_dir]
Needs Pillow.
"""

import glob
import os
import sys

from PIL import Image, ImageDraw, ImageFont

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
FRAMES_DIR = sys.argv[1] if len(sys.argv) > 1 else os.path.join(REPO_ROOT, "build", "livearea-frames")
OUT_DIR = sys.argv[2] if len(sys.argv) > 2 else os.path.join(REPO_ROOT, "tools", "vita", "sce_sys_local")


def near(c, ref, tol=24):
    return all(abs(a - b) <= tol for a, b in zip(c, ref))


def is_title(im):
    # Title screen: black corners, the orange "3rd STRIKE" letters in the middle of the logo
    black = all(near(im.getpixel(p), (0, 0, 0), 4) for p in [(5, 5), (630, 5), (5, 470)])
    if not (black and near(im.getpixel((320, 100)), (239, 189, 0), 20)):
        return False
    # The finished title screen has the copyright lines at the bottom (the intro's zoom-in does not)
    band = im.crop((20, 430, 620, 470)).convert("L")
    lit = sum(1 for v in band.tobytes() if v > 100)
    return 2500 < lit < 8000


def pick(frames, test, what):
    hits = [f for f in frames if test(f[1])]
    if not hits:
        sys.exit(f"No {what} frame found in {FRAMES_DIR}. Run capture.sh again (or set LIVEAREA_SECONDS higher).")
    # The last frame of the first run of matches: fully faded in
    first = frames.index(hits[0])
    chosen = hits[0]
    for f in frames[first:]:
        if not test(f[1]):
            break
        chosen = f
    print(f"{what}: {os.path.basename(chosen[0])}")
    return chosen[1]


CREDIT = ["3SX by the crowded-street team", "github.com/crowded-street/3sx", "Vita port: github.com/unclejahjah"]


def credit_font(size):
    for name in ["DejaVuSans.ttf", "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", "LiberationSans-Regular.ttf",
                 "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf", "arial.ttf"]:
        try:
            return ImageFont.truetype(name, size)
        except OSError:
            pass
    return ImageFont.load_default(size)


def add_credit(im, margin=14, size=15):
    # Credit for the 3SX decompilation this port is built on, bottom right
    draw = ImageDraw.Draw(im)
    font = credit_font(size)
    y = im.height - margin
    for line in reversed(CREDIT):
        left, top, right, bottom = draw.textbbox((0, 0), line, font=font)
        y -= bottom - top
        draw.text((im.width - margin - (right - left) - left, y - top), line, font=font, fill=(200, 200, 200))
        y -= 5
    return im


def floating(art, size):
    # The logo without the black around it: black connected to the border becomes transparent
    art = art.resize(size, Image.LANCZOS).convert("RGB")
    w, h = art.size
    px = art.load()
    clear = [[False] * w for _ in range(h)]
    stack = [(x, y) for x in range(w) for y in (0, h - 1)] + [(x, y) for y in range(h) for x in (0, w - 1)]
    while stack:
        x, y = stack.pop()
        if 0 <= x < w and 0 <= y < h and not clear[y][x] and max(px[x, y]) < 28:
            clear[y][x] = True
            stack += [(x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)]
    pal = art.quantize(colors=255, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.FLOYDSTEINBERG)
    data = bytearray(pal.tobytes())
    for y in range(h):
        for x in range(w):
            if clear[y][x]:
                data[y * w + x] = 255
    pal.frombytes(bytes(data))
    palette = pal.getpalette()[:255 * 3] + [0, 0, 0]
    pal.putpalette(palette)
    pal.info["transparency"] = 255
    return pal


# The start button is a floating logo (a live item that starts the game) instead of the framed gate,
# laid out like other homebrew LiveAreas
# On the Vita, x and y move the image from the centre of the frame (x=0 is centred; checked on hardware)
LOGO_W, LOGO_H, LOGO_X, LOGO_Y = 420, 247, 0, 95
TEMPLATE = f"""<?xml version="1.0" encoding="utf-8"?>
<livearea style="a\x64\x30" format-ver="01.00" content-rev="1">
  <livearea-background>
    <image>bg.png</image>
  </livearea-background>

  <frame id="frame1" multi="o" autoflip="1">
    <liveitem>
      <target>psla:eboot</target>
      <image y="{LOGO_Y}" x="{LOGO_X}" width="{LOGO_W}" height="{LOGO_H}">startup.png</image>
    </liveitem>
  </frame>
</livearea>
"""


def indexed(im):
    # The Vita only shows 8-bit palette PNGs on the LiveArea
    return im.convert("RGB").quantize(colors=256, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.FLOYDSTEINBERG)


def save(im, rel):
    path = os.path.join(OUT_DIR, rel)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    indexed(im).save(path, optimize=True)
    print(f"wrote {path} {im.size[0]}x{im.size[1]}")


def main():
    paths = sorted(glob.glob(os.path.join(FRAMES_DIR, "f*.png")))
    if not paths:
        sys.exit(f"No frames in {FRAMES_DIR}. Run tools/vita/livearea/capture.sh first.")
    frames = [(p, Image.open(p).convert("RGB").crop((0, 0, 640, 480))) for p in paths]

    title = pick(frames, is_title, "title screen")

    # The logo with "Fight for the Future", without the copyright lines; the title screen is black around it
    logo = title.crop((10, 20, 630, 385))
    emblem = title.crop((10, 20, 630, 335))  # the logo alone, without "Fight for the Future"

    def on_black(size, art, width, top=None):
        canvas = Image.new("RGB", size)
        h = art.height * width // art.width
        y = (size[1] - h) // 2 if top is None else top
        canvas.paste(art.resize((width, h), Image.LANCZOS), ((size[0] - width) // 2, y))
        return canvas

    # icon0.png 128x128: the logo on black
    save(on_black((128, 128), emblem, 124), "icon0.png")

    # bg.png 840x500: plain black with the 3SX credit; the logo is on the gate only
    save(add_credit(Image.new("RGB", (840, 500))), "livearea/contents/bg.png")

    # startup.png: the floating title logo that starts the game (see TEMPLATE)
    startup = os.path.join(OUT_DIR, "livearea", "contents", "startup.png")
    os.makedirs(os.path.dirname(startup), exist_ok=True)
    floating(logo, (LOGO_W, LOGO_H)).save(startup, transparency=255, optimize=True)
    print(f"wrote {startup} {LOGO_W}x{LOGO_H}")

    # pic0.png 960x544: shown while the bubble opens; the title screen without the copyright lines
    pic = Image.new("RGB", (960, 544))
    t = title.crop((0, 0, 640, 400))
    t = t.resize((640 * 544 // 400, 544), Image.LANCZOS)
    pic.paste(t, ((960 - t.width) // 2, 0))
    save(pic, "pic0.png")

    with open(os.path.join(OUT_DIR, "livearea", "contents", "template.xml"), "w", encoding="utf-8") as f:
        f.write(TEMPLATE)
    print(f"Done. Rebuild with tools/vita/build-port.sh; CMake now packs {OUT_DIR}.")


if __name__ == "__main__":
    main()
