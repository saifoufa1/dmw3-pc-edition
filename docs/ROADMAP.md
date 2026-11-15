# Roadmap

## v0.0.1 — Field demo ✅ (this build)
Real field maps + sprites from the player's data, walk/run, partner follow, stage browser, gamepad.

## v0.01 — First launch (proposal, to discuss)
Goal: "the first hour of DW3 plays on PC natively".

| # | Milestone | Owner agent |
|---|---|---|
| 1 | Collision + depth from `S###MASK.BIN`; layer-B foreground; animated tiles `TMPK` | ps1-reverse-engineer, graphics-programmer |
| 2 | Stage exits/transitions and spawn points (from `PRO/WSTAG###.PRO` overlays) | ps1-reverse-engineer, gameplay-systems-designer |
| 3 | Text + font (`COMMON/CMFONT*`, `FIELDMES.BIN`), dialogue boxes, NPCs | gameplay-systems-designer, graphics-programmer |
| 4 | Audio: SPU ADPCM banks + sequenced BGM | audio-engineer |
| 5 | Battle prototype: models from `DAT/MODEL`, 3v3 turn system, techs | graphics-programmer, gameplay-systems-designer |
| 6 | Title screen, save/load (own format), options (scale, fullscreen, key remap) | engine-architect |
| 7 | Release pipeline: CI build, packaged zip (exe only), changelog | qa-release-engineer |

Cross-cutting: game-state stack (Title/Field/Battle/Menu), SDL2 port for Linux/Steam Deck after v0.01.

## Placeholders currently in the code
- Collision = "stand on non-transparent background pixel" (`Field::walkable`).
- Spawn = nearest walkable spot to the map centre.
- Partner roster = fixed list of rookie sprite ids.
