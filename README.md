# Digimon World 3 — PC Edition (fan engine)

A native C++ engine that runs **Digimon World 3** (PlayStation, 2002) on PC by reading the data from **your own copy** of the game. No emulator: the file formats are reverse-engineered and reimplemented in C++.

> ⚠️ This repository contains **no game assets**. You must own the original disc and extract its files yourself. Digimon is a trademark of Bandai Namco; this is an unofficial, non-commercial fan project.

## Status: v0.0.2 demo (field + walls + travel + real-time battle)
- ✅ Pre-rendered field maps (all 237 stages) and character sprites (all 179 packs) decoded from your disc
- ✅ Walls/collision, exits between maps (S200 town ⇄ S400 forest), placed NPCs: **talk** and **fight/boss** (touch to fight)
- ✅ Real-time battle: partner moves freely, 3 attack buttons + guard, DV gauge → digivolve (each stage is bigger: ×1.0 / ×1.45 / ×1.9 / ×2.4)
- ✅ In-game stage editor (F2): paint walls, draw exits, place NPCs, set spawn → saved to `data/stages/*.stage`
- ✅ Battle partner drawn with the original **3D battle models** (DAT/MODEL decoded): Agumon -> Greymon -> MetalGreymon -> WarGreymon, each with its own animations, size and 3 moves; full digivolution sequence (charge, white silhouette, grow, flash, roar)
- ✅ **Story mode, chapter 1 (Asuka City)**: login, partner download (Agumon / Guilmon / Patamon), Teddy & Kail, Tamer Genji's first battle — with the **original dialogue read from your disc** (message banks decoded), dialogue box with choices, objective in the HUD, save/load
- ⏳ Enemies as 3D models too, original exits, audio, menus — see [docs/ROADMAP.md](docs/ROADMAP.md)

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
| Save / load story | F5 / F9 (also autosaves) | — |
| HUD / quit | F1 / Esc | Back |

## Stage data (our own game design)
Each map's gameplay lives in `data/stages/<Stage>.stage` — plain text we own and can change freely:
```
spawn 300 640
exit 0 792 18 64 S400 205 862                 # rect -> target stage + arrival point
npc talk P011MINO 330 600 0 "Mina" "Line one|Line two"
npc fight P171BAGM 610 482 7 "Black Agumon" 3  # touch it to fight
npc boss P123DIGM 960 252 0 "Digmon" 8
walls original                                 # use the game's own walk map (or: 'walls' + an 8x8 px grid of '#'/'.')
```
Maps without a file use the game's walk map, spawn point and NPC positions decoded from your disc (NPCs have no lines yet) — open the editor with **F2** to author them (Ctrl+G = back to the original walls).
The game starts in Asuka City (S200, with S202 and S218).

## Story script (our own)
The chapter lives in `data/story/chapter1.story` (plain text, documented in its header). It never contains the
game's text: lines are message ids such as `say dmg200:4:3` or `talk00:0x13`, resolved at runtime from your
`DAT/COUNTRY/USA` files (`dw3check --dump-text DMG200` lists them in the console). Story NPCs, touch-to-fight
battles, exit gates and the objective line are all declared there. Progress is saved to
`%APPDATA%\DMW3PC\story.sav`; start with `--new-game` to ignore it.

## Setup
1. Copy the disc's `DAT`, `PRO` and `STR` folders into a folder named `game_data` next to (or any parent folder of) the repo/exe. Or set `DW3_DATA=<folder>`, or pass `--data <folder>`.
2. Build (Windows, Visual Studio 2022/2026 with "Desktop development with C++"):
   ```
   build.bat
   ```
3. Run `build\dmw3.exe` (optional: `--stage S400`, or test a fight directly: `--battle`, `--boss P123DIGM`, `--form 1` = start as Greymon, `--dv` = full digivolve gauge).
   Developer only: `--dev` enables the battle **size dashboard** (F3 in a fight: live +/- per form, enemy, boss, unit px; 1-4 preview forms, E enemy form, Ctrl+S saves `data/battle_sizes.cfg`); `--sizes` opens a fight with it already open. Players never see it without these flags.

Story testing: `--new-game`, `--story-flags partner_chosen,friends_split`, `--story-event download`, `--pos X Y`,
`--confirm-at FRAME`, `--confirm-every N`, `--walk lrud FROM TO`, `--no-story`.

`build\dw3check.exe` validates your data and all parsers; `--dump-stage S200 out.bmp` / `--dump-sprite P006AGUM out.bmp` / `--dump-model M005GREY out.bmp` (every animation clip as a sheet) render assets for inspection.

## Docs
- [docs/FORMATS.md](docs/FORMATS.md) — reverse-engineered file formats
- [docs/ROADMAP.md](docs/ROADMAP.md) — plan to v0.01
