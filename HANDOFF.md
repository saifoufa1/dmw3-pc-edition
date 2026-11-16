# Handoff — read this first (for any AI assistant or developer continuing the project)

DMW3 PC Edition is a from-scratch C++17 PC engine for **Digimon World 3 (PS1)**. It reads the player's **own**
extracted disc data at runtime and re-implements the game with the owner's design changes.

## Hard rules (never break)
1. **Never commit game data** or anything extracted from it: no `.BIN/.PRO/.STR`, no dumped images, audio,
   fonts or text. The repo contains only code, our own data files (`data/`), and format documentation.
   `.gitignore` blocks the common cases, but check `git status` before every commit.
2. `build\dw3check.exe` must print `OK (0 failures)` after every parser change.
3. Every decoded format goes into `docs/FORMATS.md` (offsets, confidence, open questions).
4. Owner design decisions are listed in `docs/ROADMAP.md` ("Design decisions") — keep them:
   no random encounters (NPCs placed on the map, touch to fight), real-time DW1-style battles with direct
   attack buttons, the player decides when to digivolve, digivolution lasts the whole battle,
   held items are a separate future project.
5. Developer tools (F3 size dashboard, `--dev`, test flags) must never appear for normal players.

## Build & run (Windows, Visual Studio 2022/2026 with the C++ workload)
```
build.bat                         # -> build\dmw3.exe, build\dw3check.exe (CMake + Ninja + MSVC)
build\dw3check.exe                # corpus check over the whole disc data: must end with OK (0 failures)
build\dmw3.exe                    # the game (finds game data by walking up from the exe, or --data PATH / DW3_DATA)
```
Game data layout expected: `<root>/DAT`, `<root>/PRO`, `<root>/STR` (the owner keeps it in `C:\DW3_Project\game_data`).

Launching `build\dmw3.exe` with no flags starts the **PC flow**: title → New Game / Continue → save slot → Digimon
Online sign-up → starter pack → Asuka City Main Lobby → partner download → Central Sector → Tamer Genji's tutorial
battle, then free play (see docs/ROADMAP.md "PC launch flow"). Test it headlessly (ALWAYS with `--save-dir` on a
scratch folder, and delete it afterwards, so no test save lands in `%APPDATA%\DMW3PC`):
`--flow title|menu|options|slots|newslot|signup|pack|newgame|continue`, `--slot N`, `--account NAME`,
`--select-set 1..3`, `--esc-at FRAME`, `--lobby STAGE`, `--flow-event EVENT`, `--flow-pos X Y`,
`--confirm-every N --confirm-from FRAME`, e.g.
`build\dmw3.exe --save-dir %TEMP%\dmw3t --flow newgame --account Tamer --select-set 2 --flow-event download --confirm-every 12 --confirm-from 240 --frames 1500 --screenshot lobby.bmp`.

