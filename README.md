# 3SX for PS Vita

A native PS Vita and PS TV port of *Street Fighter III: 3rd Strike – Fight for the Future*, built on
[3SX](https://github.com/crowded-street/3sx), the C decompilation of the PlayStation 2 version by the crowded-street team.

> [!NOTE]
> This is neither emulation nor a recompiler. The game's own logic, decompiled to C, runs directly on the Vita's CPU and
> draws with the Vita's GPU. It runs at a locked 60 fps, in step with the screen.

> [!IMPORTANT]
> No game data is included. You need `SF33RD.AFS` from your own, legally obtained copy of *Street Fighter III: 3rd Strike*
> or *Street Fighter Anniversary Collection* for PlayStation 2. This project does not condone piracy, and nobody here
> will send you game files.

## Features

- Native 60 fps on a Vita or PS TV, with full sound and music
- The game's own Screen Adjust page, plus a **SCREEN SHAPE** row on the Vita: 4:3, wide or full screen
- A sharp 2x picture filter (default) or plain nearest-pixel scaling
- Two players on a PS TV with two paired controllers (unlocks Versus; not yet tested on a real PS TV)
- Button layout you can change in the game's Button Config menu
- A LiveArea page and bubble (see [LiveArea art](#livearea-art-optional))

## What you need

| | |
|---|---|
| **Console** | A PS Vita (any model) or PS TV running HENkaku/Ensō or another HENkaku-based homebrew enabler |
| **Homebrew tools** | [VitaShell](https://github.com/TheOfficialFloW/VitaShell) to install the .vpk and copy files |
| **Game data** | `SF33RD.AFS` (about 642 MB) from your own PS2 disc, see below |
| **Free space** | About 650 MB on `ux0:` |

If your Vita does not run homebrew yet, follow the guide at [vita.hacks.guide](https://vita.hacks.guide). It covers every
firmware version.

## Step 1: get `SF33RD.AFS` from your disc

`SF33RD.AFS` holds all of the game's graphics, sound and music. It is in the `THIRD` folder of these PS2 discs:

- *Street Fighter III: 3rd Strike – Fight for the Future* (Japan, SLPM-65621, and other regions of the standalone release)
- *Street Fighter Anniversary Collection* (North America and Europe)

1. **Make an image of your disc.** On Windows, put the disc in a DVD drive and use [ImgBurn](https://www.imgburn.com)
   (*Create image file from disc*) to save it as an `.iso`. On Linux or macOS,
   `dd if=/dev/<your-dvd-drive> of=3s.iso bs=2048` does the same.
2. **Open the .iso.** On Windows, open it with [7-Zip](https://www.7-zip.org) (right-click › 7-Zip › Open archive) or
   double-click it to mount it as a drive. On macOS, double-click it. On Linux, `sudo mount -o loop 3s.iso /mnt`.
3. **Copy out `THIRD/SF33RD.AFS`.** The file is 642,492,416 bytes.
4. **(Optional) Check it:**
   - Windows: `certutil -hashfile SF33RD.AFS SHA256`
   - Linux/macOS: `sha256sum SF33RD.AFS` (macOS: `shasum -a 256 SF33RD.AFS`)

   A good copy gives `f9fa50f3a124ec9fa9465aa9c8546c2d867887eb39f711a070762a0324ba5604`.
   Any other result means a bad rip or a different file; make the image again.

## Step 2: install the game

1. Get `3sx.vpk` from the [Releases](https://github.com/unclejahjah/sf3-vita/releases) page once one is published, or
   build it yourself (see [Building](#building)).
2. Copy `3sx.vpk` to the Vita, for example to `ux0:downloads/`:
   - **FTP:** in VitaShell press SELECT to start the FTP server, then connect to the address it shows (port 1337) with an
     FTP client such as [FileZilla](https://filezilla-project.org) or WinSCP.
   - **USB:** in VitaShell press START › *USB connection*, connect the cable, and the memory card shows up as a drive.
3. In VitaShell, open the folder, select `3sx.vpk` and press × to install it.
   A **3SX** bubble appears on the home screen.

## Step 3: copy the game data

1. In VitaShell, create the folder `ux0:data/3sx/resources/` (press △ › *New folder*).
2. Copy `SF33RD.AFS` into it over FTP or USB, so that the file is at:

   ```
   ux0:data/3sx/resources/SF33RD.AFS
   ```

   `ux0:data/3sx/SF33RD.AFS` works too.
3. Start **3SX** from the home screen.

If the game cannot find the file, it shows a message with the exact path it looked in.

## Controls

| Vita | Game |
|---|---|
| D-pad / left stick | Move |
| □ | Light punch |
| △ | Medium punch |
| R | Heavy punch |
| × | Light kick |
| ○ | Medium kick |
| L | Heavy kick |
| START | Start |

The Vita has no L2/R2, so heavy kick is on L by default. Every button can be changed in the game's Button Config menu.
On a PS TV the first controller is player 1 and the next paired controller is player 2. Controllers can be paired while
the game is running.

## Settings and files

Everything the game writes goes to `ux0:data/3sx/`:

| File | What it is |
|---|---|
| `config` | Settings, created on the first start |
| `log.txt` | A log of the last start; attach it to bug reports |
| `perf.txt` | Frames that took too long, for speed reports (turn off with `vita-perf-log = false`) |

The Vita settings (`vita-aspect`, `vita-filter`, `vita-perf-log`) and the Screen Adjust page are explained in
[docs/vita.md](docs/vita.md). The general options are in [docs/config.md](docs/config.md).

## Troubleshooting

- **"SF33RD.AFS not found"**: check the path and the file name (capital letters) in Step 3, and that the file is the full
  642,492,416 bytes.
- **The game crashes while loading, or graphics are missing**: the AFS file may be damaged or from a different
  disc. Check its SHA256 (Step 1).
- **The bubble still shows old artwork after updating**: delete the 3SX bubble and install the .vpk again; the Vita
  keeps the old LiveArea otherwise. Your settings in `ux0:data/3sx/` are kept.
- **Anything else**: open an issue with `log.txt` (and `perf.txt` for speed problems).

## Building

The port builds with [VitaSDK](https://vitasdk.org). The easiest way is the `vitasdk/vitasdk` Docker image:

```sh
docker run --rm -v "$PWD":/work -w /work vitasdk/vitasdk bash tools/vita/build-minizip.sh
docker run --rm -v "$PWD":/work -w /work vitasdk/vitasdk bash tools/vita/build-port.sh
```

The result is `build/vita/3sx.vpk`. More detail, including builds without network access, is in
[docs/vita.md](docs/vita.md). The desktop build (Windows, macOS, Linux) is described in [docs/building.md](docs/building.md).

### LiveArea art (optional)

The repository only has plain placeholder art for the bubble and LiveArea, because the game's logo belongs to Capcom.
To use the real title logo, cut it from your own `SF33RD.AFS` with the desktop build:

```sh
tools/vita/livearea/capture.sh            # plays the attract mode off screen and saves frames
python3 -I tools/vita/livearea/compose.py # writes the bubble, LiveArea and splash images
```

The images go to `tools/vita/sce_sys_local/`, which git ignores, and the next Vita build packs them automatically.
Do not share a .vpk built this way publicly.

## Credits

**3SX** – the decompilation and the game code this port runs:
- The [crowded-street](https://github.com/crowded-street) team, led by Artem Pstygo ([@apstygo](https://github.com/apstygo)),
  and every [3SX contributor](https://github.com/crowded-street/3sx/graphs/contributors)
- [3s-decomp](https://github.com/crowded-street/3s-decomp), the matching decompilation of the PS2 version that 3SX grew from

**PS Vita port** – [unclejahjah](https://github.com/unclejahjah)

**Vita homebrew tools the port depends on:**
- [VitaSDK](https://vitasdk.org) – the toolchain and libraries for Vita homebrew
- [vita2d](https://github.com/xerpi/libvita2d) by xerpi – the GPU drawing library
- [SDL3](https://github.com/libsdl-org/SDL) and its PS Vita backend – sound, timing and files
- [HENkaku and Ensō](https://henkaku.xyz) by Team Molecule – homebrew on the Vita
- [VitaShell](https://github.com/TheOfficialFloW/VitaShell) by TheFloW – installing and copying files

**Libraries used by 3SX:**
- [GekkoNet](https://github.com/HeatXD/GekkoNet) for rollback netcode (desktop only)
- [SDL_net](https://github.com/libsdl-org/SDL_net) for P2P connections (desktop only)
- [libcdio / libiso9660](https://github.com/libcdio/libcdio) for reading .iso files (desktop only)
- [zlib](https://zlib.net) for decompression
- [minizip-ng](https://github.com/zlib-ng/minizip-ng) for unzipping
- [argparse](https://github.com/cofyc/argparse) for command line arguments
- [TF-PSA-Crypto](https://github.com/Mbed-TLS/TF-PSA-Crypto) for checksums
- [stb](https://github.com/nothings/stb) for data structures
- [Dear ImGui](https://github.com/ocornut/imgui) for the debug UI (desktop only)

Their licences are listed in [THIRD_PARTY_NOTICES.txt](THIRD_PARTY_NOTICES.txt).

**Street Fighter III: 3rd Strike** © CAPCOM CO., LTD. 1999, 2004. Street Fighter and Capcom are trademarks of
CAPCOM CO., LTD. This project is not affiliated with or endorsed by Capcom.

## Community

Questions about the game code and 3SX in general are welcome on the crowded-street Discord:

[![Join the Discord](https://dcbadge.limes.pink/api/server/https://discord.gg/wqs6BqYr8C)](https://discord.gg/wqs6BqYr8C)

Problems with the Vita port belong in this repository's [issues](https://github.com/unclejahjah/sf3-vita/issues).

## Licence

3SX and this port are licensed under the [GNU Affero General Public License v3.0](LICENSE). If you share a build,
share its source code too.
