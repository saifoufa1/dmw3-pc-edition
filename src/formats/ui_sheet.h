// UI cell sheets (*CS.BIN / *CST.BIN / PGFGTC00) and their texture packs (*TM.BIN / *TIM.BIN / PGFGTW00).
// See docs/FORMATS.md "UI cell sheet" and "UI texture pack". Pure parser, no platform code.
//
//  Texture pack: u32 table[n] (n = table[0] / 4, relative to the table start). An entry points either straight
//  at a page (RLEN-compressed or raw TIM) or at a sub-table of the same shape whose entries are pages
//  (0 = end / empty). Each sub-table is one "group" = pages laid side by side in VRAM, 64 VRAM words each.
//  Cell sheet: u32 table[n] (same shape), one cell group per entry. Group = u32 off[m] (relative to the
//  group, m = off[0] / 4): off[0] = parts (14 bytes each), off[1] = frame ids (u8, 0xFF-terminated),
//  off[2..] = pieces (one per frame id): u16 count, u16 0, i16 flag, count x {u16 part, i16 dx, i16 dy}.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "core/bytes.h"
#include "formats/image.h"

namespace dw3 {

// One textured rectangle. x is in texels of its depth (kind 0: 4bpp, kind 1: 8bpp) counted across the
// group's pages (4bpp page = 256 texels wide, 8bpp page = 128). CLUT = VRAM word (clutX, clutY) inside the
// group's 16-bit pages; for kind 1 clutY >= 256 selects row clutY - 256 of the group's 8bpp TIM CLUT.
struct UiPart {
    uint16_t x = 0, y = 0, w = 0, h = 0;
    uint16_t clutX = 0, clutY = 0;
    uint16_t kind = 0;  // 0 = 4bpp, 1 = 8bpp
};

struct UiLayer {
    uint16_t part = 0;
    int16_t dx = 0, dy = 0;
};

struct UiPiece {
    int16_t flag = -1;  // -1 = opaque; 1 = semi-transparent; 2/3 = additive (glow/flash) — medium confidence
    std::vector<UiLayer> layers;
};

struct UiCellGroup {
    std::vector<UiPart> parts;
    std::vector<uint8_t> frameIds;  // ascending, frameIds[i] = id of pieces[i]
    std::vector<UiPiece> pieces;
    bool empty() const { return parts.empty(); }
    int pieceForFrame(int frameId) const;  // -1 if none
};

// One VRAM page: 64 x 256 16-bit words. bpp/clut come from the TIM header (8bpp TIMs carry their own CLUT).
struct UiPage {
    int bpp = 2;                 // TIM depth code: 0 = 4bpp, 1 = 8bpp, 2 = 16bpp (raw VRAM page)
    int wWords = 0, h = 0;
    std::vector<uint16_t> words;  // wWords * h
    int clutW = 0, clutRows = 0;
    std::vector<uint16_t> clut;   // clutW * clutRows (8bpp TIM pages only)
};

struct UiTexGroup {
    std::vector<UiPage> pages;
    bool empty() const { return pages.empty(); }
    // True when every page is a 64-word VRAM texture page (a group can also hold a plain TIM, e.g. the
    // 156x12 cursor strip that is group 0 of PGFGTW00).
    bool isVramPages() const;
    uint16_t word(int wx, int y) const;  // 0 when outside
};

bool loadCellSheet(ByteView v, std::vector<UiCellGroup>& out, std::string* err = nullptr);
bool loadTexturePack(ByteView v, std::vector<UiTexGroup>& out, std::string* err = nullptr);
// Texture group used by cell group `cellGroup`: the same index when it holds VRAM pages, else the next group
// that does (wrapping around). nullptr if none.
// Combined pack (DAT/SCREEN/TTCOMMPK, TLOGOUSA, TLOGOJPN): ONE file whose top table mixes cell groups and texture
// groups. A run of N cell groups is followed by a run of texture groups; the k-th cell group of the run uses the
// k-th texture group (leftover cells use the run's last one). See docs/FORMATS.md "Title screen packs".
struct UiPack {
    std::vector<UiCellGroup> cells;
    std::vector<int> texFor;  // texture group index per cell group
    std::vector<UiTexGroup> tex;
};
bool loadUiPack(ByteView v, UiPack& out, std::string* err = nullptr);

const UiTexGroup* uiTexGroupFor(const std::vector<UiTexGroup>& tex, size_t cellGroup);

// PS1 BGR555 -> RGBA, 0x0000 = transparent.
uint32_t ps1Color(uint16_t c);

// Renders one part (colour 0 transparent). Returns false if the part lies outside the pages.
bool renderUiPart(const UiTexGroup& tex, const UiPart& p, Image& out);
// Renders a whole piece; the image's top-left corresponds to piece origin + (originX, originY).
bool renderUiPiece(const UiTexGroup& tex, const UiCellGroup& cells, int piece, Image& out, int& originX, int& originY);

// ---- DW3 text encoding (DAT/COUNTRY/USA message banks) <-> CMFONT glyph parts ----------------------------
// Byte codes: 00 end; 01 xx symbol (glyph part 80 + xx, 01 01 = space); 02 xx control (02 01 newline,
// 02 05 n = argument n, 02 07 speaker tag, 02 02 wait); 03 next page; 04-0D '0'-'9'; 0E-27 'A'-'Z';
// 28-41 'a'-'z'; 42-E4 kana (glyph part = code + 135); E5 '/', E6 '?', E7 '!', E8 '-'.
enum class UiFont { White = 0, Orange = 1, Small = 2 };
// CMFONT glyph part for an ASCII character, -1 if the font has none.
int uiGlyphPart(UiFont font, char c);
// CMFONT glyph part for a single encoded DW3 code (code >= 4) or a 01-prefixed symbol (symbol = true).
int uiGlyphPartForCode(UiFont font, uint8_t code, bool symbol);

}  // namespace dw3
