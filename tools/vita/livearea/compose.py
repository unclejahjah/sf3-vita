#!/usr/bin/env python3
"""Builds the Vita LiveArea art from frames of the game's own attract mode.

Reads the frames that capture.sh recorded (title screen and the player select screen with Alex),
cuts the logo and artwork out of them and writes 8-bit indexed PNGs, the format the Vita needs, to
tools/vita/sce_sys_local/. CMake packs that folder into the .vpk instead of tools/vita/sce_sys/ when
it exists.

The output is copyrighted game art: sce_sys_local/ is in .gitignore and must never be committed.

Usage: python3 -I tools/vita/livearea/compose.py [frames_dir] [out_dir]
Needs Pillow.
"""

import glob
import os
import shutil
import sys

from PIL import Image, ImageEnhance, ImageFilter

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
FRAMES_DIR = sys.argv[1] if len(sys.argv) > 1 else os.path.join(REPO_ROOT, "build", "livearea-frames")
OUT_DIR = sys.argv[2] if len(sys.argv) > 2 else os.path.join(REPO_ROOT, "tools", "vita", "sce_sys_local")
TRACKED_SCE_SYS = os.path.join(REPO_ROOT, "tools", "vita", "sce_sys")


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


def is_select(im):
    # Player select: orange gradient corners and the white line under the "1P <name>" banner
    return (near(im.getpixel((5, 5)), (58, 16, 0)) and near(im.getpixel((630, 5)), (255, 197, 8))
            and near(im.getpixel((600, 100)), (255, 255, 255), 8))


def is_alex(im):
    # Alex is the first character shown; his shoulder fills the bottom left corner
    return is_select(im) and near(im.getpixel((5, 470)), (197, 132, 107)) and near(im.getpixel((30, 40)), (66, 16, 0))


def pick(frames, test, what):
    hits = [f for f in frames if test(f[1])]
    if not hits:
        sys.exit(f"No {what} frame found in {FRAMES_DIR}. Run capture.sh again (or set LIVEAREA_SECONDS higher).")
    # The last frame of the first run of matches: fully faded in, banner animation finished
    first = frames.index(hits[0])
    chosen = hits[0]
    for f in frames[first:]:
        if not test(f[1]):
            break
        chosen = f
    print(f"{what}: {os.path.basename(chosen[0])}")
    return chosen[1]


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
    alex = pick(frames, is_alex, "player select (Alex)")

    # The logo with "Fight for the Future", without the copyright lines
    logo = title.crop((10, 20, 630, 385))
    logo_mask = logo.convert("L").point(lambda v: 255 if v > 18 else v * 14)

    # icon0.png 128x128: Alex's face from the player select screen
    save(alex.crop((115, 72, 325, 282)).resize((128, 128), Image.LANCZOS), "icon0.png")

    # bg.png 840x500: the player select screen, blurred and dimmed, under the title logo
    bg = alex.resize((840, 630), Image.LANCZOS).crop((0, 65, 840, 565)).filter(ImageFilter.GaussianBlur(6))
    bg = ImageEnhance.Brightness(bg).enhance(0.45)
    lw = 470
    lh = logo.height * lw // logo.width
    bg.paste(logo.resize((lw, lh), Image.LANCZOS), ((840 - lw) // 2, 8), logo_mask.resize((lw, lh), Image.LANCZOS))
    save(bg, "livearea/contents/bg.png")

    # startup.png 280x158: the gate, Alex with his "1P ALEX" banner
    # (cut below the blinking "PRESS 1P START" line at the top of the screen)
    save(alex.crop((10, 16, 394, 232)).resize((280, 158), Image.LANCZOS), "livearea/contents/startup.png")

    # pic0.png 960x544: shown while the bubble opens; the title screen without the copyright lines
    pic = Image.new("RGB", (960, 544))
    t = title.crop((0, 0, 640, 400))
    t = t.resize((640 * 544 // 400, 544), Image.LANCZOS)
    pic.paste(t, ((960 - t.width) // 2, 0))
    save(pic, "pic0.png")

    shutil.copy(os.path.join(TRACKED_SCE_SYS, "livearea", "contents", "template.xml"),
                os.path.join(OUT_DIR, "livearea", "contents", "template.xml"))
    print(f"Done. Rebuild with tools/vita/build-port.sh; CMake now packs {OUT_DIR}.")


if __name__ == "__main__":
    main()
