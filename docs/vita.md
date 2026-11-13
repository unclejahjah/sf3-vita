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

## Settings

The config file is `ux0:data/3sx/config`. It is created on the first start. Besides the general
[options](config.md) the Vita port reads:

### `vita-aspect`

How the 384x224 picture is fitted to the 960x544 screen.

- `4:3` (default): the shape of the arcade monitor, with black bars on the sides.
- `square-pixels`: every game pixel is square. Nearly fills the screen.
- `stretch`: fills the screen.

### `vita-filter`

- `sharp` (default): the game is drawn at twice its resolution and then scaled smoothly. Crisp pixels without shimmer.
- `nearest`: the game is drawn straight to the screen without smoothing. A little cheaper for the GPU.

## Troubleshooting

A log is written to `ux0:data/3sx/log.txt` on every start. Please attach it to bug reports.

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
