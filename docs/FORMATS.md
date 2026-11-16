# Digimon World 3 file formats

All integers little-endian. "Verified" = holds for every file of that type (checked by `tools/dw3check`).

## Disc layout
| Path | Contents |
|---|---|
| `DAT/Z_STAGE/S###PACK.BIN` (237) | Pre-rendered field backgrounds — **decoded** |
| `DAT/FIELD/SPRT/P###XXXX.BIN` (179) | Field character sprites + animations — **decoded** |
| `DAT/FIELD/STAGE/S###MASK.BIN`, `S###TMPK.BIN` | Per-stage mask/attribute data, tile animation pack — open |
| `DAT/MODEL/M###XXXX.BIN` | Battle 3D models (name = species id + short name; e.g. M003AGUM Agumon, M005GREY Greymon, M012MTGR MetalGreymon, M213WGRE WarGreymon). **Decoded** — see "Battle model" below (`src/formats/model.*`, 241/246 files load) |
| `DAT/EFFECT/MEFT1001..1055.BIN` | **3D battle arenas** (one per fight stage) in the battle-model layout — **decoded**, see "Battle arena" (54/55 load) |
| `DAT/EFFECT/MEFT0xxx.BIN` | Technique effect models (battle-model layout, used as projectiles) — partly decoded |
| `DAT/SOUND/MP*.BIN`, `MV*.BIN` | Music/SFX (likely sequence + sample bank pairs) — open |
| `DAT/COUNTRY/USA/USDMG###.BIN`, `USTALK##.BIN` | Story/event text and NPC talk text — **decoded** (see "Message text") |
| `DAT/COUNTRY/{JPN,ENG,FRA,...}` | Same banks, but glyph-indexed (kanji font) encoding — open |
| `*CS.BIN` / `*TM.BIN` pairs (`COMMON/CMFONT`, `SCREEN/ST*`, `DGLB`, `NAMEENT`, `MCARD`, `TRAINING`, `TEXT/JTWNDCST`+`JTWNDTIM`, `MODEL/PGFGTC00`+`PGFGTW00`) | UI graphics: CS = cell sheet (parts + pieces), TM = texture pages — **decoded**, see "UI cell sheet" (19/19 sheets) |
| `DAT/CARD` | card game graphics — open |
| `PRO/*.PRO` | Code overlays (field, battle, menus, per-stage `WSTAG###`) — open |
| `STR/*.STR` | MDEC movies — open |

## TIM (PS1 standard) — verified
`u32 0x10`, `u32 flags` (bits0-1 bpp: 0=4,1=8,2=16; bit3 = has CLUT).
CLUT block (if flag 8): `u32 len, u16 x, y, w, h, w*h u16 colours`. Image block: `u32 len, u16 x, y, w, h, data` where `w` is in 16-bit VRAM units (4bpp → 4 px per unit, 8bpp → 2).
Colour 15-bit BGR555 + STP; `0x0000` = transparent. Validation must check both block lengths (`len == 12 + w*h*2`): a bare magic+flags match occurs inside sprite animation scripts.

## RLEN compression — verified
`"RLEN"`, `u32 decompressedSize`, then a stream: `c & 0x80` → repeat next byte `c & 0x7f` times; else copy `c` literal bytes. (`c & 0x7f == 0` is invalid.)

## Field sprite pack `FIELD/SPRT/P###XXXX.BIN` — verified (179/179)
- `u32 offsets[n]`, `n = offsets[0] / 4`.
- Leading entries are TIMs (4bpp, one per frame, e.g. 24x32 or 32x32).
- Remaining entries are animation scripts: repeated `{i32 duration(ticks @60Hz), i32 frame, i32 ox, i32 oy}`, terminated by one `i32`: `0` = loop, `-1` = play once. (ox, oy) = frame top-left relative to the actor's feet.
- Movement set convention (tamers and partner rookies; 18 packs have it): anim slots `0-4` idle, `5-9` walk, `10-14` run, each ordered **S, SE, E, NE, N**. NW/W/SW = mirror of NE/E/SE (draw x = feet − ox − width).
- Other slots: emotes/actions (not yet mapped). Some packs end with non-animation data (left empty by the parser).
- Known ids: P001PLAY = Junpei(boy tamer), P006AGUM Agumon, P003PKEN Patamon?, P004KOGU, P005KOET, P007VMON Veemon, P008GIRU Guilmon, P009RENA Renamon.

## Stage background pack `Z_STAGE/S###PACK.BIN` — verified (237/237)
Header (sector 0, 0x800 bytes):
| Off | Type | Meaning |
|---|---|---|
| 0x00 | u32 | number of non-empty cells |
| 0x04 | u32 | grid width (cells) |
| 0x08 | u32 | grid height (cells) |
| 0x0C | u32 | slot stride (bytes, multiple of 0x800; 18432 or 20480 seen) |
| 0x10 | u16[w*h] | per-cell stored size, 0 = empty cell |

Each cell is 128x128 px; cell `i` (row-major) lives at `0x800 + i*stride` (empty cells still own a zeroed slot; file size = 0x800 + w*h*stride).
A slot is either `RLEN`-compressed or raw. Decompressed chunk:
| Off | Type | Meaning |
|---|---|---|
| 0x00 | u32 | offset of the TIM inside the chunk (0 = empty) |
| 0x04 | u32 n, then n × Rect | layer A rects |
| … | u32 m, then m × Rect | layer B rects (hypothesis: upper/foreground layer) |
| TIM offset | TIM | 8bpp atlas, 256-colour CLUT |

`Rect = u16 dstX, dstY, srcX, srcY, w, h` (dst relative to the cell). The current renderer draws both layers into one background.
**Open:** whether layer B should be drawn above actors; black holes are filled by the MASK/TMPK scene pieces placed by the WSTAG overlay (see below).

