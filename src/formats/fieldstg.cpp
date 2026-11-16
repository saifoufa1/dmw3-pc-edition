#include "formats/fieldstg.h"

#include <fstream>
#include <sstream>

namespace dw3 {

namespace {
constexpr uint32_t kCharTable = 0x80099134;   // u32 per character id
constexpr uint32_t kCharSize = 0x80099758;    // u8 per character id
constexpr uint32_t kStageTable = 0x800998E4;  // {u32 key, u32 WSTAG file id, u32 ptr}, ended by key 0
constexpr int kChars = int((kCharSize - kCharTable) / 4);  // 393
}  // namespace

bool loadFieldStg(ByteView v, FieldStgTables& out, std::string* err) {
    out = {};
    auto fail = [&](const char* m) { if (err) *err = m; return false; };
    const size_t ct = kCharTable - kFieldStgBase, cs = kCharSize - kFieldStgBase, st = kStageTable - kFieldStgBase;
    if (!v.has(ct, size_t(kChars) * 4) || !v.has(cs, size_t(kChars))) return fail("FIELDSTG too small");
    uint32_t player;
    v.u32(ct + 4, player);
    if (player >> 16 == 0 || (player >> 16) > 0x2000) return fail("FIELDSTG character table not found (other version?)");
    for (int c = 0; c < kChars; ++c) {
        uint32_t w;
        v.u32(ct + size_t(c) * 4, w);
        FieldChar fc;
        fc.fileId = uint16_t(w >> 16);
        fc.lo = uint16_t(w & 0xFFFF);
        fc.size = v.data[cs + size_t(c)];
        out.chars.push_back(fc);
    }
    for (size_t p = st; v.has(p, 12); p += 12) {
        uint32_t key, file;
        v.u32(p, key);
        v.u32(p + 4, file);
        if (key == 0) break;
        out.stageFiles[uint16_t(key)] = uint16_t(file);
    }
    if (out.stageFiles.empty()) return fail("FIELDSTG stage table not found");
    out.ok = true;
    return true;
}

bool loadFileIds(const std::string& path, std::map<uint16_t, std::string>& out) {
    std::ifstream f(path);
    if (!f) return false;
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream is(line);
        std::string id, name;
        if (!(is >> id >> name)) continue;
        out[uint16_t(std::stoul(id, nullptr, 0))] = name;
    }
    return !out.empty();
}

}  // namespace dw3
