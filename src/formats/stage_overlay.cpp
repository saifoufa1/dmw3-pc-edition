#include "formats/stage_overlay.h"

namespace dw3 {

namespace {
bool addrToOff(ByteView v, uint32_t addr, size_t n, size_t& off) {
    if (addr < kWstagData) return false;
    off = addr - kWstagData;
    return v.has(off, n);
}

std::vector<FlagCond> readConds(ByteView v, uint32_t addr) {
    std::vector<FlagCond> out;
    size_t p;
    if (!addr || !addrToOff(v, addr, 4, p)) return out;
    for (int n = 0; n < 64 && v.has(p, 4); ++n, p += 4) {
        FlagCond c;
        v.u16(p, c.flag);
        v.u16(p + 2, c.value);
        if (c.flag == 0xFFFF) break;
        out.push_back(c);
    }
    return out;
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

    // +0x10: scene objects, 18-byte records, ended by type 0.
    size_t p;
    if (out.has[0x10 / 4] && out.desc[0x10 / 4] && addrToOff(v, out.desc[0x10 / 4], 18, p)) {
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
            v.u32(r, a.showPtr); v.u32(r + 4, a.talkPtr); v.u16(r + 8, a.charId); v.u16(r + 10, a.slot);
            v.u16(r + 12, x); v.u16(r + 14, y); v.u16(r + 16, d);
            a.x = int16_t(x); a.y = int16_t(y); a.dir = int16_t(d);
            a.show = readConds(v, a.showPtr);
            size_t t;
            if (a.talkPtr && addrToOff(v, a.talkPtr, 12, t)) {
                for (int k = 0; k < 64 && v.has(t, 12); ++k, t += 12) {
                    uint32_t c, st, m;
                    v.u32(t, c); v.u32(t + 4, st); v.u32(t + 8, m);
                    if (!c && !st && !m) break;
                    TalkEntry e;
                    e.cond = readConds(v, c);
                    e.set = readConds(v, st);
                    e.msg = m;
                    a.talk.push_back(std::move(e));
                }
            }
            out.actors.push_back(a);
        }
    }
    // +0x14: area links (warps / event zones), 24-byte records, ended by kind 0 with no condition.
    if (out.has[0x14 / 4] && out.desc[0x14 / 4] && addrToOff(v, out.desc[0x14 / 4], 24, p)) {
        for (int n = 0; n < 64 && v.has(p, 24); ++n, p += 24) {
            uint16_t w[12];
            for (int k = 0; k < 12; ++k) v.u16(p + size_t(k) * 2, w[k]);
            if (w[4] == 0 && w[5] == 0) break;
            OverlayLink l;
            if (w[0] != 0xFFFF) l.cond.push_back({w[0], w[1]});
            if (w[2] != 0xFFFF) l.cond.push_back({w[2], w[3]});
            l.kind = w[4];
            l.arg = w[5];
            l.x = int16_t(w[6]);
            l.y = int16_t(w[7]);
            l.dir = w[8];
            l.link = w[9];
            out.links.push_back(std::move(l));
        }
    }
    out.ok = true;
    return true;
}

}  // namespace dw3
