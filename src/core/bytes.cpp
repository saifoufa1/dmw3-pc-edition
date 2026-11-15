#include "core/bytes.h"

#include <cstdlib>
#include <fstream>

namespace fs = std::filesystem;

namespace dw3 {

bool readFile(const fs::path& p, Bytes& out) {
    std::ifstream f(p, std::ios::binary | std::ios::ate);
    if (!f) return false;
    std::streamsize n = f.tellg();
    if (n < 0) return false;
    out.resize(size_t(n));
    f.seekg(0);
    return n == 0 || bool(f.read(reinterpret_cast<char*>(out.data()), n));
}

static bool isDataRoot(const fs::path& p) {
    std::error_code ec;
    return fs::is_directory(p / "DAT" / "Z_STAGE", ec) && fs::is_directory(p / "DAT" / "FIELD", ec);
}

fs::path findDataRoot(const std::string& explicitPath, const fs::path& exeDir) {
    if (!explicitPath.empty()) {
        fs::path p(explicitPath);
        if (isDataRoot(p)) return p;
        if (isDataRoot(p / "game_data")) return p / "game_data";
        return {};
    }
    if (const char* env = std::getenv("DW3_DATA")) {
        if (isDataRoot(env)) return fs::path(env);
    }
    std::error_code ec;
    for (fs::path start : {exeDir, fs::current_path(ec)}) {
        fs::path p = start;
        for (int i = 0; i < 6 && !p.empty(); ++i) {
            if (isDataRoot(p / "game_data")) return p / "game_data";
            if (isDataRoot(p)) return p;
            fs::path parent = p.parent_path();
            if (parent == p) break;
            p = parent;
        }
    }
    return {};
}

}  // namespace dw3