### Battle EXP rule (owner decision 2026-11-16, `Battle::startResult`)
Each Digimon of the team has its own level, EXP and stats. After a won battle the Digimon that fought gets the
full EXP; the other team members get half of it (rounded down) from the bench. Each one levels up on its own
(`DigimonDb::addExp`, stats grow by the species' growth line in `data/digivolve_paths.cfg`); the fight report
shows the fighter's level and stat gains. A Digimon can digivolve in battle only to forms its level has unlocked
(`path` levels). EXP needed for the next level: 30 + 20 × level (placeholder curve).

Useful flags (see `src/platform/win32_main.cpp`):
- `--battle`, `--form 0..3`, `--boss SPRITE`, `--arena N`, `--cam-yaw DEG`, `--dv` — jump into a fight
- `--new-game`, `--no-story`, `--story-event NAME`, `--pos X Y`, `--stage S200` — field / story tests
- `--frames N --screenshot out.bmp` (+ `--attack 1..3`, `--confirm-at F`, `--confirm-every N`) — headless screenshot tests
- `--ui-preview hud|result|levelup|window|font|sheets` — original UI component preview
- `--window W H` (with `--screenshot` the frame is composed off-screen at W x H, so captures never depend on the
  window being visible), `--scale fit|stretch|integer`, `--no-wide`, `--lowres`, `--nearest`, `--msaa N`, `--crt`,
  `--password TEXT`, `--mouse X Y --click-at F` (mouse tests) — display / input tests (options.cfg holds the player's choice)
- `--dev` (F3 size dashboard in battle), `--sizes` (opens it) — developer only
- dw3check: `--dump-pack TTCOMMPK out.bmp` (title / NAMEENT/NE_DATA_ packs, prints piece origins), `--dump-field S210 out.bmp --overlay` (prints trigger zones), `--dump-model ID out.bmp`, `--dump-arena N out.bmp`, `--dump-ui SHEET out.bmp`, `--dump-text DMG200`
  (dump output goes to a scratch folder, never into the repo)

## Code map
| Path | What |
|---|---|
| `src/core` | file IO, data-root discovery |
| `src/formats` | pure parsers: sprites, field stage (MASK/TMPK), stage overlay (WSTAG), 3D model (DAT/MODEL), arena (MEFT1001-1055), text (USA encoding), UI sheets (*CS/*TM) |
| `src/render` | OpenGL 1.1 renderer, model renderer, 3D camera |
| `src/ui` | ORIGINAL UI drawn from game data: font, windows, battle HUD, fight report (EXP / level up) |
| `src/game` | `app.*` top-level state machine (title, menu, sign-up, starter pack, log-in, pause), `account.*` AccountService, `screen_style.h` front-end layout/colours, game modes: field, battle (3D arena, real-time), story + dialogue, common types (Party = the player's team) |
| `src/ui/pack_art.*` | whole-screen original art (title / Digimon Online packs) as one texture per piece |
| `data/starter_sets.cfg` | the original starter packs A/B/C (members, portraits, `@N` original texts) |
| `data/digivolve_paths.cfg` + `src/game/digimon_db.*` | per-species stats, growth and digivolve paths |
| `data/areas.cfg` | area names for the save slot screen |
| `data/stages/*.stage` | our stage files (walls, exits, NPCs) — editable in game with F2 |
| `data/story/chapter1.story` | our story script (references original message ids; no game text) |
| `data/battle_sizes.cfg` | battle sizes (Agumon 0.6, Greymon 1.5, MetalGreymon 2.1, WarGreymon 2.6) |
| `tools/dw3check.cpp` | corpus checker + dump tools |
| `docs/FORMATS.md` | every decoded PS1 format |
| `docs/ROADMAP.md` | milestones, owner design decisions, story status |

## State (2026-11-16)
Working: Asuka City field maps with original pieces/walk maps/NPC positions; chapter 1 story with original
dialogue; real-time 3D battles in the original arenas (free movement, orbit camera Q/E / right-drag / wheel);
Agumon line (Agumon/Greymon/MetalGreymon/WarGreymon) as original 3D models with their attack effects;
digivolution sequences (partner and enemy: Black Agumon -> BlackGreymon); original battle HUD, dialogue
window/font and post-battle EXP/BIT/level-up screen.

## Next steps (owner priorities, in order)
1. Draw the game as the original: original menus (status, items, Digimon), original positions of the HUD
   (layout code in `PRO/FIGHTSTG.PRO`, `PRO/STFGTREP.PRO` not decoded yet — current layout is reconstructed).
2. Exit/door destinations (lobby interior, Digimon Lab, shops) so the opening plays in the original rooms.
3. WSTAG charId -> sprite map (NPCs still use hand-placed sprites), original stage -> arena table.
4. Chapter 2 of the story; enemies' original parties.
5. Audio (SPU ADPCM banks + sequenced BGM).

## Workflow with the owner
The owner says "update github" to publish: commit the new work and push to
`https://github.com/saifoufa1/dmw3-pc-edition` (public). Commit identity is the owner's GitHub noreply address.
