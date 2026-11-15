# Digimon World 3 — PC Edition (fan engine)

A native C++ engine that runs **Digimon World 3** (PlayStation, 2002) on PC by reading the data from **your own copy** of the game. No emulator: the file formats are reverse-engineered and reimplemented in C++.

> ⚠️ This repository contains **no game assets**. You must own the original disc and extract its files yourself. Digimon is a trademark of Bandai Namco; this is an unofficial, non-commercial fan project.

## Status: v0.0.2 demo (field + walls + travel + real-time battle)
- ✅ Pre-rendered field maps (all 237 stages) and character sprites (all 179 packs) decoded from your disc
- ✅ Walls/collision, exits between maps (S200 town ⇄ S400 forest), placed NPCs: **talk** and **fight/boss** (touch to fight)
- ✅ Real-time battle: partner moves freely, 3 attack buttons + guard, DV gauge → digivolve (each stage is bigger: ×1.0 / ×1.45 / ×1.9 / ×2.4)
- ✅ In-game stage editor (F2): paint walls, draw exits, place NPCs, set spawn → saved to `data/stages/*.stage`
- ✅ Battle partner drawn with the original **3D battle models** (DAT/MODEL decoded): Agumon -> Greymon -> MetalGreymon -> WarGreymon, each with its own animations, size and 3 moves; full digivolution sequence (charge, white silhouette, grow, flash, roar)
- ⏳ Enemies as 3D models too, original NPC/exit data, audio, menus — see [docs/ROADMAP.md](docs/ROADMAP.md)

## Controls
| Action | Keyboard | Gamepad |
|---|---|---|
| Move / run | WASD or arrows / hold Shift | Left stick or D-pad / A |
| Talk, continue | Space / Enter | X |
| **Battle:** attack 1 / 2 / 3 | J / K / L (or 1 / 2 / 3) | X / B / Y |
| **Battle:** guard (hold) | Space (or 4) | LB |
| **Battle:** digivolve (DV gauge full) | U (or 5) | RB |
| Browse stages (debug) | PgUp / PgDn | LB / RB |
| Stage editor | F2 (then 1-5 tools, Ctrl+S save) | — |
| HUD / quit | F1 / Esc | Back |

## Stage data (our own game design)
Each map's gameplay lives in `data/stages/<Stage>.stage` — plain text we own and can change freely:
```
spawn 300 640
exit 0 792 18 64 S400 205 862                 # rect -> target stage + arrival point
npc talk P011MINO 330 600 0 "Mina" "Line one|Line two"
npc fight P171BAGM 610 482 7 "Black Agumon" 3  # touch it to fight
npc boss P123DIGM 960 252 0 "Digmon" 8
walls                                          # 8x8 px grid: '#' wall, '.' floor
```
Maps without a file get automatic walls (background holes) — open the editor with **F2** to author them.

## Setup
1. Copy the disc's `DAT`, `PRO` and `STR` folders into a folder named `game_data` next to (or any parent folder of) the repo/exe. Or set `DW3_DATA=<folder>`, or pass `--data <folder>`.
2. Build (Windows, Visual Studio 2022/2026 with "Desktop development with C++"):
   ```
   build.bat
   ```
3. Run `build\dmw3.exe` (optional: `--stage S400`, or test a fight directly: `--battle`, `--boss P123DIGM`, `--form 1` = start as Greymon, `--dv` = full digivolve gauge).

`build\dw3check.exe` validates your data and all parsers; `--dump-stage S200 out.bmp` / `--dump-sprite P006AGUM out.bmp` / `--dump-model M005GREY out.bmp` (every animation clip as a sheet) render assets for inspection.

## Docs
- [docs/FORMATS.md](docs/FORMATS.md) — reverse-engineered file formats
- [docs/ROADMAP.md](docs/ROADMAP.md) — plan to v0.01
