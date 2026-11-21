# Changelog

Release notes for the PS Vita port. The release workflow copies the section for the released version into the GitHub release.

## 1.02

- **New: picture filters.** Screen Adjust has FILTER and INTENSITY rows. Choose OFF, one of five shaders (sharp bilinear, sharp bilinear simple, LCD3x, zfast LCD, zfast CRT) or one of six light CRT looks (scanlines, soft, hard, aperture grille, shadow mask, slot mask). INTENSITY sets how strong the effect is.
- Includes everything from 1.01: game options are kept after the app is closed, and Screen Adjust position and size are restored at start.

## 1.01

- **Fixed:** game options (difficulty, BONUS STAGE and the rest) were forgotten when the app was closed. They are now saved to `ux0:data/3sx/saves/settings` and loaded at start.
- **Fixed:** the Screen Adjust position and size are applied at start. They were saved but only used while the Screen Adjust page was open.
- Saves and loads are written to `ux0:data/3sx/log.txt`, so a failure can be traced.

## 1.0

First release: Street Fighter III: 3rd Strike running natively on the PS Vita and PS TV.

- Full game at 60 fps with sound and music.
- Vita button layout (heavy kick on L, since the Vita has no R2) and a native pad driver for PS TV.
- SCREEN SHAPE row in Screen Adjust (NORMAL / WIDE / FULL), plus the game's own position and size settings.
- Frame times are logged to `ux0:data/3sx/perf.txt` for troubleshooting.

