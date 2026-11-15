#include "formats/tim.h"

namespace dw3 {

static uint32_t ps1Color(uint16_t c) {
    if (c == 0) return 0;  // fully transparent on PS1 GPU
    auto ex = [](int v) { return uint8_t((v << 3) | (v >> 2)); };
    return rgba(ex(c & 31), ex((c >> 5) & 31), ex((c >> 10) & 31), 255);
}

bool looksLikeTim(ByteView v, size_t off) {
    uint32_t magic, flags;
    if (!v.u32(off, magic) || !v.u32(off + 4, flags)) return false;
    if (magic != 0x10 || !(flags == 0 || flags == 1 || flags == 2 || flags == 8 || flags == 9)) return false;
    // Validate block lengths too: a bare magic+flags match happens inside other data.
    size_t p = off + 8;
    uint32_t len;
    uint16_t w, h;
    if (flags & 8) {
        if (!v.u32(p, len) || !v.u16(p + 8, w) || !v.u16(p + 10, h)) return false;
        if (w == 0 || h == 0 || len != 12u + uint32_t(w) * h * 2 || !v.has(p, len)) return false;
        p += len;
    } else if ((flags & 3) < 2) {
        return false;
    }
    if (!v.u32(p, len) || !v.u16(p + 8, w) || !v.u16(p + 10, h)) return false;
    return w != 0 && h != 0 && len == 12u + uint32_t(w) * h * 2 && v.has(p, len);
}

bool decodeTim(ByteView v, size_t off, Image& out, size_t* end, int clutRow) {
    if (!looksLikeTim(v, off)) return false;
    uint32_t flags;
    v.u32(off + 4, flags);
    const int bpp = int(flags & 3);
    size_t p = off + 8;

    // Optional CLUT block: len, x, y, w, h, then w*h 16-bit colours.
    const uint8_t* clut = nullptr;
    int clutW = 0, clutH = 0;
    if (flags & 8) {
        uint32_t len;
        uint16_t cx, cy, cw, ch;
        if (!v.u32(p, len) || !v.u16(p + 4, cx) || !v.u16(p + 6, cy) || !v.u16(p + 8, cw) ||
            !v.u16(p + 10, ch))
            return false;
        if (len < 12 || !v.has(p, len) || size_t(cw) * ch * 2 + 12 > len) return false;
        clut = v.data + p + 12;
        clutW = cw;
        clutH = ch;
        p += len;
    } else if (bpp < 2) {
        return false;  // paletted image without palette
    }

    uint32_t len;
    uint16_t ix, iy, iw, ih;
    if (!v.u32(p, len) || !v.u16(p + 4, ix) || !v.u16(p + 6, iy) || !v.u16(p + 8, iw) ||
        !v.u16(p + 10, ih))
        return false;
    if (iw == 0 || ih == 0 || !v.has(p, len) || size_t(iw) * ih * 2 + 12 > len) return false;
    const uint8_t* pix = v.data + p + 12;

    const int row = (clutH > 0) ? (clutRow % clutH) : 0;
    auto clutColor = [&](int idx) -> uint32_t {
        if (idx >= clutW) return 0;
        const uint8_t* c = clut + (size_t(row) * clutW + idx) * 2;
        return ps1Color(uint16_t(c[0] | (c[1] << 8)));
    };

    switch (bpp) {
        case 0:  // 4bpp: each 16-bit VRAM word holds 4 pixels
            out.resize(iw * 4, ih);
            for (int y = 0; y < ih; ++y)
                for (int x = 0; x < iw * 2; ++x) {
                    uint8_t b = pix[size_t(y) * iw * 2 + x];
                    out.px[size_t(y) * out.w + x * 2] = clutColor(b & 15);
                    out.px[size_t(y) * out.w + x * 2 + 1] = clutColor(b >> 4);
                }
            break;
        case 1:  // 8bpp
            out.resize(iw * 2, ih);
            for (size_t i = 0; i < out.px.size(); ++i) out.px[i] = clutColor(pix[i]);
            break;
        case 2:  // 16bpp direct colour
            out.resize(iw, ih);
            for (size_t i = 0; i < out.px.size(); ++i)
                out.px[i] = ps1Color(uint16_t(pix[i * 2] | (pix[i * 2 + 1] << 8)));
            break;
        default:
            return false;
    }
    if (end) *end = p + len;
    return true;
}

}  // namespace dw3
