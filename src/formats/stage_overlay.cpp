#include "formats/stage_overlay.h"

namespace dw3 {

namespace {
bool addrToOff(ByteView v, uint32_t addr, size_t n, size_t& off) {
    if (addr < kWstagBase) return false;
    off = addr - kWstagBase;
    return v.has(off, n);
}
}  // namespace

bool loadStageOverlay(ByteView v, StageOverlay& out, std::string* err) {
    out = {};
    auto fail = [&](const char* m) { if (err) *err = m; return false; };
    if (v.size < 16 || v.size > 0x40000) return fail("bad overlay size");

    // Linear register tracking (lui/addiu/ori) over the code; record `sw rt, imm(rs)` where rs holds
    // the descriptor address. Stops at the first `jr ra` after descriptor stores were seen.
    uint32_t reg[32] = {};
    bool known[32] = {};
    bool seen = false;
    for (size_t off = 4; off + 4 <= v.size; off += 4) {
        uint32_t x;
        v.u32(off, x);
        const uint32_t op = x >> 26, rs = (x >> 21) & 31, rt = (x >> 16) & 31, imm = x & 0xFFFF;
        const int32_t simm = int32_t(int16_t(uint16_t(imm)));
        if (op == 0x0F) { reg[rt] = imm << 16; known[rt] = true; }
        else if (op == 0x09) { reg[rt] = reg[rs] + uint32_t(simm); known[rt] = known[rs]; }
        else if (op == 0x0D) { reg[rt] = reg[rs] | imm; known[rt] = known[rs]; }
        else if (op == 0x2B && known[rs] && reg[rs] == kStageDesc && simm >= 0 && simm < 128 && (simm & 3) == 0) {
            out.desc[simm / 4] = known[rt] ? reg[rt] : 0;
            out.has[simm / 4] = true;
            seen = true;
        } else if (op == 0 && (x & 63) == 0x21 && ((x >> 11) & 31) != 0) {  // addu rd, rs, rt (moves)
            const uint32_t rd = (x >> 11) & 31;
            if (rt == 0) { reg[rd] = reg[rs]; known[rd] = known[rs]; }
            else if (rs == 0) { reg[rd] = reg[rt]; known[rd] = known[rt]; }
            else known[rd] = false;
        }
        if (x == 0x03E00008 && seen) break;
        reg[0] = 0; known[0] = true;
    }
    if (!seen) return fail("no stage descriptor stores found");

    if (out.has[0x2C / 4] && out.has[0x30 / 4]) {
        out.spawnX = int(out.desc[0x2C / 4] >> 8);
        out.spawnY = int(out.desc[0x30 / 4] >> 8);
    }

    // +0x10: scene objects, 18-byte records starting 4 bytes before the pointer, ended by type 0.
    size_t p;
    if (out.has[0x10 / 4] && out.desc[0x10 / 4] && addrToOff(v, out.desc[0x10 / 4] - 4, 18, p)) {
        for (int n = 0; n < 1024 && v.has(p, 18) && v.data[p] != 0; ++n, p += 18) {
            OverlayObject o;
            uint16_t a, b, c, x, y;
            o.type = v.data[p];
            o.link = v.data[p + 1];
            v.u16(p + 2, o.flags);
            v.u16(p + 4, a); v.u16(p + 6, b); v.u16(p + 8, c); v.u16(p + 10, x); v.u16(p + 12, y);
            v.u16(p + 14, o.depthY); v.u16(p + 16, o.unk);
            o.frame = uint8_t(a & 0xFF); o.mode = uint8_t(a >> 8);
            o.loopFrom = uint8_t(b & 0xFF); o.loopTo = uint8_t(b >> 8);
            o.delay = uint8_t(c & 0xFF); o.counter = uint8_t(c >> 8);
            o.x = int16_t(x); o.y = int16_t(y);
            out.objects.push_back(o);
        }
    }
    // +0x4C: 0-terminated list of pointers to 20-byte event actor records.
    if (out.has[0x4C / 4] && out.desc[0x4C / 4] && addrToOff(v, out.desc[0x4C / 4], 4, p)) {
        for (int n = 0; n < 1024; ++n, p += 4) {
            uint32_t ptr;
            size_t r;
            if (!v.u32(p, ptr) || ptr == 0 || !addrToOff(v, ptr, 20, r)) break;
            OverlayActor a;
            uint16_t x, y, d;
            v.u32(r, a.condPtr); v.u16(r + 4, a.charId); v.u16(r + 6, a.slot);
            v.u16(r + 8, x); v.u16(r + 10, y); v.u16(r + 12, d); v.u32(r + 16, a.nextPtr);
            a.x = int16_t(x); a.y = int16_t(y); a.dir = int16_t(d);
            out.actors.push_back(a);
        }
    }
    out.ok = true;
    return true;
}

}  // namespace dw3
