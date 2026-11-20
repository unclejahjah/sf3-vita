# PS Vita

3SX runs natively on a PS Vita or PS TV with HENkaku/Ensō. The port draws with the Vita's GPU through
[vita2d](https://github.com/xerpi/libvita2d) and is locked to the screen's 60 Hz refresh.

> [!IMPORTANT]
> The game data is not included. You need `SF33RD.AFS` from your own, legally obtained copy of
> *Street Fighter III: 3rd Strike* or *Street Fighter Anniversary Collection* for PlayStation 2
> (it is in the `THIRD` folder of the disc).

## Install

1. Install `3sx.vpk` with VitaShell.
2. Copy `SF33RD.AFS` to `ux0:data/3sx/resources/SF33RD.AFS` (`ux0:data/3sx/SF33RD.AFS` works too).
3. Start **3SX** from the home screen.

If the file is missing, the game shows a message that says where it looked.

## Controls

| Vita    | Game         |
|---------|--------------|
| D-pad / left stick | Move |
| □       | Light punch  |
| △       | Medium punch |
| R       | Heavy punch  |
| ×       | Light kick   |
| ○       | Medium kick  |
| L       | Heavy kick   |
| START   | Start        |

The Vita has no L2/R2, so the default layout puts heavy kick on L. Buttons can be changed in the game's
button config menu.

On a PS TV the first controller is player 1 and the next paired controller is player 2, which unlocks Versus.
A controller can be paired while the game is running.

## Screen Adjust

The game's own Screen Adjust page (Option menu) works on the Vita: the position and size settings move and shrink
the picture. The page has one more row on the Vita, SCREEN SHAPE, which switches the picture between NORMAL (4:3),
WIDE and FULL (the whole screen). The choice is saved as `vita-aspect` in the config file.

Two more rows pick a picture filter:

- FILTER: OFF, one of the post-processing filters of [Emu4VitaPlus](https://github.com/noword/Emu4VitaPlus)
  (sharp bilinear, anti-aliasing, LCD grids, zfast CRT, handheld colour palettes), or one of four light CRT looks
  made for this port: CRT SCANLINES, CRT APERTURE (aperture grille), CRT SHADOW MASK and CRT SLOT MASK.
  Saved as `vita-shader`.
- INTENSITY: how strongly the filter shows, from 0% to 100% in steps of 10. Below 100% the filtered picture is
  mixed with the plain one. Saved as `vita-shader-intensity`.

Emu4VitaPlus's heavy CRT shaders (crt-lottes, crt-easymode, crt-gdv-mini) and Scale2x+ dropped frames on the
Vita and were taken out. The four CRT looks replace them: the scanline and mask pattern is worked out once when a
setting changes and then multiplied onto the picture, which costs about one extra draw of the picture.
[tools/vita/shaders/README.md](../tools/vita/shaders/README.md) has the source and the license of every filter.

To make room for the extra rows, the page is written in the game's small menu font.

## Settings

The config file is `ux0:data/3sx/config`. It is created on the first start. Besides the general
[options](config.md) the Vita port reads:

### `vita-aspect`

How the 384x224 picture is fitted to the 960x544 screen. It can also be changed in the game, see Screen Adjust.

- `4:3` (default): the shape of the arcade monitor, with black bars on the sides.
- `square-pixels`: every game pixel is square. Nearly fills the screen.
- `stretch`: fills the screen.

### `vita-filter`

- `sharp` (default): the game is drawn at twice its resolution and then scaled smoothly. Crisp pixels without shimmer.
- `nearest`: the game is drawn straight to the screen without smoothing. A little cheaper for the GPU.

### `vita-shader`

The picture filter, also chosen on the Screen Adjust page: `off` (default) or the file name of a filter in
`tools/vita/shaders/gxp/` without `_v.gxp` / `_f.gxp` (for example `sharp-bilinear-simple`), or one of the CRT
looks `crt-scanlines`, `crt-aperture`, `crt-shadow-mask` and `crt-slot-mask`.
Filters work with both `vita-filter` settings.

### `vita-shader-intensity`

How strongly the filter shows, `0` to `100` (default `100`).

### `vita-perf-log`

`true` (default) records every frame that took too long to `ux0:data/3sx/perf.txt`, with the time each part of
the frame took and a summary every ten seconds. Set it to `false` to turn the recording off.

## Troubleshooting

A log is written to `ux0:data/3sx/log.txt` on every start. Please attach it to bug reports, together with
`perf.txt` if the report is about speed.

## Known differences from the desktop version

- No netplay and no arcade balance option.
- Sprites are drawn back to front instead of with a depth buffer. In rare frames the shade of a translucent
  shadow or HUD part differs slightly from the desktop version.

## Build

The port builds with [VitaSDK](https://vitasdk.org). The easiest way is the `vitasdk/vitasdk` Docker image:

```sh
docker run --rm -v "$PWD":/work -w /work vitasdk/vitasdk bash tools/vita/build-minizip.sh
docker run --rm -v "$PWD":/work -w /work vitasdk/vitasdk bash tools/vita/build-port.sh
```

The result is `build/vita/3sx.vpk`. SDL3 and vita2d come with VitaSDK. Only minizip-ng is built from source,
into `third_party/vita`. If the container has no network access, clone
[minizip-ng](https://github.com/zlib-ng/minizip-ng) yourself and pass its path in `MINIZIP_NG_SRC`.

### LiveArea art

`tools/vita/sce_sys` holds the bubble (`icon0.png`), the LiveArea background and floating start logo, and the splash
(`pic0.png`). They are the game's title logo on black, cut from the title screen. To make them again from your copy of
the game with the desktop build:

```sh
tools/vita/livearea/capture.sh            # plays the attract mode under Xvfb, saves frames to build/livearea-frames
python3 -I tools/vita/livearea/compose.py # writes 8-bit indexed PNGs to tools/vita/sce_sys_local
```

`capture.sh` needs the desktop build (installed with `cmake --install build/application --prefix build/application`),
Xvfb, ffmpeg, and `SF33RD.AFS` in the desktop data folder. `compose.py` needs Pillow. When
`tools/vita/sce_sys_local` exists (it is in `.gitignore`), CMake packs it into the .vpk instead of `tools/vita/sce_sys`,
so you can try new art without touching the tracked files. Copy the files over to `tools/vita/sce_sys` to keep them.
