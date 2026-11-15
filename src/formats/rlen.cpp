#include "formats/rlen.h"

namespace dw3 {

bool isRlen(ByteView v, size_t off) {
    return v.has(off, 8) && v.data[off] == 'R' && v.data[off + 1] == 'L' && v.data[off + 2] == 'E' &&
           v.data[off + 3] == 'N';
}

bool decodeRlen(ByteView v, size_t off, Bytes& out, size_t* end) {
    uint32_t n;
    if (!isRlen(v, off) || !v.u32(off + 4, n) || n > (64u << 20)) return false;
    out.clear();
    out.reserve(n);
    size_t i = off + 8;
    while (out.size() < n) {
        if (i >= v.size) return false;
        uint8_t c = v.data[i++];
        size_t count = c & 0x7f;
        if (count == 0) return false;  // would never terminate
        if (c & 0x80) {
            if (i >= v.size) return false;
            out.insert(out.end(), count, v.data[i++]);
        } else {
            if (!v.has(i, count)) return false;
            out.insert(out.end(), v.data + i, v.data + i + count);
            i += count;
        }
    }
    out.resize(n);
    if (end) *end = i;
    return true;
}

}  // namespace dw3
