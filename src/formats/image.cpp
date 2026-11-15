#include "formats/image.h"

#include <fstream>

namespace dw3 {

static void put16(std::ofstream& f, uint16_t v) { f.put(char(v & 0xff)); f.put(char(v >> 8)); }
static void put32(std::ofstream& f, uint32_t v) {
    for (int i = 0; i < 4; ++i) f.put(char((v >> (8 * i)) & 0xff));
}

bool writeBmp(const std::filesystem::path& p, const Image& img, uint32_t bg) {
    std::ofstream f(p, std::ios::binary);
    if (!f || img.w <= 0 || img.h <= 0) return false;
    const uint32_t dataSize = uint32_t(img.w) * uint32_t(img.h) * 4;
    f.put('B'); f.put('M');
    put32(f, 54 + dataSize); put32(f, 0); put32(f, 54);
    put32(f, 40); put32(f, uint32_t(img.w)); put32(f, uint32_t(-img.h));  // negative = top-down
    put16(f, 1); put16(f, 32); put32(f, 0); put32(f, dataSize);
    put32(f, 2835); put32(f, 2835); put32(f, 0); put32(f, 0);
    for (uint32_t c : img.px) {
        if ((c >> 24) == 0) c = bg;
        f.put(char((c >> 16) & 0xff));  // B
        f.put(char((c >> 8) & 0xff));   // G
        f.put(char(c & 0xff));          // R
        f.put(static_cast<char>(0xff));
    }
    return bool(f);
}

}  // namespace dw3
