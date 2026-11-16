# Live status — read this right after HANDOFF.md

This file says **what is being worked on right now**, so any assistant (Claude, DeepSeek, or a human) can pick up the work if the current one stops (for example when its token limit runs out).
Rule for every assistant: **update this file in the same commit as your work**. Say what you finished, what is half-done, and the exact next step.

Last updated: 2026-11-16 by Claude (field agent).

## Branches
| Branch | What | State |
|---|---|---|
| `main` | Game start, playable: title → New Game/Continue (3 save slots) → Digimon Online sign-up → starter pack A/B/C → Main Lobby S210 with no Digimon → partner download at Tamer Service → Central Sector S200 → Tamer Genji → tutorial battle vs Kuwagamon → story stops (free play). | Works; owner found bugs (below). |
| `gfx-round2` | Graphics/controls work (separate worktree `C:\DW3_Project\dmw3-gfx`). | In progress. Merge into `main` when finished. |

## Owner's bug list from the 2026-11-16 play test (what is being fixed now)

### Field work — on `main`
Owner's list: (1) NPCs from the disc data, Tamer Service clerk in the right place; (2) symmetric doors + a both-ways
door test; (3) the ORIGINAL door marker over the player, U/Enter goes through; (4) U/Enter = talk/confirm
everywhere, Esc (and Backspace) = back; (5) door transition + teleport port animation; (6) topmost test window
(done: a6b73d1).

**Why it was wrong:** NPC sprites used `charId & 0xFF` as the `P###` number (wrong: charId indexes a FIELDSTG table
of file ids), extra story NPCs were hand-placed on top, and every door target was hand-picked (the lobby's lower-right
walkway really leads to S220, the west corridor to the Central Sector's dragon gate). The original data has all of
it: docs/FORMATS.md "Area link table", "FIELDSTG.PRO tables", "File ids", "Field common effects".

**Done**
- Extraction (`3c12ebf`): `dw3check --dump-npcs S210`, `--extract-npcs data`, `--map-file-ids`;
  `data/file_ids.cfg` (names only, committed); `data/npcs/S###.npc` + `data/doors/S###.door` are generated from the
  player's own game data by `build.bat` (git-ignored, never pushed). dw3check checks every warp target exists.
- Keys (`c594337`): U added to `confirm` (win32_main.cpp `poll`), Backspace = back in the app.cpp menus, hint texts.
- Field (`FIELD`): NPCs only from data/npcs (story actors bind to original slots: `actor ... slot N [at X Y DIR]`),
  counter fronts depth-sorted (clerks behind the counter), doors from data/doors via the trigger zones, the
  original door marker (FIELDCOM pieces 35-38, `Field::loadDoorMark`), U/Enter uses a door (`Field::playerDoor`,
  never by walking), door sequence: door panel opens -> tile wipe -> area card (band = city, box = area) -> scene;
  ports (`data/ports.cfg`, S225 login pads, our light effect); story phase flags 6002 -> 6003 (Genji) -> 6004
  (exclusive). `dmw3 --door-test OUT.txt`: 16 PASS, 1 SKIP (S235 has no decoded trigger map), 0 failures.
  Screenshots: C:\DW3_Project\reference\pc\round2\ (owner PC only).

**Open**
- S235 (behind S200 zone 4): its TMPK has no walk/trigger map our parser understands -> entering it strands the player.
- Marker frame timing is a guess; beam/port effects are ours (originals: FIELDCOM group 1 beams, not wired).
- Stage drawing (lights, blinking screens, missing pieces) belongs to gfx-round2; see FORMATS.md "Scene objects".

**Next step:** remaining round-2 screenshots (port on the S225 pads, each lobby door both ways, the Genji battle),
in src/game/field.cpp nothing is pending; then the final report to the coordinator.

### Graphics/controls work — on `gfx-round2`
1. **Fit any screen:** resizable window, borderless fullscreen, and Fit / Stretch / Integer scale modes; wide screens show more of the scene. Saved in options.cfg.
2. **Better graphics without changing the characters:**
   - higher internal resolution and clean upscaling;
   - black gaps in field stages fixed (commit 0005e09 decodes the 3rd rect list of Z_STAGE chunks);
   - smoother 3D in battle;
   - an optional CRT filter, off by default.
3. **Mouse support:** menus, slots, packs, YES/NO, the letter grid, the pause menu and the battle panel (click-to-walk is a bonus).
4. **Password shown as `*`** on sign-up and login.

## After both are done
- Merge `gfx-round2` → `main`, then do a code review.
- The owner re-tests the game start up to the end of the first fight.
- Then: an NPC database, the original stat and digivolve tables, and then the **online mode**. AccountService and Party are already behind interfaces for a server.

## Not committed on purpose
STR movie decoding and audio (`src/formats/str*`, `src/platform/audio.*`, and their CMakeLists/dw3check changes) are only in the owner's working tree. Movies are skipped for now, so don't build on them.

## Reference material (owner's PC only, never in git)
`C:\DW3_Project\reference\` holds screenshots of the original game plus NOTES.md (the walkthrough up to the first battle, the lobby map and door targets, and the controls). The original is played in ePSXe; owner keys: arrows, U I O P = face buttons (O = confirm), V = Start, C = Select.
