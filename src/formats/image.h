#pragma once
#include <cstdint>
#include <filesystem>
#include <vector>

namespace dw3 {

// RGBA8 image. Pixel layout in memory: R, G, B, A (little-endian uint32 = A<<24|B<<16|G<<8|R).
struct Image {
    int w = 0, h = 0;
    std::vector<uint32_t> px;

    void resize(int width, int height, uint32_t fill = 0) {
        w = width;
        h = height;
        px.assign(size_t(w) * size_t(h), fill);
    }
    bool inside(int x, int y) const { return x >= 0 && y >= 0 && x < w && y < h; }
    uint32_t at(int x, int y) const { return px[size_t(y) * w + x]; }
    bool opaque(int x, int y) const { return inside(x, y) && (at(x, y) >> 24) != 0; }
};

inline uint32_t rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
    return uint32_t(r) | (uint32_t(g) << 8) | (uint32_t(b) << 16) | (uint32_t(a) << 24);
}

// Writes a 32-bit top-down BMP (no external deps). Transparent pixels become `bg`.
bool writeBmp(const std::filesystem::path& p, const Image& img, uint32_t bg = rgba(32, 32, 40));

}  // namespace dw3
