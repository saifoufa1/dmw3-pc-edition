// dw3check: runs every format parser over the user's game data and reports results.
//   dw3check [--data <dir>]                       scan everything, exit 1 on any failure
//   dw3check --dump-stage S200 out.bmp            render a full field background
//   dw3check --dump-sprite P006AGUM out.bmp       render a sprite pack's frames as a sheet
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>

#include "core/bytes.h"
#include "formats/sprite_pack.h"
#include "formats/stage_pack.h"

namespace fs = std::filesystem;
using namespace dw3;

static int dumpStage(const fs::path& root, const std::string& id, const fs::path& out) {
    Bytes b;
    if (!readFile(root / "DAT" / "Z_STAGE" / (id + "PACK.BIN"), b)) { std::printf("cannot read %s\n", id.c_str()); return 1; }
    StageMap m; std::string err;
    if (!loadStagePack(b, m, &err)) { std::printf("stage %s: %s\n", id.c_str(), err.c_str()); return 1; }
    std::printf("%s: %dx%d cells, %d drawn, %d empty, %d failed\n", id.c_str(), m.gridW, m.gridH,
                m.chunksDecoded, m.chunksEmpty, m.chunksFailed);
    return writeBmp(out, m.image, rgba(0, 0, 0)) ? 0 : 1;
}

static int dumpSprite(const fs::path& root, const std::string& id, const fs::path& out) {
    Bytes b;
    if (!readFile(root / "DAT" / "FIELD" / "SPRT" / (id + ".BIN"), b)) { std::printf("cannot read %s\n", id.c_str()); return 1; }
    SpritePack sp; std::string err;
    if (!loadSpritePack(b, sp, &err)) { std::printf("sprite %s: %s\n", id.c_str(), err.c_str()); return 1; }
    int cw = 0, ch = 0;
    for (auto& f : sp.frames) { cw = std::max(cw, f.w + 2); ch = std::max(ch, f.h + 2); }
    const int cols = 16, rows = int((sp.frames.size() + cols - 1) / cols);
    Image sheet; sheet.resize(cols * cw, rows * ch);
    for (size_t i = 0; i < sp.frames.size(); ++i) {
        const Image& f = sp.frames[i];
        int ox = int(i % cols) * cw, oy = int(i / cols) * ch;
        for (int y = 0; y < f.h; ++y)
            for (int x = 0; x < f.w; ++x) sheet.px[size_t(oy + y) * sheet.w + ox + x] = f.at(x, y);
    }
    size_t valid = std::count_if(sp.anims.begin(), sp.anims.end(), [](const Anim& a) { return !a.keys.empty(); });
    std::printf("%s: %zu frames, %zu anim slots (%zu valid)\n", id.c_str(), sp.frames.size(), sp.anims.size(), valid);
    return writeBmp(out, sheet) ? 0 : 1;
}

int main(int argc, char** argv) {
    std::string dataArg;
    for (int i = 1; i < argc; ++i)
        if (!std::strcmp(argv[i], "--data") && i + 1 < argc) dataArg = argv[++i];
    fs::path root = findDataRoot(dataArg, fs::path(argv[0]).parent_path());
    if (root.empty()) { std::printf("game data not found (use --data <dir containing DAT>)\n"); return 2; }
    std::printf("data root: %s\n", root.string().c_str());

    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--dump-stage") && i + 2 < argc) return dumpStage(root, argv[i + 1], argv[i + 2]);
        if (!std::strcmp(argv[i], "--dump-sprite") && i + 2 < argc) return dumpSprite(root, argv[i + 1], argv[i + 2]);
    }

    int failures = 0, sprites = 0, frames = 0, stages = 0, chunks = 0, badChunks = 0, moveSets = 0;
    for (auto& e : fs::directory_iterator(root / "DAT" / "FIELD" / "SPRT")) {
        Bytes b; SpritePack sp; std::string err;
        if (!readFile(e.path(), b) || !loadSpritePack(b, sp, &err)) {
            std::printf("FAIL sprite %s: %s\n", e.path().filename().string().c_str(), err.c_str());
            ++failures; continue;
        }
        ++sprites; frames += int(sp.frames.size());
        bool hasMoveSet = sp.anims.size() >= 15 &&
            std::all_of(sp.anims.begin(), sp.anims.begin() + 15, [](const Anim& a) { return !a.keys.empty(); });
        moveSets += hasMoveSet;
    }
    for (auto& e : fs::directory_iterator(root / "DAT" / "Z_STAGE")) {
        Bytes b; StageMap m; std::string err;
        if (!readFile(e.path(), b) || !loadStagePack(b, m, &err)) {
            std::printf("FAIL stage %s: %s\n", e.path().filename().string().c_str(), err.c_str());
            ++failures; continue;
        }
        ++stages; chunks += m.chunksDecoded; badChunks += m.chunksFailed;
        if (m.chunksFailed) std::printf("WARN stage %s: %d chunk(s) failed\n", e.path().filename().string().c_str(), m.chunksFailed);
    }
    std::printf("sprites: %d packs, %d frames, %d with full idle/walk/run set\n", sprites, frames, moveSets);
    std::printf("stages : %d packs, %d chunks drawn, %d chunks failed\n", stages, chunks, badChunks);
    std::printf("%s (%d failures)\n", failures ? "FAILED" : "OK", failures);
    return failures ? 1 : 0;
}
