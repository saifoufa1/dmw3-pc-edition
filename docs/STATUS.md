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
Worktree `C:\DW3_Project\dmw3-gfx`. Owner of this section: the graphics agent (the field agent does not edit it).

**Done**
- `0005e09` Black gaps in field stages: Z_STAGE chunks have a 3rd rect list (low floor); now decoded (`src/formats/stage_pack.cpp`).
- `d38878b` Render pipeline in `src/render/gl_renderer.*`: scene drawn into an FBO at virtual size x integer scale R,
  composed onto the window with Fit / Stretch / Integer, sharp-bilinear or nearest, optional MSAA (battle 3D) and CRT
  filter; HUD text queued and drawn in real window pixels by `gfx::endFrame()`. Wide windows: field + battle show more
  horizontally (`gfx::viewLeft()/viewRight()`, x 0..320 stays the centred 4:3 area). DPI aware window, F11 fullscreen.
- Options screen (title menu and in-game Esc menu): Display, Scale, Widescreen, Resolution, Upscale, Anti-aliasing, CRT;
  saved in options.cfg (`App::loadOptions/saveOptions/changeOption`).
- Mouse: title, main menu plates, slots + overwrite YES/NO, sign-up fields / letter grid / Show-Hide, starter pack tabs +
  register YES/NO, pause + options menus, battle command window (`ui::BattleHud::commandAt`), fight report, story
  dialogue (click = confirm), click-to-walk in the field (`Game::mouseInField`, game.cpp only).
- Password on sign-up drawn as one pixel `*` per character (the original font has no `*`); Show/Hide button + F4.
- Screenshots no longer depend on the window: `--screenshot` composes off-screen at `--window W H`
  (`gfx::setOffscreenPresent`, `gfx::readFrame`). Test flags: `--window W H --scale fit|stretch|integer --no-wide
  --lowres --nearest --msaa N --crt --password TEXT --mouse X Y --click-at FRAME`.

- Screenshots checked at 1280x720, 1920x1080, 2560x1080, 1366x768 (title, sign-up, pack, lobby, battle, plus options,
  pause, CRT, integer, stretch, hover, click-to-walk, battle click) in `C:\DW3_Project\reference\pc\round2_gfx\`
  (owner's PC only). Black gaps before/after: `gaps_before_S210_960x720.png` / `gaps_after_S210_960x720.png`.

**Half-done / next step**
- HUD text in window pixels (Consolas 15 px: objective line, key hints, NPC labels) does not grow with the window;
  the original-UI font does. Next: font tiers in `gfx::initText` + a `gfx::charW()` used by story.cpp / dialogue.cpp
  instead of `gfx::kCharW` (their box layouts assume an 8 px advance).
- Pause menu: the objective box (a HUD rect) stays visible while its text is hidden (`App::render` clears only text).
- Not pushed: pushing gfx-round2 needs the owner's permission in this session.
- Open: the original lobby floor is vivid blue (a blue gradient overlay, probably a B-F semi-transparent effect drawn
  by FIELDSTG/WSTAG code) — not decoded; ours shows the decoded lavender floor. The green window piece on S210 uses a
  different CLUT row than the original teal (MASK piece CLUT choice, `FieldStage::renderPart`).

**Files likely to conflict with main (field agent):** `src/platform/win32_main.cpp` (topmost test window vs. our
off-screen screenshots + flags), `src/game/field.cpp` (only `updateCamera` clamp + piece culling in `render`),
`src/game/app.cpp` (options / pause / mouse), `src/game/game.cpp` (mouse in field).

## After both are done
- Merge `gfx-round2` → `main`, then do a code review.
- The owner re-tests the game start up to the end of the first fight.
- Then: an NPC database, the original stat and digivolve tables, and then the **online mode**. AccountService and Party are already behind interfaces for a server.

## Not committed on purpose
STR movie decoding and audio (`src/formats/str*`, `src/platform/audio.*`, and their CMakeLists/dw3check changes) are only in the owner's working tree. Movies are skipped for now, so don't build on them.

## Reference material (owner's PC only, never in git)
`C:\DW3_Project\reference\` holds screenshots of the original game plus NOTES.md (the walkthrough up to the first battle, the lobby map and door targets, and the controls). The original is played in ePSXe; owner keys: arrows, U I O P = face buttons (O = confirm), V = Start, C = Select.
