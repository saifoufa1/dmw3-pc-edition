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

## PC launch flow ✅ (2026-11-16, `src/game/app.*`)
Owner design: MMO-like, as in the original up to the first fight. Launch → original title screen ("Press Start
Button", DAT/SCREEN/TTCOMMPK + TLOGOUSA) → menu **New Game / Continue / Options / Quit** (Continue only when a save
exists) → **save slot screen** (3 slots: account, area, play time, lead Digimon + level; New Game picks a slot and
asks before overwriting a used one, Continue loads one) → New Game skips the intro movie, the street scene and every
talk before the Account Clerk → **Digimon Online sign-up** (our own screen in the original look: hex background
CMNBGD, terminal window + logo from NE_DATA_; account name + password, typed on the keyboard or picked from an
arrow/gamepad letter grid; layout/colours in `src/game/screen_style.h`) → **starter pack** (the ORIGINAL A Balanced /
B Powerful / C Maniac packs, original panel + texts from USNAMEET, `data/starter_sets.cfg`) → "Register account?
YES/NO" → the player arrives in the **Asuka City Main Lobby (S210)** in front of the Login Room door (shot 38),
**without Digimon**. Then the original opening (`data/story/chapter1.story`): Tamer Service counter → "Download your
partner Digimon now?" → the pack's 3 Digimon beam in on the online mark of the floor (our beam effect), Teddy and
Kail log in, the team greets the player (the pack's own event block, USDMG200 3/4/5) → the lower-right exit opens
(before: "Oh yeah! I need my partner Digimon data!") → Asuka City **Central Sector** (S200): Tamer Genji → the
**tutorial battle against his Kuwagamon** (3D model M051KUWA) → fight report → Genji's advice → **the story stops
here** (owner review point): free play. **Continue** = log in with the slot's account → back where the player saved.
In game: Esc = menu (Resume / Save / Return to Title / Quit), Tab = next team member (the other two follow),
autosave on every area change. Accounts go through `AccountService` (`LocalAccountService` per slot now; an
`OnlineAccountService` can replace it without touching the screens). Save: `%APPDATA%\DMW3PC\slotN\story.sav` +
`account.cfg`, `options.cfg` in `DMW3PC` (tests: `--save-dir`; an old single save moves to slot 1 once).
Developer tools (F2 editor, Q/E stage browser, stage HUD) only with `--dev`; the old test flags (`--stage`,
`--battle`, `--new-game`, ...) still start the field/battle directly.
- **Lobby doors** (reference/NOTES.md): S210 zone 0xA2 ↔ **S225 Login Room**, 0xA3 ↔ **S230 Arena Front Desk**,
  0xE1 ↔ **S200 Central Sector** (dragon gate, zone 0xA5). Not linked: 0xA4, 0x65 (destinations unknown); the west
  corridor 0x20 leads to the Central Sector for now (PLACEHOLDER).
- **Each Digimon has its own level, EXP and stats** (TeamMember in the save) and its own digivolve path
  (`data/digivolve_paths.cfg`: base stats, growth per level, forms with the level that unlocks them and their 3D
  model). Battle EXP rule: see HANDOFF.md. PLACEHOLDER: the stat numbers and the non-Agumon paths are ours (the
  original stat tables / Digimon Lab digivolutions are not decoded); only the Agumon line has real 3D models.
- Original game flags used: 6002 = lobby before the partner download, 6004 = after it (WSTAG210 event actors
  appear/disappear by these; set in `chapter1.story`). Observed from the WSTAG show conditions, not decoded code.
- Next: online play (server login, other players in the lobby) — networking not started.
- PLACEHOLDER: the password is only hashed (no security yet).
- PLACEHOLDER: original beam / partner-download effect not decoded (ours: blue column + flash).
- Stages: S210/S211 = Main Lobby (day/night?), S212 = lobby used by the opening cutscene, S225/S226 = Login Room,
  S230/S231 = Arena Front Desk, S785 = MAGAMI Online Center (real world, the Account Clerk), S780 = real-world street
  (opening), S790 = Online Center (inner part).

## Story (chapter 1, `data/story/chapter1.story`)
Login (USDMG200 block 0, only for `--new-game` tests; the PC flow's sign-up replaces it) → Main Lobby S210 without
Digimon → Tamer Service: "Download your partner Digimon now?" (block 2) → the pack's own download scene (block 3 =
pack A, 4 = B, 5 = C: beam-in, Teddy & Kail, the team's greetings) → lobby exit opens (block 8) → Central Sector S200:
Tamer Genji (block 11) → tutorial battle vs his Kuwagamon → his advice (block 12) → **story stops** (owner review
point, objective "Free play"). Town NPCs speak their ORIGINAL lines (WSTAG talk tables, story phase flags 6002 /
6004). Save: one per slot (F5 save, F9 load; autosaves at story beats and area changes).
- Removed for now: the Black Agumon fight at the west bridge and the "chapter complete" card (they came after the
  first fight; the owner wants the story to stop after Genji).
- PLACEHOLDER: Genji's Kuwagamon level / stats are ours; his real party data is not decoded.
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
