# Vita picture filters

These are the post-processing filters of [Emu4VitaPlus](https://github.com/noword/Emu4VitaPlus) by noword
(taken from commit `cae97f50ef782a46683695ac9eed42e120a6f7fc`). They are RetroArch Cg shaders that noword adapted
to the Vita and compiled to GXM programs.

- `gxp/` holds the compiled programs exactly as Emu4VitaPlus ships them (`share/pkg/assets/shaders/`). The build
  packs them into the .vpk as `shaders/`, and the game loads the chosen one at runtime.
- `cg/` holds their source (`script/shaders/` in Emu4VitaPlus).

VitaSDK has no Cg compiler, so the programs can't be rebuilt here. Emu4VitaPlus compiles each file with Sony's
`psp2cgc`:

```
psp2cgc -profile sce_vp_psp2 -fastprecision -DVERTEX   name.cg -o name_v.gxp
psp2cgc -profile sce_fp_psp2 -fastprecision -DFRAGMENT name.cg -o name_f.gxp
```

The filters are separate files that the game reads at runtime; they are not linked into the game.

## Licenses

| Filter (menu name) | File | Author | License |
| --- | --- | --- | --- |
| SHARP BILINEAR | `sharp-bilinear` | Themaister | Public domain |
| SHARP SIMPLE | `sharp-bilinear-simple` | rsn8887, after Themaister | Public domain |
| LCD3X | `lcd3x` | Gigaherz | Public domain |
| ZFAST LCD | `zfast_lcd` | Greg Hogan (SoltanGris42) | GPL v2 or later |
| ZFAST CRT | `zfast_crt` | Greg Hogan (SoltanGris42) | GPL v2 or later |

All of the files were adapted for the Vita by noword as part of Emu4VitaPlus, which is licensed under the GNU GPL
version 2 (`LICENSE-GPL-2.0.txt`). The GPL-licensed and public-domain filters can be used with 3SX's AGPL-3.0.

Left out:

- `scale2xplus`, `crt-gdv-mini`, `crt-easymode`, `crt-lottes-fast` and `crt-lottes` were tried and dropped frames
  on a Vita (they take 100-200 GPU instructions per pixel). The CRT SCANLINES, CRT SCAN SOFT, CRT SCAN HARD, CRT APERTURE,
  CRT SHADOW MASK and CRT SLOT MASK looks of the FILTER row replace them. They are not shaders: `src/platform/video/vita/vita_crt.c`
  works out the scanline and phosphor mask pattern on the CPU when a setting changes and multiplies it onto the
  picture with vita2d's plain texture shader, which costs about as much as drawing the picture once more.
- `advanced-aa`, `gba-color`, `gbc-color`, `gba-color+lcd3x` and the `gb-palette-*` handheld palettes were
  removed at Jah's request (not needed for this game).
- Emu4VitaPlus also has `crt-mattias`, a port of Mattias's Shadertoy "CRT Effect"
  (shadertoy.com/view/lsB3DV). Shadertoy shaders without a license of their own are under CC BY-NC-SA 3.0, which
  forbids commercial use and isn't compatible with the GPL, so it isn't included.

## Cost

The filter pass shades every pixel of the picture: 725x544 for NORMAL, 932x544 for WIDE and 960x544 for FULL. The
game waits for the GPU each frame, so the pass adds straight to the frame time. These are estimates from the number
of fragment instructions in each program (read from its GXP header), assuming the SGX543MP4+ at 222 MHz completes
16 instructions per clock. Only a test on a Vita gives real numbers: the frame-time log (`vita-perf-log`) notes
which filter was on when a frame was slow.

| Filter | Instructions | NORMAL (ms) | FULL (ms) | 60 fps |
| --- | ---: | ---: | ---: | --- |
| SHARP BILINEAR | 15 | 1.7 | 2.2 | expected to hold |
| SHARP SIMPLE | 16 | 1.8 | 2.4 | expected to hold |
| ZFAST LCD | 18 | 2.0 | 2.6 | expected to hold |
| LCD3X | 31 | 3.4 | 4.6 | expected to hold |
| ZFAST CRT | 31 | 3.4 | 4.6 | expected to hold |
