#include "formats/ui_sheet.h"

#include <algorithm>

#include "formats/rlen.h"

namespace dw3 {

namespace {

bool fail(std::string* err, const char* msg) {
    if (err) *err = msg;
    return false;
}

// A page = RLEN-compressed TIM or raw TIM at `off`.
bool isPage(ByteView v, size_t off) {
    if (isRlen(v, off)) return true;
    uint32_t m, f;
    return v.u32(off, m) && v.u32(off + 4, f) && m == 0x10 && (f == 0 || f == 1 || f == 2 || f == 8 || f == 9);
}

bool loadPage(ByteView v, size_t off, UiPage& pg) {
    Bytes tmp;
    ByteView t;
    if (isRlen(v, off)) {
        if (!decodeRlen(v, off, tmp)) return false;
        t = ByteView(tmp);
    } else {
        t = v.sub(off, v.size - off);
    }
    uint32_t magic, flags;
    if (!t.u32(0, magic) || !t.u32(4, flags) || magic != 0x10) return false;
    size_t p = 8;
    pg = {};
    pg.bpp = int(flags & 3);
    if (flags & 8) {
        uint32_t len;
        uint16_t cw, ch;
        if (!t.u32(p, len) || !t.u16(p + 8, cw) || !t.u16(p + 10, ch) || len != 12u + uint32_t(cw) * ch * 2 || !t.has(p, len))
            return false;
        pg.clutW = cw;
        pg.clutRows = ch;
        pg.clut.resize(size_t(cw) * ch);
        for (size_t k = 0; k < pg.clut.size(); ++k) t.u16(p + 12 + k * 2, pg.clut[k]);
        p += len;
    }
    uint32_t len;
    uint16_t w, h;
    if (!t.u32(p, len) || !t.u16(p + 8, w) || !t.u16(p + 10, h) || len != 12u + uint32_t(w) * h * 2 || !t.has(p, len))
        return false;
    pg.wWords = w;
    pg.h = h;
    pg.words.resize(size_t(w) * h);
    for (size_t k = 0; k < pg.words.size(); ++k) t.u16(p + 12 + k * 2, pg.words[k]);
    return true;
}

// u32 table at `base`, n = first / 4 entries (first entry = table size). Offsets relative to `base`.
bool readTable(ByteView v, size_t base, std::vector<uint32_t>& out) {
    uint32_t first;
    if (!v.u32(base, first) || first < 4 || (first & 3) || first > 0x10000 || !v.has(base, first)) return false;
    out.resize(first / 4);
    for (size_t i = 0; i < out.size(); ++i) v.u32(base + i * 4, out[i]);
    return true;
}

}  // namespace

int UiCellGroup::pieceForFrame(int frameId) const {
    for (size_t i = 0; i < frameIds.size() && i < pieces.size(); ++i)
        if (frameIds[i] == frameId) return int(i);
    return -1;
}

bool UiTexGroup::isVramPages() const {
    if (pages.empty()) return false;
    for (const UiPage& p : pages)
        if (p.wWords != 64 || p.h != 256) return false;
    return true;
}

const UiTexGroup* uiTexGroupFor(const std::vector<UiTexGroup>& tex, size_t cellGroup) {
    for (size_t k = 0; k < tex.size(); ++k) {
        const size_t i = (cellGroup + k) % tex.size();
        if (tex[i].isVramPages()) return &tex[i];
    }
    return nullptr;
}

uint16_t UiTexGroup::word(int wx, int y) const {
    if (wx < 0 || y < 0) return 0;
    int x = wx;
    for (const UiPage& p : pages) {
        if (x < p.wWords) return y < p.h ? p.words[size_t(y) * p.wWords + x] : 0;
        x -= p.wWords;
    }
    return 0;
}

namespace {

bool parseCellGroup(ByteView v, size_t base, UiCellGroup& g, std::string* err) {
    std::vector<uint32_t> off;
    if (!readTable(v, base, off) || off.size() < 2) return fail(err, "bad cell group table");
    for (size_t i = 1; i < off.size(); ++i)
        if (off[i] < off[i - 1] || !v.has(base + off[i], 1)) return fail(err, "cell group offsets out of order");
    // parts
    const size_t pEnd = base + off[1];
    for (size_t p = base + off[0]; p + 14 <= pEnd; p += 14) {
        UiPart part;
        v.u16(p, part.x); v.u16(p + 2, part.y); v.u16(p + 4, part.w); v.u16(p + 6, part.h);
        v.u16(p + 8, part.clutX); v.u16(p + 10, part.clutY); v.u16(p + 12, part.kind);
        g.parts.push_back(part);
    }
    // frame ids
    for (size_t p = base + off[1];; ++p) {
        if (!v.has(p, 1)) return fail(err, "unterminated frame id list");
        if (v.data[p] == 0xFF) break;
        g.frameIds.push_back(v.data[p]);
    }
    // pieces
    for (size_t k = 2; k < off.size(); ++k) {
        const size_t p = base + off[k];
        uint16_t cnt, zero, flag;
        if (!v.u16(p, cnt) || !v.u16(p + 2, zero) || !v.u16(p + 4, flag)) return fail(err, "piece header out of range");
        if (!v.has(p + 6, size_t(cnt) * 6)) return fail(err, "piece layers out of range");
        UiPiece pc;
        pc.flag = int16_t(flag);
        for (uint16_t j = 0; j < cnt; ++j) {
            UiLayer l;
            uint16_t dx, dy;
            v.u16(p + 6 + j * 6, l.part); v.u16(p + 8 + j * 6, dx); v.u16(p + 10 + j * 6, dy);
            l.dx = int16_t(dx); l.dy = int16_t(dy);
            if (l.part >= g.parts.size()) return fail(err, "piece references a missing part");
            pc.layers.push_back(l);
        }
        g.pieces.push_back(std::move(pc));
    }
    return true;
}

// Texture group at `base`: a single page or a 0-terminated sub-table of pages. `strict`: every sub-table
// entry must be a page (used to tell texture groups from cell groups inside a combined pack).
bool parseTexGroup(ByteView v, size_t base, UiTexGroup& g, bool strict, std::string* err) {
    if (isPage(v, base)) {
        UiPage pg;
        if (!loadPage(v, base, pg)) return fail(err, "bad page");
        g.pages.push_back(std::move(pg));
        return true;
    }
    std::vector<uint32_t> sub;
    if (!readTable(v, base, sub)) return fail(err, "bad texture group table");
    for (uint32_t e : sub) {
        if (e == 0) break;
        // Non-page entries exist (PGFGTW00 group 0 carries a raw 16-bit cursor strip): skipped.
        if (!isPage(v, base + e)) {
            if (strict) return fail(err, "not a texture group");
            continue;
        }
        UiPage pg;
        if (!loadPage(v, base + e, pg)) return fail(err, "bad page in group");
        g.pages.push_back(std::move(pg));
    }
    return !strict || !g.pages.empty() || fail(err, "empty texture group");
}

}  // namespace

bool loadCellSheet(ByteView v, std::vector<UiCellGroup>& out, std::string* err) {
    out.clear();
    std::vector<uint32_t> top;
    if (!readTable(v, 0, top)) return fail(err, "bad cell sheet table");
    for (size_t gi = 0; gi < top.size(); ++gi) {
        UiCellGroup g;
        const size_t base = top[gi];
        if (base == 0 && gi > 0) { out.push_back(std::move(g)); continue; }
        if (!parseCellGroup(v, base, g, err)) return false;
        out.push_back(std::move(g));
    }
    return !out.empty() || fail(err, "no cell groups");
}

bool loadTexturePack(ByteView v, std::vector<UiTexGroup>& out, std::string* err) {
    out.clear();
    std::vector<uint32_t> top;
    if (!readTable(v, 0, top)) return fail(err, "bad texture pack table");
    for (size_t gi = 0; gi < top.size(); ++gi) {
        UiTexGroup g;
        const size_t base = top[gi];
        if (base == 0 && gi > 0) { out.push_back(std::move(g)); continue; }
        if (!parseTexGroup(v, base, g, false, err)) return false;
        out.push_back(std::move(g));
    }
    return !out.empty() || fail(err, "no texture groups");
}

bool loadUiPack(ByteView v, UiPack& out, std::string* err) {
    out = {};
    std::vector<uint32_t> top;
    if (!readTable(v, 0, top)) return fail(err, "bad pack table");
    std::vector<size_t> waiting;  // cell groups not yet paired with a texture group
    size_t paired = 0;            // how many of `waiting` the current texture run already paired
    bool inTexRun = false;
    for (size_t gi = 0; gi < top.size(); ++gi) {
        if (top[gi] == 0) continue;
        UiTexGroup tg;
        std::string terr;
        if (parseTexGroup(v, top[gi], tg, true, &terr)) {
            if (!inTexRun) paired = 0;
            inTexRun = true;
            out.tex.push_back(std::move(tg));
            const int ti = int(out.tex.size()) - 1;
            if (paired < waiting.size()) out.texFor[waiting[paired++]] = ti;
            continue;
        }
        if (inTexRun) {  // a new cell run starts: the previous one pairs leftovers with its last texture
            for (size_t k = paired; k < waiting.size(); ++k) out.texFor[waiting[k]] = int(out.tex.size()) - 1;
            waiting.clear();
            inTexRun = false;
        }
        UiCellGroup cg;
        if (!parseCellGroup(v, top[gi], cg, err)) return false;
        out.cells.push_back(std::move(cg));
        out.texFor.push_back(-1);
        waiting.push_back(out.cells.size() - 1);
    }
    if (inTexRun)
        for (size_t k = paired; k < waiting.size(); ++k) out.texFor[waiting[k]] = int(out.tex.size()) - 1;
    if (out.cells.empty() || out.tex.empty()) return fail(err, "pack has no cells or no textures");
    for (int t : out.texFor)
        if (t < 0) return fail(err, "cell group without a texture group");
    return true;
}

uint32_t ps1Color(uint16_t c) {
    if (c == 0) return 0;
    const uint8_t r = uint8_t((c & 31) * 255 / 31), g = uint8_t(((c >> 5) & 31) * 255 / 31),
                  b = uint8_t(((c >> 10) & 31) * 255 / 31);
    return rgba(r, g, b, 255);
}

bool renderUiPart(const UiTexGroup& tex, const UiPart& p, Image& out) {
    out.resize(p.w, p.h);
    if (tex.pages.empty()) return false;
    int totalWords = 0;
    for (auto& pg : tex.pages) totalWords += pg.wWords;
    if (p.kind == 0) {
        if ((p.x + p.w + 3) / 4 > totalWords || p.y + p.h > 256) return false;
        uint16_t pal[16];
        for (int i = 0; i < 16; ++i) pal[i] = tex.word(p.clutX + i, p.clutY);
        for (int y = 0; y < p.h; ++y)
            for (int x = 0; x < p.w; ++x) {
                const int tx = p.x + x;
                const uint16_t w = tex.word(tx >> 2, p.y + y);
                out.px[size_t(y) * p.w + x] = ps1Color(pal[(w >> ((tx & 3) * 4)) & 15]);
            }
        return true;
    }
    if (p.kind == 1) {
        if ((p.x + p.w + 1) / 2 > totalWords || p.y + p.h > 256) return false;
        const UiPage* timClut = nullptr;
        for (auto& pg : tex.pages)
            if (pg.clutRows > 0) { timClut = &pg; break; }
        uint16_t pal[256];
        for (int i = 0; i < 256; ++i) {
            if (p.clutY >= 256 && timClut) {
                const int row = p.clutY - 256;
                pal[i] = (row < timClut->clutRows && i < timClut->clutW) ? timClut->clut[size_t(row) * timClut->clutW + i] : 0;
            } else {
                pal[i] = tex.word(p.clutX + i, p.clutY);
            }
        }
        for (int y = 0; y < p.h; ++y)
            for (int x = 0; x < p.w; ++x) {
                const int tx = p.x + x;
                const uint16_t w = tex.word(tx >> 1, p.y + y);
                out.px[size_t(y) * p.w + x] = ps1Color(pal[(w >> ((tx & 1) * 8)) & 255]);
            }
        return true;
    }
    return false;
}

bool renderUiPiece(const UiTexGroup& tex, const UiCellGroup& cells, int piece, Image& out, int& ox, int& oy) {
    if (piece < 0 || piece >= int(cells.pieces.size())) return false;
    const UiPiece& pc = cells.pieces[size_t(piece)];
    if (pc.layers.empty()) { out.resize(0, 0); ox = oy = 0; return true; }
    int x0 = 1 << 30, y0 = 1 << 30, x1 = -(1 << 30), y1 = -(1 << 30);
    for (auto& l : pc.layers) {
        const UiPart& p = cells.parts[l.part];
        x0 = std::min(x0, int(l.dx)); y0 = std::min(y0, int(l.dy));
        x1 = std::max(x1, l.dx + int(p.w)); y1 = std::max(y1, l.dy + int(p.h));
    }
    out.resize(x1 - x0, y1 - y0);
    ox = x0; oy = y0;
    bool ok = true;
    Image part;
    for (auto& l : pc.layers) {  // later layers draw on top
        ok &= renderUiPart(tex, cells.parts[l.part], part);
        for (int y = 0; y < part.h; ++y)
            for (int x = 0; x < part.w; ++x) {
                const uint32_t c = part.px[size_t(y) * part.w + x];
                if (c >> 24) out.px[size_t(l.dy - y0 + y) * out.w + (l.dx - x0 + x)] = c;
            }
    }
    return ok;
}

// ---- font ------------------------------------------------------------------------------------------------

int uiGlyphPartForCode(UiFont font, uint8_t code, bool symbol) {
    if (symbol) {
        switch (font) {
            case UiFont::White: return code >= 1 && code <= 0x24 ? 80 + code : -1;
            case UiFont::Orange:
                switch (code) {
                    case 0x01: return 420; case 0x04: return 421; case 0x05: return 422; case 0x09: return 423;
                    case 0x0a: return 424; case 0x0e: case 0x1a: return 425; case 0x11: return 427;
                    case 0x13: return 428; case 0x17: return 429; case 0x18: return 430;
                    default: return -1;
                }
            case UiFont::Small:
                switch (code) {
                    case 0x07: return 713; case 0x0e: case 0x1a: return 714; case 0x0f: return 715;
                    default: return -1;
                }
        }
        return -1;
    }
    switch (code) {  // single-byte punctuation
        case 0xe5: return uiGlyphPartForCode(font, 0x0f, true);
        case 0xe6: return uiGlyphPartForCode(font, 0x09, true);
        case 0xe7: return uiGlyphPartForCode(font, 0x0a, true);
        case 0xe8: return uiGlyphPartForCode(font, 0x1a, true);
        default: break;
    }
    if (code < 4) return -1;
    switch (font) {
        case UiFont::White: return code <= 0xe4 ? code + 135 : -1;
        case UiFont::Orange: return code <= 0xe4 && code + 428 <= 711 ? code + 428 : -1;
        case UiFont::Small: {
            if (code <= 0x0d) return 717 + (code - 4);
            static const char kLetters[] = "BEHILMPRTVX";
            const char c = (code >= 0x0e && code <= 0x27) ? char('A' + code - 0x0e) : (code >= 0x28 && code <= 0x41) ? char('A' + code - 0x28) : 0;
            for (int i = 0; kLetters[i]; ++i)
                if (kLetters[i] == c) return 727 + i;
            return -1;
        }
    }
    return -1;
}

int uiGlyphPart(UiFont font, char c) {
    if (c >= '0' && c <= '9') return uiGlyphPartForCode(font, uint8_t(4 + c - '0'), false);
    if (c >= 'A' && c <= 'Z') return uiGlyphPartForCode(font, uint8_t(0x0e + c - 'A'), false);
    if (c >= 'a' && c <= 'z') return uiGlyphPartForCode(font, uint8_t(0x28 + c - 'a'), false);
    uint8_t sym = 0;
    switch (c) {
        case ' ': sym = 0x01; break;  case ',': sym = 0x04; break;  case '.': sym = 0x05; break;
        case ':': sym = 0x07; break;  case ';': sym = 0x08; break;  case '?': sym = 0x09; break;
        case '!': sym = 0x0a; break;  case '^': sym = 0x0b; break;  case '_': sym = 0x0c; break;
        case '-': sym = 0x1a; break;  case '/': sym = 0x0f; break;  case '\\': sym = 0x10; break;
        case '~': sym = 0x11; break;  case '\'': sym = 0x13; break; case '"': sym = 0x14; break;
        case '(': sym = 0x15; break;  case ')': sym = 0x16; break;  case '[': sym = 0x17; break;
        case ']': sym = 0x18; break;  case '+': sym = 0x19; break;  case '=': sym = 0x1c; break;
        case '<': sym = 0x1d; break;  case '>': sym = 0x1e; break;
        default: return -1;
    }
    return uiGlyphPartForCode(font, sym, true);
}

}  // namespace dw3
