# Live status — read this right after HANDOFF.md

This file says **what is being worked on right now**, so any assistant (Claude, DeepSeek, or a human) can pick up the work if the current one stops (for example when its token limit runs out).
Rule for every assistant: **update this file in the same commit as your work**. Say what you finished, what is half-done, and the exact next step.

Last updated: 2026-11-16 by Claude (coordinator).

## Branches
| Branch | What | State |
|---|---|---|
| `main` | Game start, playable: title → New Game/Continue (3 save slots) → Digimon Online sign-up → starter pack A/B/C → Main Lobby S210 with no Digimon → partner download at Tamer Service → Central Sector S200 → Tamer Genji → tutorial battle vs Kuwagamon → story stops (free play). | Works; owner found bugs (below). |
| `gfx-round2` | Graphics/controls work (separate worktree `C:\DW3_Project\dmw3-gfx`). | In progress. Merge into `main` when finished. |

## Owner's bug list from the 2026-11-16 play test (what is being fixed now)

### Field work — on `main`
1. **NPCs are wrong.**
   - Problem: positions and sprites don't match the original, and the Tamer Service clerk is misplaced, so the owner could not download their partners.
   - Fix: extract the NPCs from the disc's WSTAG/stage overlay actor tables (position, facing, sprite id, talk script id, show conditions), dump them with `dw3check --dump-npcs S210`, and write them to `data/npcs/S###.npc`. Place NPCs **only** from that data. A database comes later (owner: "first extract them").
2. **Doors lead to wrong places.**
   - Fix: every door must be symmetric (A→B arrives at B's matching door, and B→A comes back). Add a test that walks every door both ways.
   - Rooms: S210 Main Lobby ↔ S225 Login Room (zone 0xA2), S230 Arena Front Desk (0xA3), S200 Central Sector (0xE1 ↔ 0xA5). 0xA4/0x65 are unknown.
3. **Door interaction like the original:** at a door, a red arrow bubble appears over the player, and **U or Enter** goes through.
4. **Keys:** U / Enter = talk and confirm everywhere (Space still works too). Esc = back.
5. **Animations:** a door-enter transition (area-card wipe), and teleport ports that open when stepped on or when Enter is pressed.
6. Test runs open the game window topmost (done: commit a6b73d1).

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
