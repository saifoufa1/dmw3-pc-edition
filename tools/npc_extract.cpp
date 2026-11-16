// Extraction of the original NPCs (event actors) and area links (doors / exits) of every stage.
//   chars:  WSTAG event actor charId -> FIELDSTG character table -> sprite FILE ID -> data/file_ids.cfg name
//   doors:  trigger zone value & 31 -> WSTAG area link record -> target stage KEY -> FIELDSTG stage table
//           -> WSTAG file id -> data/file_ids.cfg name (WSTAG210 -> S210)
#include "npc_extract.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <unordered_map>

#include "core/bytes.h"
#include "formats/fieldstg.h"
#include "formats/stage_overlay.h"
#include "formats/stage_tables.h"

namespace fs = std::filesystem;

namespace dw3 {

namespace {

// data/file_ids.cfg next to the repo: walk up from the exe folder and the working folder.
fs::path findIds(const fs::path& exeDir) {
    for (fs::path start : {exeDir, fs::current_path()}) {
        for (fs::path p = fs::absolute(start); !p.empty(); p = p.parent_path()) {
            if (fs::exists(p / "data" / "file_ids.cfg")) return p / "data" / "file_ids.cfg";
            if (p == p.parent_path()) break;
        }
    }
    return {};
}

struct Tables {
    FieldStgTables stg;
    std::map<uint16_t, std::string> ids;
    std::string err;
    bool load(const fs::path& root, const fs::path& exeDir) {
        Bytes b;
        if (!readFile(root / "PRO" / "FIELDSTG.PRO", b)) { err = "cannot read PRO/FIELDSTG.PRO"; return false; }
        if (!loadFieldStg(b, stg, &err)) return false;
        const fs::path idp = findIds(exeDir);
        if (idp.empty() || !loadFileIds(idp.string(), ids)) {
            err = "data/file_ids.cfg not found (make it once: dw3check --map-file-ids YOUR_DISC.cue data/file_ids.cfg)";
            return false;
        }
        return true;
    }
    std::string sprite(int charId) const {
        if (charId < 0 || size_t(charId) >= stg.chars.size() || !stg.chars[size_t(charId)].fileId) return "-";
        auto it = ids.find(stg.chars[size_t(charId)].fileId);
        return it != ids.end() && it->second.rfind('P', 0) == 0 ? it->second : "-";
    }
    std::string stage(uint16_t key) const {
        auto s = stg.stageFiles.find(key);
        if (s != stg.stageFiles.end()) {
            auto it = ids.find(s->second);
            if (it != ids.end() && it->second.rfind("WSTAG", 0) == 0) return "S" + it->second.substr(5);
        }
        char b[16];
        std::snprintf(b, sizeof b, "key0x%x", key);
        return b;
    }
};

bool loadOverlay(const fs::path& root, const std::string& stage, StageOverlay& ov) {
    Bytes b;
    return stage.size() == 4 && readFile(root / "PRO" / ("WSTAG" + stage.substr(1) + ".PRO"), b) && loadStageOverlay(b, ov);
}

std::vector<NpcRecord> npcRecords(const Tables& t, const StageOverlay& ov) {
    std::vector<NpcRecord> out;
    for (const auto& a : ov.actors) {
        NpcRecord r;
        r.slot = a.slot;
        r.charId = a.charId;
        r.sprite = t.sprite(a.charId);
        r.x = a.x;
        r.y = a.y;
        r.dir = a.dir;
        r.show = a.show;
        r.talk = a.talk;
        out.push_back(std::move(r));
    }
    return out;
}

std::vector<DoorRecord> doorRecords(const Tables& t, const StageOverlay& ov) {
    std::vector<DoorRecord> out;
    for (size_t i = 0; i < ov.links.size(); ++i) {
        const auto& l = ov.links[i];
        DoorRecord r;
        r.index = int(i);
        r.kind = l.kind;
        r.arg = l.arg;
        if (l.kind == 1) r.target = t.stage(l.arg);
        r.x = l.x;
        r.y = l.y;
        r.dir = l.dir;
        r.link = l.link;
        r.cond = l.cond;
        out.push_back(std::move(r));
    }
    return out;
}

// ---- raw disc (MODE2/2352) ----
struct Disc {
    std::ifstream f;
    uint32_t sectors = 0;
    bool open(const fs::path& p) {
        fs::path bin = p;
        if (p.extension() == ".cue" || p.extension() == ".CUE") {
            std::ifstream c(p);
            std::string line;
            while (std::getline(c, line)) {
                const size_t a = line.find('"'), b = a == std::string::npos ? a : line.find('"', a + 1);
                if (line.find("FILE") != std::string::npos && b != std::string::npos) { bin = p.parent_path() / line.substr(a + 1, b - a - 1); break; }
            }
        }
        f.open(bin, std::ios::binary);
        if (!f) return false;
        f.seekg(0, std::ios::end);
        sectors = uint32_t(uint64_t(f.tellg()) / 2352);
        return sectors > 16;
    }
    bool sector(uint32_t lba, uint8_t* out2048) {
        if (lba >= sectors) return false;
        f.seekg(std::streamoff(uint64_t(lba) * 2352 + 24));
        return bool(f.read(reinterpret_cast<char*>(out2048), 2048));
    }
    bool file(uint32_t lba, uint32_t size, Bytes& out) {
        out.assign(size_t((size + 2047) / 2048) * 2048, 0);
        for (uint32_t i = 0; i * 2048 < size; ++i)
            if (!sector(lba + i, out.data() + size_t(i) * 2048)) return false;
        out.resize(size);
        return true;
    }
};

}  // namespace

int mapFileIds(const fs::path& root, const fs::path& discPath, const fs::path& outPath) {
    Disc d;
    if (!d.open(discPath)) { std::printf("cannot open disc image %s (raw MODE2/2352 .bin or its .cue)\n", discPath.string().c_str()); return 1; }
    // ISO 9660 root directory -> SLUS_*.*
    uint8_t s[2048];
    if (!d.sector(16, s) || std::memcmp(s + 1, "CD001", 5) != 0) { std::printf("not an ISO 9660 disc image\n"); return 1; }
    const uint32_t rootLba = uint32_t(s[156 + 2] | s[156 + 3] << 8 | s[156 + 4] << 16 | s[156 + 5] << 24);
    Bytes exe;
    if (d.sector(rootLba, s)) {
        for (size_t p = 0; p < 2048 && s[p];) {
            const uint8_t len = s[p];
            const uint32_t lba = uint32_t(s[p + 2] | s[p + 3] << 8 | s[p + 4] << 16 | s[p + 5] << 24);
            const uint32_t size = uint32_t(s[p + 10] | s[p + 11] << 8 | s[p + 12] << 16 | s[p + 13] << 24);
            const std::string name(reinterpret_cast<const char*>(s + p + 33), s[p + 32]);
            if (name.rfind("SLUS_014.36", 0) == 0) d.file(lba, size, exe);
            p += len;
        }
    }
    // SLUS_014.36 (USA): u32 LBA per file id at file offset 0x35378 (0x80044B78 in RAM).
    constexpr size_t kLbaTable = 0x35378;
    if (exe.size() < kLbaTable + 0x2000 || std::memcmp(exe.data(), "PS-X EXE", 8) != 0) {
        std::printf("SLUS_014.36 not found on this disc (only the USA version is supported)\n");
        return 1;
    }
    // Every extracted file, keyed by its first 64 bytes (then the whole first sector is compared).
    std::unordered_map<std::string, std::vector<std::pair<fs::path, Bytes>>> heads;
    for (const char* dir : {"PRO", "DAT"}) {
        for (auto& e : fs::recursive_directory_iterator(root / dir)) {
            if (!e.is_regular_file()) continue;
            Bytes b;
            if (!readFile(e.path(), b) || b.size() < 64) continue;
            b.resize(std::min<size_t>(b.size(), 2048));
            heads[std::string(b.begin(), b.begin() + 64)].push_back({e.path(), std::move(b)});
        }
    }
    std::ofstream out(outPath, std::ios::binary);
    if (!out) { std::printf("cannot write %s\n", outPath.string().c_str()); return 1; }
    out << "# DMW3 PC Edition: original FILE IDS -> extracted file names (dw3check --map-file-ids DISC.cue)\n"
           "# File ids index the LBA table of SLUS_014.36 (file offset 0x35378). Used by dw3check --extract-npcs to name\n"
           "# NPC sprites (FIELDSTG character table) and door targets (FIELDSTG stage table). Names only, no game data.\n";
    int matched = 0;
    for (uint32_t id = 0; id < 0x900; ++id) {
        uint32_t lba;
        if (!ByteView(exe).u32(kLbaTable + id * 4, lba) || lba == 0 || lba >= d.sectors || !d.sector(lba, s)) continue;
        auto it = heads.find(std::string(reinterpret_cast<char*>(s), 64));
        if (it == heads.end()) continue;
        for (const auto& [path, first] : it->second) {
            if (std::memcmp(first.data(), s, first.size()) != 0) continue;
            std::string n = path.filename().string();
            const size_t dot = n.find('.');
            if (dot != std::string::npos) n = n.substr(0, dot);
            char b[16];
            std::snprintf(b, sizeof b, "0x%03x ", id);
            out << b << n << "\n";
            ++matched;
            break;
        }
    }
    std::printf("%d file ids matched -> %s\n", matched, outPath.string().c_str());
    return matched > 100 ? 0 : 1;
}

int dumpNpcs(const fs::path& root, const fs::path& exeDir, const std::string& stage) {
    Tables t;
    if (!t.load(root, exeDir)) { std::printf("%s\n", t.err.c_str()); return 1; }
    StageOverlay ov;
    if (!loadOverlay(root, stage, ov)) { std::printf("cannot read overlay of %s\n", stage.c_str()); return 1; }
    std::printf("%s: %zu event actors, %zu area links\n", stage.c_str(), ov.actors.size(), ov.links.size());
    for (const auto& r : npcRecords(t, ov)) {
        std::printf("  slot %2d char %3d %-9s at %4d,%-4d dir %d  show %s\n", r.slot, r.charId, r.sprite.c_str(), r.x, r.y, r.dir,
                    flagListText(r.show).c_str());
        for (const auto& e : r.talk)
            std::printf("      talk if %s set %s -> msg 0x%x\n", flagListText(e.cond).c_str(), flagListText(e.set).c_str(), e.msg);
    }
    for (const auto& d : doorRecords(t, ov)) {
        if (d.kind == 1)
            std::printf("  zone index %2d: warp -> %s at %d,%d dir %d  door object 0x%x  if %s\n", d.index, d.target.c_str(), d.x, d.y,
                        d.dir, d.link, flagListText(d.cond).c_str());
        else
            std::printf("  zone index %2d: kind %d arg 0x%x  if %s\n", d.index, d.kind, d.arg, flagListText(d.cond).c_str());
    }
    return 0;
}

int extractNpcs(const fs::path& root, const fs::path& exeDir, const fs::path& outDir, const std::vector<std::string>& stagesIn) {
    Tables t;
    if (!t.load(root, exeDir)) { std::printf("%s\n", t.err.c_str()); return 1; }
    std::vector<std::string> stages = stagesIn;
    if (stages.empty())
        for (auto& e : fs::directory_iterator(root / "PRO")) {
            const std::string n = e.path().filename().string();
            if (n.rfind("WSTAG", 0) == 0 && n.size() == 12) stages.push_back("S" + n.substr(5, 3));
        }
    std::sort(stages.begin(), stages.end());
    std::error_code ec;
    fs::create_directories(outDir / "npcs", ec);
    fs::create_directories(outDir / "doors", ec);
    int written = 0, skipped = 0;
    for (const auto& st : stages) {
        StageOverlay ov;
        if (!loadOverlay(root, st, ov)) { ++skipped; continue; }
        const std::string head = "# " + st + " - extracted from PRO/WSTAG" + st.substr(1) +
                                 ".PRO by `dw3check --extract-npcs data` (regenerate, do not edit by hand)\n";
        if (!saveNpcTable((outDir / "npcs" / (st + ".npc")).string(), head, npcRecords(t, ov)) ||
            !saveDoorTable((outDir / "doors" / (st + ".door")).string(), head, doorRecords(t, ov))) {
            std::printf("cannot write tables for %s\n", st.c_str());
            return 1;
        }
        ++written;
    }
    std::printf("%d stages written to %s (npcs/, doors/), %d without a readable overlay\n", written, outDir.string().c_str(), skipped);
    return 0;
}

int checkStageLinks(const fs::path& root, const fs::path& exeDir) {
    int failures = 0;
    Bytes b;
    FieldStgTables stg;
    std::string err;
    if (!readFile(root / "PRO" / "FIELDSTG.PRO", b) || !loadFieldStg(b, stg, &err)) {
        std::printf("FAIL FIELDSTG tables: %s\n", err.c_str());
        return 1;
    }
    int withSprite = 0;
    for (const auto& c : stg.chars) withSprite += c.fileId != 0;
    int links = 0, warps = 0, badKeys = 0;
    for (auto& e : fs::directory_iterator(root / "PRO")) {
        const std::string n = e.path().filename().string();
        if (n.rfind("WSTAG", 0) != 0) continue;
        Bytes ob;
        StageOverlay ov;
        if (!readFile(e.path(), ob) || !loadStageOverlay(ob, ov)) continue;
        for (const auto& l : ov.links) {
            ++links;
            if (l.kind != 1) continue;
            ++warps;
            if (!stg.stageFiles.count(l.arg)) ++badKeys;
        }
    }
    if (badKeys) { std::printf("FAIL area links: %d warps to an unknown stage key\n", badKeys); ++failures; }
    Tables t;
    const bool named = t.load(root, exeDir);
    std::printf("links  : FIELDSTG %zu characters (%d with a sprite), %zu stage keys; %d area links (%d warps)%s\n", stg.chars.size(),
                withSprite, stg.stageFiles.size(), links, warps, named ? "" : " - data/file_ids.cfg missing: names not checked");
    return failures;
}

}  // namespace dw3