## FIELD/STAGE/S###MASK.BIN — verified (237/237)
`u32 offsets[]` (0-terminated) → RLEN blocks, each a TIM. Block 0 = 8bpp 128x256 with `n` CLUT rows (256 colours each);
blocks 1.. = 16bpp 64x256 TIMs that are really 8bpp texture pages (128 texels wide). Side by side they form one
8bpp **scene-piece atlas** (`128 * blocks` x 256) that uses block 0's CLUT rows. Code: `src/formats/field_stage.cpp`.
Contents: the pieces that the Z_STAGE background leaves black (doors, machines, fountains, signs, lamps, glows).
**Open:** the 4bpp effect parts (kind 0, below) use a palette that is *not* in MASK (probably a shared VRAM CLUT
loaded by FIELDSTG); we draw them additively with a generated warm ramp.

## FIELD/STAGE/S###TMPK.BIN — verified for parts/pieces/walk/trigger, partial otherwise
`u32 sectionOffsets[]`; section kinds are recognised by content, not by index (order varies):

**Parts/pieces table** (offset table of entries):
| entry | layout | confidence |
|---|---|---|
| 0 | `n × {u16 x, y, w, h, clut, rowOrDepth, kind}` (14 bytes, table padded to 4) | verified |
| 1 | ascending u8 frame ids, `0xFF` terminated. Piece for frame id `f` = (index of `f` in this list) + 2 | verified |
| 2.. | piece: `u16 count, u16 0, i16 flag, count × {u16 part, i16 dx, i16 dy}` | verified |
- `kind 1` = 8bpp part in the MASK atlas, CLUT row = `rowOrDepth - 256`. Wide pieces are cut into vertical strips laid side by side via `dx`.
- `kind 0` = 4bpp effect part; `x` is in 4bpp texels; `clut` = palette (row `>>4`, bank `&15`); `rowOrDepth` 0xE9..0xFC unknown.
- `flag -1` = opaque piece, other values (seen: 3) = glow/additive.

**Quadtree maps** (walk map and trigger map, same format; one each, told apart by max value: walk < 0x20):
`u32 off[6]` (off[0] = 0x18) + `u32 (gh << 8) | gw`. At `off[0]`: `u8 gw, gh`, then one u8 node per 128 px grid cell.
Level 1 = 4 × u8 children, levels 2–4 = 4 × u16 children (order TL, TR, BL, BR), level 5 = 8x8 byte leaves
(128 → 64 → 32 → 16 → 8). Child/node index 0 = an all-zero block. Verified on S200/S202/S218 by overlaying on the map.
- Walk map values: `0` blocked, `1` floor, `6` stairs (other small values: floor variants, open).
- Trigger map values: `(type << 5) | index`; type 1 = map edge exit, type 3/5 = doors and links (seen on S200/S202/S218).
  The destination of each zone is in the WSTAG **area link table** (desc +0x14, below): `value & 31` = the record index.
Other TMPK sections (animation timing, contain the 0x1802 marker) are not decoded.

## PRO/WSTAG###.PRO — stage overlays — partial (236/237 parsed)
Raw MIPS code; the init function starts at file offset 4 = `0x800A4CA0` (word 0 = 0x00966754 header).
**Data pointers map to file offsets as `addr - 0x800A4CA4`** (verified: with this base every actor-list,
condition-list and talk-table pointer lands exactly on a record start in all 236 overlays; the old
"record starts at pointer − 4" rule for scene objects was this off-by-4). FIELDSTG.PRO (loaded at `0x80082448`) runs the
stage's init function, which fills a **stage descriptor at `0x800990B4`** with `sw` stores of constants/pointers.
`src/formats/stage_overlay.cpp` runs a tiny register tracker (lui/addiu/ori/addu moves, `sw` into the descriptor,
stop at `jr ra`) to recover it without an emulator. WSTAG260 has another layout (not word aligned) and is skipped.
| desc offset | meaning | confidence |
|---|---|---|
| +0x08, +0x0C (hi16), +0x1C | file ids (MASK/TMPK/PACK, order unresolved) | low |
| +0x10 | pointer to the **scene-object list** | verified (S200/S202/S218) |
| +0x14 | **area link table** (doors / exits / event zones, below) | high (S200/S210/S220/S225/S230 match the reference run) |
| +0x20 / +0x24 | runtime lists / callbacks | low |
| +0x28 | VRAM sprite slots, 16 bytes each | medium |
| +0x2C / +0x30 | player spawn, 8.8 fixed point (S200 = 1000,236) | high |
| +0x34 | 1 only on S200 | unknown |
| +0x44 | 205 or 240 (BGM or area id?) | unknown |
| +0x4C | pointer list of **event actors** | high |

**Scene object** (18 bytes, record starts at pointer − 4; FIELDSTG 0x80088880–0x80088BE0 reads stride 18):
`u8 type, u8 link, u16 flags, u16 a (lo = frame id, hi = mode), u16 b (loopFrom | loopTo << 8), u16 c (delay),
i16 x, i16 y (top-left of the piece in map px), u16 depthY (sort line), u16 unk`.
mode 0 = static, mode 1 = cycle frames loopFrom..loopTo, mode 2 = frame + k (hypothesis). Delay ≈ `c * 2` ticks (guess).
409/7108 objects have no piece (frame id not in TMPK) — event-controlled objects with link ≥ 0x64?, open.

