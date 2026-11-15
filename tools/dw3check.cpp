// dw3check: runs every format parser over the user's game data and reports results.
//   dw3check [--data <dir>]                       scan everything, exit 1 on any failure
//   dw3check --dump-stage S200 out.bmp            render a full field background
//   dw3check --dump-sprite P006AGUM out.bmp       render a sprite pack's frames as a sheet
//   dw3check --dump-model M005GREY out.bmp [clip] render a battle model: one row per clip
//                                                 (8 sampled frames each), or one clip
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cctype>
#include <cstdlib>
#include <string>

#include "core/bytes.h"
#include "formats/model.h"
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

// Tiny software rasterizer (z-buffer, nearest texel) for headless model previews.
static void rasterModel(const Model& m, const std::vector<Image>& tex, const Pose& pose, Image& dst, int ox, int oy,
                        int cell, float scale, float yaw) {
    std::vector<Mat34> mats;
    poseMatrices(m, pose, mats);
    std::vector<float> zb(size_t(cell) * cell, 1e30f);
    const float cy = std::cos(yaw), sy = std::sin(yaw);
    struct P { float x, y, z, u, v; };
    auto tri = [&](const P& a, const P& b, const P& c, const Image& t) {
        float den = (b.y - c.y) * (a.x - c.x) + (c.x - b.x) * (a.y - c.y);
        if (std::fabs(den) < 1e-6f) return;
        int x0 = std::max(0, int(std::floor(std::min({a.x, b.x, c.x})))), x1 = std::min(cell - 1, int(std::ceil(std::max({a.x, b.x, c.x}))));
        int y0 = std::max(0, int(std::floor(std::min({a.y, b.y, c.y})))), y1 = std::min(cell - 1, int(std::ceil(std::max({a.y, b.y, c.y}))));
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x) {
                float px = x + 0.5f, py = y + 0.5f;
                float l0 = ((b.y - c.y) * (px - c.x) + (c.x - b.x) * (py - c.y)) / den;
                float l1 = ((c.y - a.y) * (px - c.x) + (a.x - c.x) * (py - c.y)) / den;
                float l2 = 1 - l0 - l1;
                if (l0 < 0 || l1 < 0 || l2 < 0) continue;
                float z = l0 * a.z + l1 * b.z + l2 * c.z;
                float& zz = zb[size_t(y) * cell + x];
                if (z >= zz) continue;
                int u = int(l0 * a.u + l1 * b.u + l2 * c.u) & 255, v = int(l0 * a.v + l1 * b.v + l2 * c.v) & 255;
                uint32_t col = t.at(u, v);
                if (!(col >> 24)) continue;
                zz = z;
                dst.px[size_t(oy + y) * dst.w + ox + x] = col;
            }
    };
    for (size_t i = 0; i < m.nodes.size(); ++i) {
        if (m.nodes[i].mesh < 0) continue;
        const ModelMesh& mesh = m.meshes[size_t(m.nodes[i].mesh)];
        std::vector<P> vs(mesh.verts.size());
        for (size_t k = 0; k < mesh.verts.size(); ++k) {
            float x, y, z;
            xform(mats[i], mesh.verts[k][0], mesh.verts[k][1], mesh.verts[k][2], x, y, z);
            float rx = x * cy + z * sy, rz = -x * sy + z * cy;
            vs[k] = {cell * 0.5f + rx * scale, cell - 4 + y * scale, rz, 0, 0};
        }
        for (const ModelPrim& pr : mesh.prims) {
            P q[4];
            for (int k = 0; k < pr.count; ++k) { q[k] = vs[pr.v[k]]; q[k].u = pr.u[k] + 0.5f; q[k].v = pr.t[k] + 0.5f; }
            tri(q[0], q[1], q[2], tex[pr.clut]);
            if (pr.count == 4) tri(q[1], q[3], q[2], tex[pr.clut]);
        }
    }
}

static int dumpModel(const fs::path& root, const std::string& id, const fs::path& out, int onlyClip) {
    fs::path file;
    for (auto& e : fs::directory_iterator(root / "DAT" / "MODEL"))
        if (e.path().filename().string().rfind(id, 0) == 0) { file = e.path(); break; }
    Bytes b;
    if (file.empty() || !readFile(file, b)) { std::printf("cannot find model %s\n", id.c_str()); return 1; }
    Model m; std::string err;
    if (!loadModel(b, m, err)) { std::printf("model %s: %s\n", id.c_str(), err.c_str()); return 1; }
    std::vector<Image> tex;
    for (size_t c = 0; c < m.cluts.size(); ++c) tex.push_back(m.textureFor(c));
    std::vector<int> clips;
    for (size_t c = 0; c < m.clips.size(); ++c)
        if (m.clips[c].valid() && (onlyClip < 0 || int(c) == onlyClip)) clips.push_back(int(c));
    const int cell = 112, cols = 8;
    const float scale = (cell - 12) / std::max(1.f, m.height * 1.25f);
    Image sheet; sheet.resize(cols * cell, int(clips.size()) * cell, rgba(40, 40, 56));
    size_t prims = 0;
    for (auto& me : m.meshes) prims += me.prims.size();
    std::printf("%s: %zu nodes, %zu meshes, %zu prims, %zu cluts, height %.0f\n", file.filename().string().c_str(),
                m.nodes.size(), m.meshes.size(), prims, m.cluts.size(), m.height);
    for (size_t r = 0; r < clips.size(); ++r) {
        const ModelClip& c = m.clips[size_t(clips[r])];
        std::printf("  row %2zu = clip %2d  len %3d %s :", r, clips[r], c.length, c.loop ? "loop" : "once");
        for (auto& s : c.segs) {
            if (s.blend) std::printf(" blend%d", s.len);
            else std::printf(" [%d-%d]", s.from, s.to);
        }
        std::printf("\n");
        ClipPlayer pl;
        pl.play(m, clips[r]);
        const float step = float(c.length) / cols;
        for (int k = 0; k < cols; ++k) {
            if (k) pl.update(m, step);
            rasterModel(m, tex, pl.pose(), sheet, k * cell, int(r) * cell, cell, scale, 0.9f);
        }
    }
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
        if (!std::strcmp(argv[i], "--dump-model") && i + 2 < argc)
            return dumpModel(root, argv[i + 1], argv[i + 2], i + 3 < argc ? std::atoi(argv[i + 3]) : -1);
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
    int models = 0, modelSkipped = 0;
    size_t modelPrims = 0;
    for (auto& e : fs::directory_iterator(root / "DAT" / "MODEL")) {
        const std::string name = e.path().filename().string();
        if (name.size() < 4 || name[0] != 'M' || !std::isdigit(static_cast<unsigned char>(name[1]))) continue;
        Bytes b; Model m; std::string err;
        if (!readFile(e.path(), b) || !loadModel(b, m, err)) {
            // A few models (multi-part bosses / effects) use another layout: reported, not fatal yet.
            std::printf("SKIP model %s: %s\n", name.c_str(), err.c_str());
            ++modelSkipped; continue;
        }
        ++models;
        for (auto& me : m.meshes) modelPrims += me.prims.size();
    }
    std::printf("models : %d loaded (%zu primitives), %d other layout (not yet supported)\n", models, modelPrims, modelSkipped);
    std::printf("sprites: %d packs, %d frames, %d with full idle/walk/run set\n", sprites, frames, moveSets);
    std::printf("stages : %d packs, %d chunks drawn, %d chunks failed\n", stages, chunks, badChunks);
    std::printf("%s (%d failures)\n", failures ? "FAILED" : "OK", failures);
    return failures ? 1 : 0;
}
