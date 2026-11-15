# Roadmap

## v0.0.1 — Field demo ✅ (this build)
Real field maps + sprites from the player's data, walk/run, partner follow, stage browser, gamepad.

## v0.0.2 — Gameplay prototype ✅
Own stage data + editor (walls, exits, NPCs, items, spawn), S200⇄S400 travel, talk/fight/boss NPCs,
real-time battle (3 attacks + guard + DV gauge, 4 digivolve stages with growing size).
Sword/held-item prototype was removed: held equipment is a separate future project (it changes the design).

### Design decisions (owner)
- No random encounters: NPCs are placed on the map. Talk NPCs talk, fight/boss NPCs start a battle on touch.
- Battles are real-time like Digimon World 1, but with direct buttons for the partner's attacks; the player decides when to digivolve.
- Held items/equipment: separate project later (not in the battle prototype).
- Each digivolution stage is bigger on screen.

## v0.01 — First launch (proposal, to discuss)
Goal: "the first hour of DW3 plays on PC natively".

| # | Milestone | Owner agent |
|---|---|---|
| 1 | Collision + depth from `S###MASK.BIN`; layer-B foreground; animated tiles `TMPK` | ps1-reverse-engineer, graphics-programmer |
| 2 | Stage exits/transitions and spawn points (from `PRO/WSTAG###.PRO` overlays) | ps1-reverse-engineer, gameplay-systems-designer |
| 3 | Text + font (`COMMON/CMFONT*`, `FIELDMES.BIN`), dialogue boxes, NPCs | gameplay-systems-designer, graphics-programmer |
| 4 | Audio: SPU ADPCM banks + sequenced BGM | audio-engineer |
| 5 | ✅ `DAT/MODEL` decoded; partner forms are the real 3D models with their own clips + digivolution sequence. **Next:** enemies as 3D models, effect scripts | graphics-programmer, gameplay-systems-designer |
| 6 | Title screen, save/load (own format), options (scale, fullscreen, key remap) | engine-architect |
| 7 | Release pipeline: CI build, packaged zip (exe only), changelog | qa-release-engineer |

Cross-cutting: game-state stack (Title/Field/Battle/Menu), SDL2 port for Linux/Steam Deck after v0.01.

## Placeholders currently in the code
- Enemies are still 2D field sprites in battle (partner is 3D).
- Battle numbers (HP, damage, MP, cooldowns) are first-pass tuning in `src/game/battle.cpp`.
- Only S200 (hand-traced) and S400 (colour-classified) have authored walls; other maps use automatic walls.
