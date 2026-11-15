# Digimon World 3 file formats

All integers little-endian. "Verified" = holds for every file of that type (checked by `tools/dw3check`).

## Disc layout
| Path | Contents |
|---|---|
| `DAT/Z_STAGE/S###PACK.BIN` (237) | Pre-rendered field backgrounds — **decoded** |
| `DAT/FIELD/SPRT/P###XXXX.BIN` (179) | Field character sprites + animations — **decoded** |
| `DAT/FIELD/STAGE/S###MASK.BIN`, `S###TMPK.BIN` | Per-stage mask/attribute data, tile animation pack — open |
| `DAT/MODEL/M###XXXX.BIN` | Battle 3D models (name = species id + short name) — open |
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
`u32 offsets[4]` (0-terminated) → RLEN blocks. First block: 8bpp TIM with 11 CLUT rows; next blocks: 16bpp TIMs of 64x256 units whose bytes look like per-cell attribute/priority values (e.g. 0x25, 0x07 runs). Likely collision + depth priority. Needs decoding (owner: ps1-reverse-engineer + graphics-programmer).
