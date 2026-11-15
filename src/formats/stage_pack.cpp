#include "formats/stage_pack.h"

#include "formats/rlen.h"
#include "formats/tim.h"

namespace dw3 {

namespace {
struct Rect { uint16_t dx, dy, sx, sy, w, h; };

bool readRects(ByteView c, size_t& p, std::vector<Rect>& out) {
    uint32_t n;
    if (!c.u32(p, n) || n > 256) return false;
    p += 4;
    for (uint32_t i = 0; i < n; ++i, p += 12) {
        Rect r;
        if (!c.u16(p, r.dx) || !c.u16(p + 2, r.dy) || !c.u16(p + 4, r.sx) || !c.u16(p + 6, r.sy) ||
            !c.u16(p + 8, r.w) || !c.u16(p + 10, r.h))
            return false;
        out.push_back(r);
    }
    return true;
}

// Returns 1 = drawn, 0 = empty chunk, -1 = failed.
int decodeChunk(ByteView chunk, Image& map, int cellX, int cellY) {
    uint32_t hdr;
    if (!chunk.u32(0, hdr)) return -1;
    if (hdr == 0) return 0;
    // Layout: u32 timOffset, u32 n, n*Rect (base layer), u32 m, m*Rect (upper layer), pad, TIM.
    std::vector<Rect> rects;
    size_t p = 4;
    if (!readRects(chunk, p, rects) || !readRects(chunk, p, rects) || p > hdr) return -1;
    Image atlas;
    if (!decodeTim(chunk, hdr, atlas)) return -1;
    const int ox = cellX * kStageCell, oy = cellY * kStageCell;
    for (const Rect& r : rects) {
        for (int y = 0; y < r.h; ++y)
            for (int x = 0; x < r.w; ++x) {
                const int sx = r.sx + x, sy = r.sy + y, dx = ox + r.dx + x, dy = oy + r.dy + y;
                if (!atlas.inside(sx, sy) || !map.inside(dx, dy)) continue;
                uint32_t c = atlas.at(sx, sy);
                if (c >> 24) map.px[size_t(dy) * map.w + dx] = c;
            }
    }
    return 1;
}
}  // namespace

bool loadStagePack(ByteView v, StageMap& out, std::string* err) {
    auto fail = [&](const char* m) { if (err) *err = m; return false; };
    out = {};
    // Header: u32 chunkCount, u32 gridW, u32 gridH, u32 chunkStride, then u16 size per grid cell
    // (0 = empty cell). Every cell owns a slot at 0x800 + cell*stride; empty cells' slots are zero.
    uint32_t count, gw, gh, stride;
    if (!v.u32(0, count) || !v.u32(4, gw) || !v.u32(8, gh) || !v.u32(12, stride)) return fail("short header");
    if (gw == 0 || gh == 0 || gw > 64 || gh > 64 || count > gw * gh) return fail("bad grid");
    if (stride == 0 || (stride & 0x7ff)) return fail("bad chunk stride");
    std::vector<uint16_t> cellSize(size_t(gw) * gh);
    uint32_t nonEmpty = 0;
    for (size_t c = 0; c < cellSize.size(); ++c) {
        if (!v.u16(16 + c * 2, cellSize[c])) return fail("short cell table");
        nonEmpty += cellSize[c] != 0;
    }
    if (nonEmpty != count) return fail("cell table does not match chunk count");
    out.gridW = int(gw);
    out.gridH = int(gh);
    out.image.resize(int(gw) * kStageCell, int(gh) * kStageCell);

    Bytes buf;
    for (size_t cell = 0; cell < cellSize.size(); ++cell) {
        if (cellSize[cell] == 0) { ++out.chunksEmpty; continue; }
        const size_t p = 0x800 + cell * stride;
        if (!v.has(p, 8)) { ++out.chunksFailed; continue; }
        ByteView chunk = v.sub(p, stride);
        if (isRlen(v, p)) {
            if (!decodeRlen(v, p, buf)) { ++out.chunksFailed; continue; }
            chunk = ByteView(buf);
        }
        int r = decodeChunk(chunk, out.image, int(cell % gw), int(cell / gw));
        if (r > 0) ++out.chunksDecoded;
        else if (r == 0) ++out.chunksEmpty;
        else ++out.chunksFailed;
    }
    if (out.chunksDecoded == 0) return fail("no chunks decoded");
    return true;
}

}  // namespace dw3
