#include "formats/field_stage.h"

#include <algorithm>

#include "formats/rlen.h"

namespace dw3 {

namespace {

uint32_t ps1ToRgba(uint16_t c) {
    if (c == 0) return 0;
    const uint8_t r = uint8_t((c & 31) << 3), g = uint8_t(((c >> 5) & 31) << 3), b = uint8_t(((c >> 10) & 31) << 3);
    return rgba(r, g, b, 255);
}

// ---- MASK: u32 offsets[] (0-terminated) -> RLEN (or raw) TIM blocks. Block 0 carries the CLUT.
bool loadMask(ByteView v, FieldStage& fs) {
    std::vector<Bytes> blocks;
    for (size_t i = 0; i < 16; ++i) {
        uint32_t off;
        if (!v.u32(i * 4, off) || off == 0) break;
        Bytes b;
        if (isRlen(v, off)) {
            if (!decodeRlen(v, off, b)) return false;
        } else {
            ByteView s = v.sub(off, v.size - off);
            b.assign(s.data, s.data + s.size);
        }
        blocks.push_back(std::move(b));
    }
    if (blocks.empty()) return false;
    struct Blk { const uint8_t* px; int wBytes, h; };
    std::vector<Blk> img;
    for (size_t bi = 0; bi < blocks.size(); ++bi) {
        ByteView t(blocks[bi]);
        uint32_t magic, flags;
        if (!t.u32(0, magic) || !t.u32(4, flags) || magic != 0x10) return false;
        size_t p = 8;
        if (flags & 8) {
            uint32_t len; uint16_t cw, ch;
            if (!t.u32(p, len) || !t.u16(p + 8, cw) || !t.u16(p + 10, ch) || len != 12u + uint32_t(cw) * ch * 2 || !t.has(p, len))
                return false;
            if (bi == 0 && cw == 256) {
                fs.clutRows = ch;
                fs.clut.resize(size_t(cw) * ch);
                for (size_t k = 0; k < fs.clut.size(); ++k) t.u16(p + 12 + k * 2, fs.clut[k]);
            }
            p += len;
        }
        uint32_t len; uint16_t w, h;
        if (!t.u32(p, len) || !t.u16(p + 8, w) || !t.u16(p + 10, h) || len != 12u + uint32_t(w) * h * 2 || !t.has(p, len))
            return false;
        img.push_back({t.data + p + 12, int(w) * 2, int(h)});
    }
    if (fs.clutRows == 0) return false;
    fs.atlasW = 0;
    for (auto& b : img) fs.atlasW += b.wBytes;
    fs.atlas.assign(size_t(fs.atlasW) * 256, 0);
    int x0 = 0;
    for (auto& b : img) {
        for (int y = 0; y < std::min(b.h, 256); ++y)
            std::copy(b.px + size_t(y) * b.wBytes, b.px + size_t(y + 1) * b.wBytes, fs.atlas.begin() + size_t(y) * fs.atlasW + x0);
        x0 += b.wBytes;
    }
    return true;
}

// ---- TMPK quadtree (walk / trigger map). Header: u32 off[6] (relative to section), u32 gridH<<8|gridW.
// off[0]: u8 gw, u8 gh, u8 node[gw*gh] (index into level 1, 0 = empty 128x128 cell)
// off[1]: level-1 nodes, 4 x u8 children (2x2: TL, TR, BL, BR), each covering 64 px
// off[2..4]: level 2-4 nodes, 4 x u16 children (32 / 16 / 8 px)
// off[5]: leaves, 8x8 bytes (one value per pixel). Index 0 everywhere means "all zero".
bool isQuadtree(ByteView v, size_t s, int gw, int gh) {
    uint32_t o[7];
    for (int i = 0; i < 7; ++i)
        if (!v.u32(s + i * 4, o[i])) return false;
    if (o[0] != 0x18 || o[6] != uint32_t((gh << 8) | gw)) return false;
    for (int i = 0; i < 5; ++i)
        if (o[i + 1] < o[i]) return false;
    return v.has(s + o[0], 2) && v.data[s + o[0]] == gw && v.data[s + o[0] + 1] == gh;
}

bool decodeQuadtree(ByteView v, size_t s, int gw, int gh, std::vector<uint8_t>& out, uint8_t& maxVal) {
    uint32_t o[6];
    for (int i = 0; i < 6; ++i) v.u32(s + i * 4, o[i]);
    const int W = gw * 128, H = gh * 128;
    out.assign(size_t(W) * H, 0);
    maxVal = 0;
    bool ok = true;
    struct Rec {
        static void node(ByteView v, size_t s, const uint32_t* o, int level, uint32_t idx, int x, int y, int size,
                         std::vector<uint8_t>& out, int W, uint8_t& mx, bool& ok) {
            if (idx == 0 || !ok) return;
            if (level == 5) {
                const size_t p = s + o[5] + size_t(idx) * 64;
                if (!v.has(p, 64)) { ok = false; return; }
                for (int yy = 0; yy < 8; ++yy)
                    for (int xx = 0; xx < 8; ++xx) {
                        uint8_t b = v.data[p + yy * 8 + xx];
                        out[size_t(y + yy) * W + x + xx] = b;
                        mx = std::max(mx, b);
                    }
                return;
            }
            uint32_t ch[4];
            if (level == 1) {
                const size_t p = s + o[1] + size_t(idx) * 4;
                if (!v.has(p, 4)) { ok = false; return; }
                for (int k = 0; k < 4; ++k) ch[k] = v.data[p + k];
            } else {
                const size_t p = s + o[level] + size_t(idx) * 8;
                uint16_t c;
                for (int k = 0; k < 4; ++k) {
                    if (!v.u16(p + k * 2, c)) { ok = false; return; }
                    ch[k] = c;
                }
            }
            const int h = size / 2;
            for (int k = 0; k < 4; ++k) node(v, s, o, level + 1, ch[k], x + (k & 1) * h, y + (k >> 1) * h, h, out, W, mx, ok);
        }
    };
    for (int cy = 0; cy < gh; ++cy)
        for (int cx = 0; cx < gw; ++cx)
            Rec::node(v, s, o, 1, v.data[s + o[0] + 2 + cy * gw + cx], cx * 128, cy * 128, 128, out, W, maxVal, ok);
    (void)H;
    return ok;
}

// ---- TMPK parts/pieces section: u32 off[n] (relative). Entry 0 = parts (14 bytes each),
// entry 1 = ascending frame ids (u8, 0xFF-terminated, one per piece), entries 2.. = pieces.
bool tryParts(ByteView v, size_t s, FieldStage& fs) {
    uint32_t first;
    if (!v.u32(s, first) || first < 12 || (first & 3) || first > 0x1000) return false;
    const size_t n = first / 4;
    std::vector<uint32_t> off(n);
    for (size_t i = 0; i < n; ++i)
        if (!v.u32(s + i * 4, off[i]) || (i && off[i] < off[i - 1]) || !v.has(s + off[i], 6)) return false;
    if ((off[1] - off[0]) % 14 > 3) return false;  // table is padded to 4 bytes
    std::vector<uint8_t> ids;
    for (size_t p = s + off[1]; v.has(p, 1) && v.data[p] != 0xFF; ++p) {
        if (!ids.empty() && v.data[p] <= ids.back()) return false;
        ids.push_back(v.data[p]);
    }
    if (ids.size() != n - 2) return false;
    std::vector<FieldPart> parts;
    for (size_t p = s + off[0]; p + 14 <= s + off[1]; p += 14) {
        FieldPart fp;
        v.u16(p, fp.x); v.u16(p + 2, fp.y); v.u16(p + 4, fp.w); v.u16(p + 6, fp.h);
        v.u16(p + 8, fp.clut); v.u16(p + 10, fp.rowOrDepth); v.u16(p + 12, fp.kind);
        parts.push_back(fp);
    }
    std::vector<FieldPiece> pieces;
    for (size_t i = 2; i < n; ++i) {
        const size_t p = s + off[i];
        uint16_t cnt, flag;
        if (!v.u16(p, cnt) || !v.u16(p + 4, flag) || cnt > 64) return false;
        FieldPiece pc;
        pc.flag = int16_t(flag);
        for (uint16_t k = 0; k < cnt; ++k) {
            FieldPieceLayer l;
            uint16_t dx, dy;
            if (!v.u16(p + 6 + k * 6, l.part) || !v.u16(p + 8 + k * 6, dx) || !v.u16(p + 10 + k * 6, dy)) return false;
            l.dx = int16_t(dx);
            l.dy = int16_t(dy);
            if (l.part >= parts.size()) return false;
            pc.layers.push_back(l);
        }
        pieces.push_back(std::move(pc));
    }
    fs.parts = std::move(parts);
    fs.pieces = std::move(pieces);
    fs.frameIds = std::move(ids);
    return true;
}

}  // namespace

int FieldStage::pieceForFrame(int frameId) const {
    auto it = std::lower_bound(frameIds.begin(), frameIds.end(), uint8_t(frameId));
    if (frameId < 0 || frameId > 255 || it == frameIds.end() || *it != frameId) return -1;
    return int(it - frameIds.begin());
}

bool FieldStage::renderPart(int pi, Image& out) const {
    if (pi < 0 || size_t(pi) >= parts.size() || atlas.empty()) return false;
    const FieldPart& p = parts[size_t(pi)];
    out.resize(p.w, p.h);
    for (int y = 0; y < p.h; ++y)
        for (int x = 0; x < p.w; ++x) {
            const int ay = p.y + y;
            if (ay >= 256) continue;
            uint16_t c = 0;
            if (p.kind == 1) {
                const int ax = p.x + x, row = int(p.rowOrDepth) - 256;
                if (ax >= atlasW || row < 0 || row >= clutRows) continue;
                c = clut[size_t(row) * 256 + atlas[size_t(ay) * atlasW + ax]];
            } else {
                // 4bpp glow mask (intensity 1..15, low nibble first). Its palette is not in the MASK CLUT
                // (open: probably a global VRAM CLUT selected by `clut`), so use a soft warm-white ramp.
                const int tx = p.x + x, ax = tx / 2;
                if (ax >= atlasW) continue;
                const uint8_t b = atlas[size_t(ay) * atlasW + ax];
                const int nib = (tx & 1) ? (b >> 4) : (b & 15);
                if (nib) {
                    const int r = nib * 2, g = nib * 2, bl = nib * 3 / 2;  // 5-bit channels, max 30/30/22
                    c = uint16_t(r | (g << 5) | (bl << 10));
                }
            }
            out.px[size_t(y) * p.w + x] = ps1ToRgba(c);
        }
    return true;
}

bool FieldStage::renderPiece(int pi, Image& out, int& originX, int& originY) const {
    if (pi < 0 || size_t(pi) >= pieces.size() || pieces[size_t(pi)].layers.empty()) return false;
    const FieldPiece& pc = pieces[size_t(pi)];
    int minX = 1 << 30, maxX = -(1 << 30), minY = 1 << 30, maxY = -(1 << 30);
    for (const auto& l : pc.layers) {
        const FieldPart& p = parts[l.part];
        minX = std::min(minX, int(l.dx));
        maxX = std::max(maxX, l.dx + int(p.w));
        minY = std::min(minY, int(l.dy));
        maxY = std::max(maxY, l.dy + int(p.h));
    }
    if (maxX <= minX || maxY <= minY || maxX - minX > 1024 || maxY - minY > 1024) return false;
    out.resize(maxX - minX, maxY - minY);
    originX = minX;
    originY = minY;
    Image part;
    for (const auto& l : pc.layers) {
        if (!renderPart(l.part, part)) continue;
        for (int y = 0; y < part.h; ++y)
            for (int x = 0; x < part.w; ++x) {
                const uint32_t c = part.px[size_t(y) * part.w + x];
                if (c >> 24) out.px[size_t(y + l.dy - minY) * out.w + (x + l.dx - minX)] = c;
            }
    }
    return true;
}

bool loadFieldStage(ByteView mask, ByteView tmpk, int gw, int gh, FieldStage& fs, std::string* err) {
    fs = {};
    fs.w = gw * 128;
    fs.h = gh * 128;
    bool any = false;
    if (mask.size) any |= loadMask(mask, fs);
    if (!any) { fs.atlas.clear(); fs.clut.clear(); fs.atlasW = 0; fs.clutRows = 0; }
    uint32_t first;
    if (tmpk.size && tmpk.u32(0, first) && first >= 4 && (first & 3) == 0 && first <= 64) {
        for (size_t i = 0; i < first / 4; ++i) {
            uint32_t s;
            if (!tmpk.u32(i * 4, s) || !tmpk.has(s, 28)) continue;
            if (isQuadtree(tmpk, s, gw, gh)) {
                std::vector<uint8_t> m;
                uint8_t mx;
                if (!decodeQuadtree(tmpk, s, gw, gh, m, mx)) continue;
                if (mx < 0x20 && !fs.hasWalk) { fs.walk = std::move(m); fs.hasWalk = true; any = true; }
                else if (mx >= 0x20 && !fs.hasTrigger) { fs.trigger = std::move(m); fs.hasTrigger = true; any = true; }
            } else if (fs.pieces.empty() && tryParts(tmpk, s, fs)) {
                any = true;
            }
        }
    }
    if (!any && err) *err = "no MASK/TMPK data decoded";
    return any;
}

}  // namespace dw3
