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
| 1 | ✅ Walk map + scene pieces (`MASK`/`TMPK`) and object/actor placement (`WSTAG` overlays) decoded. **Next:** depth sorting (draw pieces over actors by `depthY`), 4bpp glow palette, remaining black holes | ps1-reverse-engineer, graphics-programmer |
| 2 | Spawn points ✅ decoded; exit *zones* ✅ decoded (trigger map). **Next:** exit/door destinations (in overlay code or main exe) | ps1-reverse-engineer, gameplay-systems-designer |
| 3 | ✅ Story text decoded (`USDMG###`/`USTALK##`, WSTAG talk tables + flag conditions); story/event system, dialogue box, opening chapter (`data/story/chapter1.story`), save/load. **Next:** original font/window (src/ui), event-bank playback code, charId→sprite map, chapter 2 | gameplay-systems-designer, ps1-reverse-engineer |
| 4 | Audio: SPU ADPCM banks + sequenced BGM | audio-engineer |
| 5 | ✅ `DAT/MODEL` decoded; partner forms are the real 3D models with their own clips + digivolution sequence. **Next:** enemies as 3D models, effect scripts | graphics-programmer, gameplay-systems-designer |
| 6 | Title screen, save/load (own format), options (scale, fullscreen, key remap) | engine-architect |
| 7 | Release pipeline: CI build, packaged zip (exe only), changelog | qa-release-engineer |

Cross-cutting: game-state stack (Title/Field/Battle/Menu), SDL2 port for Linux/Steam Deck after v0.01.

## Story (chapter 1, `data/story/chapter1.story`)
Login (USDMG200 block 0) → Tamer Service: download partner (block 2 + choice of Agumon / Guilmon / Patamon,
blocks 3–5) → Teddy & Kail scene → Tamer Genji, first story battle on touch (blocks 11/12) → wild Black Agumon
at the west bridge (our own lines) → S400: chapter complete. Town NPCs speak their ORIGINAL lines (WSTAG talk
tables, story phase flag 6004). Save: `%APPDATA%\DMW3PC\story.sav` (F5 save, F9 load; autosaves at story beats).
- PLACEHOLDER: the opening plays on the S200 plaza (the original lobby interior and Digimon Lab are not linked yet).
- PLACEHOLDER: Genji fights with a Blue Agumon (his real party is not decoded); Black Agumon's lines are ours.
- PLACEHOLDER: non-Agumon partners use their field sprite in battle (battle supports the Agumon line best).
- Original NPC sprites: the WSTAG charId → sprite map is unknown, so stage-file sprites stay as placed
  (names in the dialogue come from the original text, e.g. "Mika", "Masato").

## Placeholders currently in the code
- Enemies are still 2D field sprites in battle (partner is 3D).
- Battle numbers (HP, damage, MP, cooldowns) are first-pass tuning in `src/game/battle.cpp`.
- Asuka City = S200 (start, centre) + S202 (south park) + S218 (north-east gate); night versions S201/S203/S219.
  All three use `walls original` (decoded walk map). NPC positions/sprites come from the decoded event actors;
  NPCs on an original actor spot speak the original lines (WSTAG talk table); our lines are a fallback.
  Exit rectangles sit on decoded trigger zones but the targets are hand-linked.
  The story fights (Genji, Black Agumon) are story actors in data/story/chapter1.story.
- Stages without a .stage file use the decoded walk map (or automatic walls if a stage has none), the decoded spawn and the decoded actors as silent talk NPCs.
- Door triggers in Asuka City (shops, houses) are not linked yet: their interior stages are unknown.
