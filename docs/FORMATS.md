# Digimon World 3 file formats

All integers little-endian. "Verified" = holds for every file of that type (checked by `tools/dw3check`).

## Disc layout
| Path | Contents |
|---|---|
| `DAT/Z_STAGE/S###PACK.BIN` (237) | Pre-rendered field backgrounds — **decoded** |
| `DAT/FIELD/SPRT/P###XXXX.BIN` (179) | Field character sprites + animations — **decoded** |
| `DAT/FIELD/STAGE/S###MASK.BIN`, `S###TMPK.BIN` | Per-stage mask/attribute data, tile animation pack — open |
| `DAT/MODEL/M###XXXX.BIN` | Battle 3D models (name = species id + short name; e.g. M003AGUM Agumon, M005GREY Greymon, M012MTGR MetalGreymon, M213WGRE WarGreymon). **Decoded** — see "Battle model" below (`src/formats/model.*`, 241/246 files load) |
| `DAT/EFFECT/*` | Battle effects — open |
| `DAT/SOUND/MP*.BIN`, `MV*.BIN` | Music/SFX (likely sequence + sample bank pairs) — open |
| `DAT/CARD`, `TEXT`, `SCREEN`, `*CS.BIN`/`*TM.BIN` pairs | UI graphics (TM = textures?, CS = cast/layout?) — open |
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
**Open:** whether layer B should be drawn above actors; black holes in some maps are filled by `FIELD/STAGE/S###TMPK.BIN` (animated tiles).

## FIELD/STAGE/S###MASK.BIN — partial
`u32 offsets[4]` (0-terminated) → RLEN blocks, each a TIM: block 0 = 8bpp 128x256 with 11 CLUT rows, blocks 1-2 = 128x256 8bpp-in-16bpp texture pages (use block 0's CLUTs).
Rendered, they are **texture atlases of scene pieces** (not collision): most likely the foreground/occluder pieces and animated objects drawn over actors.

## FIELD/STAGE/S###TMPK.BIN — partial
`u32 sectionOffsets[3]`. Section 0: offset table → small records `{u16 count, u16 0, i16 flag, u16 frameIds...}` = sprite-part / tile-animation scripts that reference the MASK atlases (fills the black holes in Z_STAGE maps). Sections 1-2: animation timing tables (contain 1802 marker).

## Collision / exits / NPCs — not in data files
Not found in DAT. `PRO/WSTAG###.PRO` are raw MIPS overlays (start with code, e.g. `addiu sp,sp,-32`) and contain coordinate-like tables (e.g. WSTAG200 @0x2BA: pairs such as 1048,212 / 1096,286). Original collision/exits are likely compiled into these overlays.
**Decision:** the PC edition uses its own `data/stages/*.stage` files (see README); decoding the overlays stays a research task.


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