**Event actor** (20 bytes) — high confidence (FIELDSTG 0x8008A7CC–0x8008A878 reads exactly these offsets):
| off | type | meaning |
|---|---|---|
| +0x00 | u32 ptr | **show conditions**: the actor is created only if this flag list holds (callback `*(0x80048288)(list, 1)`); 0 = always |
| +0x04 | u32 ptr | **talk table** (below); 0 = silent |
| +0x08 | i16 | charId, passed to the actor creator 0x80090450 (charId 1 = the player record, at 0,0) |
| +0x0A | i16 | slot (several records share a slot: one per story state) |
| +0x0C/+0x0E | i16 | x, y (map pixels, feet) → actor +0x50/+0x54 (<<8) |
| +0x10 | i16 | dir → actor +0x60 |
charId is **not** the `P###` sprite number: see the FIELDSTG character table below.

**Creation rule** (FIELDSTG loop 0x8008A7CC): EVERY record whose show list holds is created (not only the first of
its slot); charId 1 (the player), 0x6A, 0x146 and 0x147 are skipped. So the story phase flags are **exclusive**: when
the game sets `6004` it clears `6002`, otherwise both lobby phases would appear at once. Records at (0,0) are placed
later by an event script.

**Flag list** (conditions and assignments): `{u16 flag, u16 value}` pairs ended by `flag = 0xFFFF`. A condition
list holds when every `flag == value`. Flags look like `0xGGBB` (group, bit): story-phase flags `0x60xx/0x70xx`
(e.g. `6004` = first visit of Asuka City, then `600c`, `600e`, `7015`, ...), event flags `0x1Axx`, `0x72xx`...
In WSTAG210 (Main Lobby) every slot lists its variants in story order: `6002` = lobby before the partner download,
`6004` = after it (Teddy = char 178 `P012MINT` and Kail = char 179 `P014TAKT` appear next to the floor mark only with
`6004`). Phase flags are exclusive (see the creation rule; an earlier note here said "cumulative": wrong).
Flags `< 0x0100` (e.g. `0000`, `0010`, `0011`) are used like per-NPC "talked before" flags — medium confidence;
the engine keeps them per stage+slot.

**Talk table**: `{u32 condPtr, u32 setPtr, u32 msgId}` triples ended by three zeros. The first entry whose
`cond` list holds is spoken: message `msgId` of the area's talk bank, then the `set` list is applied
(e.g. Tamer Genji: `[0000=0] set [0000=1] -> 0x69`, then `[0000=1 7200=0] -> 0x6d` ...). Asuka City
(S200–S219) uses `USTALK00.BIN` (all ids and speakers match) — how a stage selects its talk bank is open.
`dw3check --dump-text S200` prints every actor, its conditions and the resolved lines (console only).

**Area link table** (desc +0x14) — high confidence: 24-byte records, the table ends at a record with kind 0 and arg 0.
| off | type | meaning |
|---|---|---|
| +0x00/+0x02 | u16, u16 | condition 1: flag, value (`0xFFFF` = none) — the link works only while it holds |
| +0x04/+0x06 | u16, u16 | condition 2 (same) |
| +0x08 | u16 | kind: **1 = warp**, 8 = event zone (arg = event id, run by the stage's event code), 7 = seen once (S202), open |
| +0x0A | u16 | warp: the target **stage key** (`0x200 + n`, see the FIELDSTG stage table) |
| +0x0C/+0x0E | i16 | warp: arrival x, y in the target stage (map px) |
| +0x10 | u16 | warp: facing on arrival |
| +0x12 | u16 | warp: the door scene object to open (scene object `link` 0x64.., 0 = none) |
| +0x14 | u16×2 | padding |
A trigger zone with value `v` (TMPK trigger map) uses record `v & 31`. At runtime the arrival goes to desc +0x64/+0x68.
Every warp of S200/S210/S220/S225/S230 is symmetric: its arrival point lies inside the zone of the matching door
back (checked by `dmw3 --door-test`). The lobby's lower-right walkway (0xE1) leads to S220, and S220 to S200.

**Scene objects as foreground / doors:** objects with flags `0x400` and a `depthY` line are foreground pieces (counter
fronts, railings): drawn in depth order with the actors (actor feet below the line = in front). Objects with
`link >= 0x64` and flags `0x640` are the door panels a warp opens (S210: obj 38 link 0x64 = Login Room door).

## FIELDSTG.PRO tables (loaded at `0x80082448`) — high confidence (`src/formats/fieldstg.*`)
- **Character table** at `0x80099134`: 393 × u32, index = event actor charId. hi16 = the sprite's **file id**,
  lo16 = unknown. Read by the creator callback desc +0x74 = `0x80091398`. File id 0 → no sprite (an invisible actor,
  e.g. S210 chars 112/113 at the counter: the engine keeps them as solid blockers).
- **Collision size** at `0x80099758`: one byte per charId (32 for most people → radius ~16), callback desc +0x78.
- **Stage table** at `0x800998E4`: 12-byte records `{u32 key (0x200 + i), u32 WSTAG file id, u32 ptr}`.
  Stage keys: S200 = 0x200, S210 = 0x203, S220 = 0x206, S225 = 0x207, S230 = 0x208 (the key is not the S number).

## File ids — high confidence
The executable `SLUS_014.36` (USA) holds a u32 **LBA per file id** at file offset `0x35378` (RAM `0x80044B78`).
`dw3check --map-file-ids DISC.cue data/file_ids.cfg` reads it from the raw disc and names each id by matching the
file's first 2048-byte sector against the extracted files: `data/file_ids.cfg` = `0xID PATH` lines (names only,
2180 ids). It is committed (it needs the raw disc to rebuild).

