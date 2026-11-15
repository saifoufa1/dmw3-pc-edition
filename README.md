# Digimon World 3 — PC Edition (fan engine)

A native C++ engine that runs **Digimon World 3** (PlayStation, 2002) on PC by reading the data from **your own copy** of the game. No emulator: the file formats are reverse-engineered and reimplemented in C++.

> ⚠️ This repository contains **no game assets**. You must own the original disc and extract its files yourself. Digimon is a trademark of Bandai Namco; this is an unofficial, non-commercial fan project.

## Status: v0.0.1 demo (field exploration)
- ✅ Pre-rendered field maps decoded from `DAT/Z_STAGE` (all 237 stages)
- ✅ Character sprites + animation scripts from `DAT/FIELD/SPRT` (all 179 packs)
- ✅ Walk / run in 8 directions with the tamer, partner Digimon follows you
- ✅ Keyboard + Xbox-style gamepad, pixel-perfect scaling, resizable window
- ⏳ Collision/depth masks, NPCs, stage exits, battles, audio — see [docs/ROADMAP.md](docs/ROADMAP.md)

## Controls
| Action | Keyboard | Gamepad |
|---|---|---|
| Move | Arrows / WASD | D-pad / left stick |
| Run | Shift | A |
| Previous / next stage | PgUp / PgDn (Q / E) | LB / RB |
| Change partner | Tab | Y |
| Toggle HUD | F1 | Back |
| Quit | Esc | — |

## Setup
1. Copy the disc's `DAT`, `PRO` and `STR` folders into a folder named `game_data` next to (or any parent folder of) the repo/exe. Or set `DW3_DATA=<folder>`, or pass `--data <folder>`.
2. Build (Windows, Visual Studio 2022/2026 with "Desktop development with C++"):
   ```
   build.bat
   ```
3. Run `build\dmw3.exe` (optional: `--stage S400`).

`build\dw3check.exe` validates your data and all parsers; `--dump-stage S200 out.bmp` / `--dump-sprite P006AGUM out.bmp` render assets for inspection.

## Docs
- [docs/FORMATS.md](docs/FORMATS.md) — reverse-engineered file formats
- [docs/ROADMAP.md](docs/ROADMAP.md) — plan to v0.01
