// Little-endian, bounds-checked readers for PS1 data blobs.
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace dw3 {

using Bytes = std::vector<uint8_t>;

struct ByteView {
    const uint8_t* data = nullptr;
    size_t size = 0;

    ByteView() = default;
    ByteView(const uint8_t* d, size_t s) : data(d), size(s) {}
    ByteView(const Bytes& b) : data(b.data()), size(b.size()) {}

    bool has(size_t off, size_t n) const { return off <= size && n <= size - off; }

    bool u16(size_t off, uint16_t& v) const {
        if (!has(off, 2)) return false;
        v = uint16_t(data[off] | (data[off + 1] << 8));
        return true;
    }
    bool u32(size_t off, uint32_t& v) const {
        if (!has(off, 4)) return false;
        v = uint32_t(data[off]) | (uint32_t(data[off + 1]) << 8) |
            (uint32_t(data[off + 2]) << 16) | (uint32_t(data[off + 3]) << 24);
        return true;
    }
    bool i32(size_t off, int32_t& v) const {
        uint32_t u;
        if (!u32(off, u)) return false;
        std::memcpy(&v, &u, 4);
        return true;
    }
    ByteView sub(size_t off, size_t n) const {
        if (off > size) return {};
        if (n > size - off) n = size - off;
        return {data + off, n};
    }
};

bool readFile(const std::filesystem::path& p, Bytes& out);

// Locates the folder that contains DAT/ (the user's extracted disc).
// Order: explicit arg, DW3_DATA env var, then walk up from exe dir and cwd
// looking for "game_data/DAT" or "DAT".
std::filesystem::path findDataRoot(const std::string& explicitPath,
                                   const std::filesystem::path& exeDir);

}  // namespace dw3