## Our extracted tables: `data/npcs/S###.npc` and `data/doors/S###.door` (generated, not in git)
`dw3check --extract-npcs data [S210 ...]` writes them from the player's own game data (`build.bat` runs it when
`data/npcs` is missing). `dw3check --dump-npcs S210` prints one stage. Flags hex, values decimal, one line per record:
```
npc SLOT CHAR SPRITE X Y DIR [show F=V,F=V] [talk COND/SET/0xMSG]...   SPRITE "-" = no sprite (invisible blocker)
door N warp STAGE X Y DIR [link 0xNN] [if F=V,..]                     N = record index = trigger value & 31
door N event 0xID [if F=V,..]
door N kind K 0xARG X Y DIR                                           other kinds, kept raw
```
COND / SET are flag lists (`-` = empty). The field places NPCs **only** from the .npc table (plus story actors that
name an original slot: `actor ID STAGE talk slot N "Name" ...` in data/story/*.story) and uses the .door table for
the stage's trigger zones; `.stage` exits are used only by stages without a .door table (e.g. our forest S400).
The format is line based so a database import later is a plain parse.

## Field common effects `DAT/FIELD/FIELDCOM.BIN` — verified (loads as a combined UI pack, `loadUiPack`)
Same layout as the combined title packs: 2 cell groups + 2 texture groups (`dw3check --dump-pack FIELD/FIELDCOM
out.bmp` prints every piece with its origin). Origins are relative to the actor's **feet**.
- Group 0 pieces **35-38** (16x16, origin +4,-36): the red/white/blue spark right of the head = the **door marker**
  shown when the player can use a door (reference shots 38, 49). The engine cycles them (`Field::loadDoorMark`);
  the frame timing (6 ticks) is our guess — the script that plays them is not found yet (FIELDSTG, open).
- Group 0 pieces 39-49 (same origin): flashes / rings / stars, probably the "!" / event markers — open.
- Group 0 pieces 0-19: small icons over the head (24x24/16x16); 20-34: speech/thought bubble growth frames
  (8x10 -> 20x22) — open. Palm trees, rings, sparkles, glows and the three big blue/green/red rings (bottom of
  the sheet) are other field effects (ports? save points?) — open.
- Group 1 pieces 13-31 (32x44 .. 32x76, origin -16,-50..-94): beam columns (download / teleport beam?) — open.

**Scene objects (for stage drawing):** what the NPC/door work confirmed about the WSTAG scene-object list
(desc +0x10, 18-byte records): flags `0x400` + depthY = foreground pieces sorted with actors (S210 counter
fronts = objects 89-102, frames 10-23); `link >= 0x64` with flags `0x640` = the door panel a warp opens (link of
the area link record); mode 1 = frame cycle loopFrom..loopTo with delay `c`. Lights / blinking screens are
scene objects with additive pieces (`flag != -1`) or mode-1 cycles. Not found: a separate light table — the
409 objects without a TMPK piece (event-controlled) are the best lead for the missing pieces.

**Still not decoded:** which code plays the `USDMG###` event blocks (event zones kind 8, callbacks +0x20/+0x24),
link kind 7, talk bank selection, the original beam / port effects (ours are drawn by the engine), the marker
timing.


## Battle model (`DAT/MODEL/M###XXXX.BIN`)

All little-endian. File = u32 offset table (entry 0 = table size). Section order differs per
model, so sections are identified by content:

- **Hierarchy**: `u32 textureSection, u32 count`, then `count` x `(u32 parent, u32 meshSection, u32 trackSection)`.
  `parent` is the 1-based index of an earlier row (0 = root). Mesh section may be empty (no geometry).
- **Texture**: `u32 8, u32 0`, then RLEN -> TIM with a single 16-bit block 64x256 words: 256x256 4bpp
  texels, with the CLUTs stored inside the image (rows ~216-255, 16 words each). Colour 0x0000 = transparent.
- **Track** (one per node): `u32 posOff(=16), rotOff, sclOff, end`; keys are `int16 x, y, z, time`.
  Rotation 4096 = 360 deg, scale 4096 = 1.0, last key time 30000. All tracks share one timeline;
  linear interpolation. Node matrix = T * (Rx*Ry*Rz) * S, world = parent * local. PS1 axes: y down, model faces -z.
- **Mesh**: `u32 vertOff, normOff, primOff, 0, 0, vertEnd`. At vertOff: `u32 n, u16 pad, n x int16[3]`;
  at normOff the same for normals (4096 = 1.0). Primitive stream at primOff, one byte command at a time:
  - `0x80-0xFE`: set render state register `hi nibble` = `lo nibble`.
    `8`: 0 tri / 1 quad, `B`: 1 = unlit (no normals, flat colour), `E`: PS1 blend mode, `9,A,C,D,F`: not needed yet.
  - `0x02 r g b`: flat colour for unlit prims (128 = 1.0).
  - `0x01` + 7 bytes `uBase, 0, vBase, clutX, clutY, 0, 0` then a primitive; `0x00` = primitive with the same header.
  - primitive = `k` vertex indices, `k` normal indices (lit only), `k` x `(u, v)` (add uBase/vBase). Quad order 0,1,2 + 1,3,2.
  - `0xFF` ends the stream.
- **Clip table** (section 0): `u32` offsets (0 = none, ~62 slots). A clip = segments `u16 start, len, from, to`
  (play timeline frames from..to, or blend to the next segment when from = to = 0), ended by
  `u16 length, 0, flag(0x8000 loop / 0xFFFF once), 0`. Slots mean the same action for every Digimon:
  0 idle, 1 move, 2 flinch, 3 breath/ranged, 4 guard/crouch, 5 knocked out, 12-13 roar/power,
  15 charge, 16 strike, 28/34 two/three-hit combo, 38/44 victory.
- Another section (`u32 72` header, 18 entries) is probably the effect/sound script per action — not decoded.
- 5 files use other layouts (effects / multi-part): reported by `dw3check` as SKIP.
- No battle model exists for the field-only Digimon Black Agumon (P171BAGM), Blue Agumon (P167SAGM) or Digmon (P123DIGM): no file name matches and a contact sheet of all 241 loadable models shows none of them. The PC edition draws Black/Blue Agumon as M003AGUM with a palette recolour (`render/model_renderer` `Recolor`) and keeps Digmon as its field sprite.


## Battle arena (`DAT/EFFECT/MEFT1001.BIN` .. `MEFT1055.BIN`) — verified 54/55 (`src/formats/arena.*`)

Each battle is fought in a whole 3D arena. Arena **n** (1..55) is the file `MEFT(1000+n)`.

**How it was found.** The battle overlays (FIGHTSTG / WFIGHTMN, base 0x80082448 / 0x800A4CA0) load
no field background, so the arena had to be a 3D file. Rendering every `DAT/EFFECT` file with
`dw3check --dump-arena` showed that exactly the run MEFT1001..1055 are big static scenes (sky
domes, floors, walls) while all other MEFT files are small effects. The numbering comes from the
battle-test overlay **WFIGHTTS** (real base 0x800A4C9C): its stage menu has a pointer table (file
offset 0x3624) to Shift-JIS names "ＭＦＳＴＧ００１".."ＭＦＳＴＧ０２９" and "ＭＥＦＴ００３０".."ＭＥＦＴ００５５",
and the menu stores `scroll + cursor + 1` (the 1-based stage number) into `*(*(0x8004D5B8 + 0x50))`,
the fight setup in the main executable's global table. Confidence: **high** for "file = arena",
**medium** for "number n = MEFT(1000+n)" (the 1..55 count and order match; the loader itself is in
the main executable, not on the data disc we read).

**Layout** = the battle-model layout (see "Battle model"): offset table, hierarchy, one track per node
with 2 identical keys (static, no animation), byte-coded meshes, 1-3 RLEN/TIM texture pages. Verified:
- all prims are **unlit** (flat colour x texture, no normals used);
- typical nodes: 6 hexagonal sky/backdrop walls (radius ~32640, y -16768..+5120), 4 ground quadrants
  (radius ~19200, hills at the rim up to y -5760), 1-2 centre floor pieces at y = 0;
- the vortex arenas 1032..1052 have their floor at y = +2432; 1021/1022/1028/1030/1031/1055 have no
  floor mesh under the fighters (space / data / dome);
- **CLUT in another page**: in multi-page arenas the CLUT row a prim names is often blank (all
  16 words 0xFFFF) in its own texel page and present in another page. `Model::textureFor` falls back
  to the first page whose palette at (clutX, clutY) is not blank (verified visually on all 54).
- **MEFT1018** fails with "mesh 27: unknown mesh command 3" (other layout); the engine falls back to
  arena 1.

**Ground (engine-side, not from the game).** The original keeps fighters at fixed spots, so there is no
collision data. The PC edition builds a 192x192 grid (250-unit cells from -24000): every triangle
with |n.y| >= 0.55 writes its height into the cells it covers; the play height is the candidate closest
to y = 0 near the centre; cells within 900 of it are kept if they connect to the centre without a
step over 220 (flood fill), the arena centre is half the centroid of that region, and the walk radius
is the largest ring (90% of 64 directions on floor). No floor, or a platform under 4500 wide = a flat
plane at the play height (radius 7000-9000). `dw3check --dump-arena` prints this per arena.

**Field stage -> arena.** Not found: the overlays never name an arena; the stage descriptor field at
WSTAG +0x3C (values 4..0x44, with +0x40 = 0x60000000 | N<<18, also set in towns and replaced by 0x1F in
story states) looks like a BGM/sound id, not an arena. The real table is probably in the main
executable (SLUS), which this project does not read. The PC edition uses a **hand-made table**
(`kStageArenas` in `src/formats/arena.cpp`, confidence **low**: matched by comparing the field
background with the arena), e.g. S200-209 Asuka -> 55 city dome, S330-359 grassland -> 1 sky island,
S400 forest -> 8 pine forest, S550-579 desert canyon -> 3 sand ruins, S695-709 snow -> 14 snow
mountain, S865-899 circuit boards -> 28/30/31 data arenas. Stages not in the table use arena 1, and an
arena that does not load uses `arenaFallback()` (the closest theme), never the 2D backdrop.

| n | look | n | look |
|---|---|---|---|
| 1 | sky island, grass | 2 | sky island, beach + cliffs |
| 3 | sky, sand ruins | 4 | sky, rocky canyon |
| 5 | sky, desert dunes | 6 | stone dungeon (corridor) |
| 7 | dark stone dungeon | 8 | pine forest clearing |
| 9 | night, moon, rock | 10 | digital grid |
| 11 | water | 12 | factory hall |
| 13 | sky island, forest | 14 | snow mountain |
| 15 | space station hull | 16 | space station deck |
| 17 | space, planet | 18 | other layout (not loaded) |
| 19 | crystal spikes (purple) | 20 | tech platform |
| 21-22 | space, planet | 23 | digital ring arena |
| 24 | wooden barrel / tree trunk | 25 | ice crystal arena |
| 26 | dark violet void | 27 | ice cave |
| 28, 31 | data rings (31 gold) | 29 | light tower |
| 30 | binary data sphere | 32-52 | coloured vortex (red, blue, cyan, green, yellow, white, violet; each plain / ring / wall) |
| 53 | grey sky dome | 54 | desert dunes 2 |
| 55 | city lights dome | | |

Open: the stage -> arena table and the arena loader in the main executable; MEFT1018's mesh command 3.

## Message text (`DAT/COUNTRY/USA/US*.BIN`) — verified (18/18 banks, 8712 messages, 0 unknown codes)

Parser: `src/formats/dw3_text.*`; checked by `dw3check` (every message must terminate and contain only known
codes). The engine reads these at runtime; our files only store message ids (never the text).

**Event banks `USDMG###.BIN`** (### = area: 200 Asuka, 260, 270, 300, 305, 400, 500, 600, 700, 800):
`u32 blockOffset[n]` (absolute; first offset = table size = 4n). Each block is one event/scene:
`u32 count`, `count × u32 offset` relative to the block; entry 0 is always an empty message (u32 0).
Example (USDMG200): block 0 = login, 1 = help menu strings + texts, 2 = "Download your partner Digimon now?"
+ options, 3/4/5 = partner download + meeting Teddy and Kail (one block per starter team), 11/12 = Tamer
Genji's first battle (before/after), 24 = the Game Master announcement.

**Talk banks `USTALK##.BIN`**: `u32 count`, `count × u32` absolute offsets; unused ids are 4-byte zero entries.
Message ids come from the WSTAG talk tables (above).

**Encoding** (USA): one message = byte stream ended by `02 00` (bare `00` ends short menu strings).
| bytes | meaning | confidence |
|---|---|---|
| `0E`–`27` | `A`–`Z` | verified |
| `28`–`41` | `a`–`z` | verified |
| `04`–`0D` | digits `0`–`9` | high ("{07} partner Digimon" = 3, "{09} servers" = 5 listed) |
| `E6` / `E7` | `?` / `!` | high |
| `03` | clear the box / new page (follows the waits) | high |
| `01 xx` | symbol: `01` space, `04` `,`, `05` `.`, `07` `:`, `0F` `/`, `13` `'`, `14` `"`, `15` `(`, `16` `)`, `1A` `-`, `E6` `?`, `E7` `!`; a letter code after `01` is a data typo (5 cases) shown as the letter | high |
| `02 00` | end of message | verified |
| `02 01` | new line (the box has 3 lines) | verified |
| `02 02` | wait for button (page end) | high |
| `02 07` … `02 07` | speaker name field (start, end) | verified |
| `02 08 01 02 05 01` | player name (4 argument bytes, always these values) | medium (meaning of the args open) |
| `02 09` | player name (used in the speaker field) | high |
The other country folders (ENG/FRA/GER/ITA/SPN/JPN, prefixes ES/FS/...) and `DAT/FIELD/FIELDMES.BIN`,
`FIELDCOM.BIN`, `DAT/TEXT/JTWND*` use 1–2 byte glyph indices into a kanji font (e.g. `94 AB 9D E5 ...`) — open.


## UI cell sheet (`*CS.BIN`) + UI texture pack (`*TM.BIN`) — verified (19/19 sheets, 2988/2988 parts inside their pages)
Parser `src/formats/ui_sheet.*`, runtime use `src/ui/*`, checked by `dw3check` ("ui" line); contact sheets with
`dw3check --dump-ui <NAME> out.bmp [parts]` (scratch folder only, never commit). Same part/piece layout as the TMPK parts table.

**Texture pack (TM / `JTWNDTIM` / `JTRNGTIM` / `PGFGTW00` / `FGTEFTIM`)**: `u32 table[n]`, `n = table[0] / 4`.
Each entry (absolute; 0 = empty group) points either straight at a page or at a sub-table of the same shape
(entries relative to the sub-table, 0-terminated) listing the pages of one **group**. A page is an RLEN or raw TIM.
Almost all pages are 16-bit TIMs of 64 x 256 words placed at (0,0): really one PS1 **VRAM texture page** each
(256 4bpp texels wide), with the CLUTs stored *inside* the page (word x 0..63, rows ~145..255).
Pages of a group sit side by side (texel x continues across pages). Some groups are 8bpp TIMs with their own
CLUT (`STDGLBTM` group 2, `STTITLTM`, `STWMP*`). `PGFGTW00` group 0 is a plain 156x12 16-bit TIM (12 frames of a
12x12 glowing orb cursor + 1 empty), group 1 is the battle HUD page. Cell group *i* uses texture group *i* when that
holds VRAM pages, else the next one that does (`uiTexGroupFor`) — high confidence (all 19 sheets render correctly).

**Cell sheet (CS / `JTWNDCST` / `JTRGCAST` / `PGFGTC00`)**: `u32 table[n]` (same shape) → cell groups
(0 = empty). Group: `u32 off[m]` relative to the group, `m = off[0] / 4`:

| entry | layout | confidence |
|---|---|---|
| 0 | parts, 14 bytes: `u16 x, y, w, h, clutX, clutY, kind` | verified |
| 1 | frame ids, u8 ascending, `0xFF` terminated; piece *i* has id `ids[i]` | verified |
| 2.. | piece: `u16 count, u16 0, i16 flag, count × {u16 part, i16 dx, i16 dy}` (later layers on top) | verified |

- `kind 0` = 4bpp: `x` in 4bpp texels across the group's pages; CLUT = 16 words at word (`clutX`, `clutY`) of the
  same pages (`clutX` is always 0/16/32/48). `kind 1` = 8bpp: `x` in 8bpp texels; `clutY >= 256` = row `clutY - 256`
  of the group's 8bpp TIM CLUT. Colour `0x0000` = transparent. Verified visually on CMFONT, PGFGTC00, STFRPT.
- `flag`: −1 normal; 1 semi-transparent (dark shadow overlays, 320x60 black bands); 2/3 glow/flash. Medium
  confidence (we draw 1 as 50 % alpha, 2/3 additive).
- The code picks pieces by frame id. Meanings below come from looking at the pieces; on-screen positions are set by
  the overlay code (FIGHTSTG / STFGTREP / STSTATUS) and are **not** decoded yet — our layouts are reconstructions.

### Common font + windows: `DAT/COMMON/CMFONTCS` + `CMFONTTM` (1 group, 2 pages, 745 parts, 94 pieces)

| parts | content |
|---|---|
| 0 | 16x16 yellow selection square (piece 0x40) |
| 1–9, 38–63 | window pieces: 0x41 136x17 name plate, 0x42 header with tab, 0x43 248x40 slanted banner, 0x44 156x42 box, **0x45 320x50 message window** (part 62 ×16 + edge 63 ×2: navy body, lavender double edge) |
| 10–37, 117–134 | item / equipment / element icons 12x12 |
| 39–42 | yellow "next page" arrow + chevrons (piece 0x0D) |
| 64–71 | 320x60 semi-transparent dark bands (pieces 0x08/0x09, flag 1) |
| 80 + s | **white font symbols** for text code `01 s` (81 space, 84 `,`, 85 `.`, 87 `:`, 89 `?`, 90 `!`, 94/106 `-`, 95 `/`, 99 `'`, 100 `"`, 101 `(`, 102 `)`, 103/104 `「」`, 105 `+`, 108 `=`, 109 `<`, 110 `>`, 111/112 `▶`, 115 `♥`, 116 `♪`); 130/132 = PS button glyphs used as `01 32` / `01 34` |
| c + 135 | **white font 8x12** (cell width = advance: 8, narrow glyphs 4) for code `c` = `04`..`E4`: digits 139–148, `A`–`Z` 149–174, `a`–`z` 175–200, hiragana/katakana 201–363 |
| 364–419 | white accented Latin (¡ £ § µ ¿ Ä Å Æ Ç Ñ Ö Ø Ü ß à â ä ç è é ...; EU builds, unused by USA text) |
| 420–431 | orange 8x9 symbols: space `,` `.` `?` `!` `-` `―` `~` `'` `「` `」` |
| c + 428 | **orange font 8x9** for code `c` (`0` = 432, `A` = 442, `a` = 468, kana 494–), accented 650–712 |
| 713–740 | **small HUD font 8x8** (orange): `:` 713, `-` 714, `/` 715, `0`–`9` 717–726, `B E H I L M P R T V X` 727–737, `キ ス ル` 738–740 (exactly what "HP MP LV EXP BIT" need; no `D`) |
| 741–744 | PS face-button icons (pieces 0x39–0x3C) |

Text code → glyph (high confidence; renders every USA string we tried correctly): `01 s` → part `80 + s`;
single byte `04`–`E4` → `c + 135`; `E5` `/`, `E6` `?`, `E7` `!`, `E8` `-` → the symbol glyphs. Control codes as in
"Message text" above; `02 05 n` = insert argument *n* (numbers / names: "EXP gained:\n{1} EXP").

### Battle HUD: `DAT/MODEL/PGFGTC00` (cells) + `PGFGTW00` (texture) (75 parts, 44 pieces)

| frame id | piece |
|---|---|
| 0x00 | enemy status plate 148x44: name box (x 7–134, y 6–17), HP bar slot (fill area x 7–134, y 22–27) |
| 0x01 | partner status plate 149x44: name box (x 11–72), level box (x 78–144, y 9–18), HP slot (x 14–141, y 22–27), number tab bottom right |
| 0x02 | small tab; 0x05–0x08 = 16x16 square marker |
| 0x0A | 104x64 large panel (flag 0) |
| 0x0B | 40x8 gauge fill strip placed at (5,1) of the gauge |
| 0x0C–0x0F | 12x9 gem, same part in 4 palettes (CLUT rows 252–255: cyan / green / red / orange) |
| 0x1E | 52x10 glass gauge (interior x 5–44, y 3–7) — we use it as the DV gauge |
| 0x1F / 0x20 | 8x8 white triangles (scroll arrows) |
| 0x22–0x24, 0x2B–0x2F | icon square + name tabs (84x19 plate, 44x24 boxes) |
| **0x25 / 0x26 / 0x27 / 0x2A** | **command windows** ~136x105: plain / ruled lines + footer tab / 7 icon boxes (cyan edge, corner mark, chamfer) |
| 0x28 | 44x24 small box; 0x29 = 100x31 tab |
| 0x31 | 320x38 message bar |
| 0x33–0x36 / 0x37–0x3A | 40x8 sparkle / lightning strips (animated gauge-full effect) |
| 0x3C–0x3F | 12x7 gem in 4 palettes |

Bar colours: the palettes at words 48–53 of rows 252–255 hold `[dark outline, 0, shade, light, white, …]` for
cyan / green / red / orange; we use words 50/51 as HP/MP/DV fill colours (medium confidence: the original fill is
untextured and drawn by code).

### Fight report (post-battle): `DAT/SCREEN/STFRPTCS` + `STFRPTTM` (44 parts, 38 pieces) — overlay `PRO/STFGTREP.PRO`

| frame id | piece |
|---|---|
| 0x00–0x1C | 32x32 partner portraits (chibi Digimon; one per form; index ↔ Digimon not mapped) |
| 0x1D | 320x38 title bar |
| 0x1E | full-screen background: 48x48 checker tiles covering x −96..336, y −96..240 (scrolls) |
| 0x1F | partner box: portrait stand (diamond) + name tab + level tab + info box (110x42) |
| 0x20 / 0x21 | 172x18 and 108x21 plates |
| 0x22 / 0x23 | dark "level up" arrow shadows (flag 1) |
| 0x24 / 0x25 | yellow highlight frames (portrait + tab, long plate) |

Texts: `USFGTRPT.BIN` = `u32` at 0 → list of absolute string offsets (0-terminated): 0 "EXP gained:\n{1} EXP",
1 "LV", 2 "Level gained!", 3 "Learned Tech:\n{1}", 4 "Can load Tech:\n{1}", 5 "{1}\nLevel {2}!",
6 "Digivolve learned:\n{1}", 7 "Digivolve Slot full…", 8 "BIT gained:\n{1} BIT", 9 "Item gained:\n{1}". (high)

### Title screen packs (combined): `DAT/SCREEN/TTCOMMPK`, `TLOGOUSA`, `TLOGOJPN`, `DAT/NAMEENT/NE_DATA_` — verified (4/4, 309/309 parts)
Parser `loadUiPack` (`src/formats/ui_sheet.*`), runtime `src/ui/pack_art.*`, checked by `dw3check` ("uipack" line),
contact sheets + piece origins with `dw3check --dump-pack TTCOMMPK out.bmp` (or `NAMEENT/NE_DATA_`).
ONE file holds both halves: `u32 table[n]` (`n = table[0] / 4`, absolute offsets); each entry is either a **cell
group** (same layout as a CS group) or a **texture group** (a page, or a 0-terminated sub-table whose entries are all
pages). Tell them apart by trying the texture form first: every non-zero sub-table entry must be an RLEN/TIM page.
Pairing (high confidence, every part lands inside its pages): a run of N cell groups is followed by a run of texture
groups; the k-th cell group of the run uses the k-th texture group (leftover cells use the run's last one).

| file | groups | content |
|---|---|---|
| `TTCOMMPK` | cells 0-3 + 8, textures 4-7 + 9 | 0/1/2: title background (circuit board) = three 8bpp pieces at x 0/128/256 (two CLUT rows each, `clutY` 256/257); 3: 98 small pieces = electric sparks running along the traces (frame runs 15-23, 24-32, 33-40, 41-49, 50-65, 66-81, 82-97 are animation sequences, position baked in); 4: 48 lightning pieces |
| `TLOGOUSA` | cells 0, texture 1 (8bpp page + 16-bit page) | frame 0x00 "DIGIMON Digital Monsters" 256x92, 0x01 "DIGIMON WORLD" 248x32, 0x02 "3" 56x56, 0x03 additive glow 300x136 (origin 9,-6), 0x04 80x72 (unknown, not drawn), 0x05 additive glow 224x43, 0x06 copyright 176x34 (origin 43,-12), 0x07 164x18 selection frame, 0x08/0x09/0x0A = empty plate (part 11, 164x16) + "Press Start Button" / "Start" / "Continue" |
| `NE_DATA_` (Digimon Online registration, overlay data of the sign-up) | cells 0, texture 1 | 0x01 320x58 terminal window with side wires (box x 72..247), 0x07 288x182 starter-pack panel (A/B/C tabs + three pink diamonds), 0x0C-0x0E 272x29 "Choose a Starter Pack" header with steps 1-2-3 (all three light step 1: the step light is drawn by code), 0x11-0x29 animated 32x32 portraits: Guilmon 0x11-13, Agumon 0x14-17, Kotemon 0x18-1B, Monmon 0x1C-1E, Kumamon 0x1F-21, Renamon 0x22-25, Patamon 0x26-29 (no Veemon), 0x38 190x75 "DIGIMON ONLINE" logo (origin -9,-52; texture is grey, the original shows it light blue), 0x36 321x92 red bars, 0x2A-0x35 small buttons |

On-screen positions used by `src/game/screen_style.h` were measured on the original (reference screenshots), not
decoded from the overlay code. `TLOGOUSA` matches the original title screen (logo at 21,20, plates at 78,151 + 21 per
entry). **Open:** what piece 0x04 of `TLOGOUSA` is; spark timing; the exact title background brightness (the original
looks darker than the raw texture, we tint it 60 %).

`DAT/SCREEN/CMNBGDCS` + `CMNBGDTM` (CS/TM pair, 3 pieces 540x532 / 580x477 / 516x452 at origins -16,-258 / -28,-206 /
-16,-192): the three layers of the **Digimon Online hex background** (diagonal bands of hexagons with the Digimon
Online emblem). We drift them slowly (parallax); the original motion is not decoded.

### Registration text `DAT/COUNTRY/USA/USNAMEET.BIN` — verified (31 messages)
`u32 count`, `count × u32` absolute offsets, 0-terminated strings in the USA encoding ("Message text").
1 welcome speech, 3-5 "1" "2" "3", 6 "Choose a Starter Pack", 7-9 "A" "B" "C", 10-12 "Balanced / Powerful / Maniac
Pack", 13 Kotemon, 14 Monmon, 15 Kumamon, 16 Agumon, 17 "Veemon" (name only), 18 Guilmon, 19 Renamon, 20 Patamon
(one 2-line description each), 21 "Confirmation Screen", 22 "Register account?", 23 "YES", 24 "NO",
26-28 pack summaries, 29 "NOW ENTERING...", 30 "Registered". `USNAMEDT.BIN` (same layout) = name entry: "Enter Name",
the letter grids, default name, "That name is not allowed.". `data/starter_sets.cfg` refers to these as `@N`.

### Other sheets (decoded, not used yet)
`STDGLB` (Digimon Lab: portraits, DV-tree arrows; group 2 = 8bpp), `STSTAT` (status screen), `STALBM`, `STCDSP`,
`STSHOP`, `STTITL` (save-file icon titles per country, not the title screen), `STWMP0/1` (world map), `NAMEDT` (name
entry letter grid + chibi portraits), `STDGNM`, `STNMET` (same art as `NE_DATA_`), `STMCRD`,
`STTRNG` / `JTRGCAST` (training), `JTWNDCST` (826 parts: Japanese kanji font + window, 5 pages).
`DAT/MODEL/FGTEFTIM.BIN` is a one-page pack of battle *effect* textures (flames, rays, glows), not HUD.

**Open:** on-screen coordinates of every HUD / report element (FIGHTSTG.PRO / STFGTREP.PRO code), portrait index ↔
Digimon, `PGFGTW00` group 0's extra sub-table entries (`2`, `0xEAC`), the exact battle bar fill colours, whether
the game uses a width table instead of the glyph cell width for proportional spacing.
