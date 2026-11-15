#include "formats/sprite_pack.h"

#include "formats/tim.h"

namespace dw3 {

bool loadSpritePack(ByteView v, SpritePack& out, std::string* err) {
    auto fail = [&](const char* m) { if (err) *err = m; return false; };
    out = {};
    uint32_t first;
    if (!v.u32(0, first) || first < 4 || (first & 3) || first > v.size) return fail("bad offset table");
    const size_t count = first / 4;
    std::vector<uint32_t> offs(count);
    for (size_t i = 0; i < count; ++i) v.u32(i * 4, offs[i]);

    // Frames are the leading TIM entries; animation scripts follow.
    size_t i = 0;
    for (; i < count && looksLikeTim(v, offs[i]); ++i) {
        Image img;
        if (!decodeTim(v, offs[i], img)) return fail("corrupt TIM frame");
        out.frames.push_back(std::move(img));
    }
    if (out.frames.empty()) return fail("no frames");

    for (; i < count; ++i) {
        Anim a;
        size_t limit = (i + 1 < count && offs[i + 1] > offs[i]) ? offs[i + 1] : v.size;
        size_t p = offs[i];
        bool ok = false;
        while (p + 4 <= limit) {
            int32_t dur;
            v.i32(p, dur);
            if (dur <= 0) {  // 0 = loop, -1 = play once
                a.loop = (dur == 0);
                ok = true;
                break;
            }
            int32_t f, ox, oy;
            if (p + 16 > limit || !v.i32(p + 4, f) || !v.i32(p + 8, ox) || !v.i32(p + 12, oy)) break;
            if (f < 0 || size_t(f) >= out.frames.size() || dur > 10000) break;
            a.keys.push_back({dur, f, ox, oy});
            p += 16;
        }
        if (!ok || a.keys.empty()) a = {};  // not an animation (other script data) -> keep slot empty
        out.anims.push_back(std::move(a));
    }
    return true;
}

}  // namespace dw3
