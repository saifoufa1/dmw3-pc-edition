// dw3check: runs every format parser over the user's game data and reports results.
//   dw3check [--data <dir>]                       scan everything, exit 1 on any failure
//   dw3check --dump-stage S200 out.bmp            render a full field background
//   dw3check --dump-sprite P006AGUM out.bmp       render a sprite pack's frames as a sheet
//   dw3check --dump-model M005GREY out.bmp [clip] render a battle model: one row per clip
//                                                 (8 sampled frames each), or one clip
//   dw3check --dump-text DMG200 [block]           print event text (console only, never commit the output)
//   dw3check --dump-text TALK00 [id]              print NPC talk text
//   dw3check --dump-text S200                     print a stage's WSTAG actors + talk tables with their lines
//   dw3check --dump-arena 1001 out.bmp [yaw] [pitch] [dist]  render a 3D battle arena (MEFT1001..1055) in
//                                                 perspective and print its nodes
//   dw3check --dump-ui CMFONT out.bmp [parts]     render a UI cell sheet's pieces (or all parts) as a contact sheet
//                                                 (CMFONT, PGFGT, STFRPT, STDGLB, JTWND, any SCREEN name like STSTAT)
//   dw3check --dump-pack TTCOMMPK out.bmp [parts]  same for a combined title pack (DAT/SCREEN/TTCOMMPK, TLOGOUSA)
#include <algorithm>
#include <array>
#include <map>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cctype>
#include <cstdlib>
#include <string>

#include "core/bytes.h"
#include "formats/arena.h"
#include "formats/model.h"
#include "formats/sprite_pack.h"
#include "formats/stage_pack.h"
#include "formats/field_stage.h"
#include "formats/stage_overlay.h"
#include "formats/dw3_text.h"
#include "formats/ui_sheet.h"

namespace fs = std::filesystem;
using namespace dw3;

// Loads MASK/TMPK + WSTAG overlay of a stage (missing files are fine).
static bool loadFieldAll(const fs::path& root, const std::string& id, int gw, int gh, FieldStage& f, StageOverlay& ov,
                         std::string& err) {
    Bytes mask, tmpk, pro;
    readFile(root / "DAT" / "FIELD" / "STAGE" / (id + "MASK.BIN"), mask);
    readFile(root / "DAT" / "FIELD" / "STAGE" / (id + "TMPK.BIN"), tmpk);
    const bool okF = loadFieldStage(mask, tmpk, gw, gh, f, &err);
    if (readFile(root / "PRO" / ("WSTAG" + id.substr(1) + ".PRO"), pro)) loadStageOverlay(pro, ov, &err);
    return okF;
}

// Background + every scene object's first frame; optional walk (green) / trigger (cyan) tint and
// event actor markers (magenta). Debug view only.
static int dumpField(const fs::path& root, const std::string& id, const fs::path& out, bool overlay) {
    Bytes b;
    if (!readFile(root / "DAT" / "Z_STAGE" / (id + "PACK.BIN"), b)) { std::printf("cannot read %s\n", id.c_str()); return 1; }
    StageMap m; std::string err;
    if (!loadStagePack(b, m, &err)) { std::printf("stage %s: %s\n", id.c_str(), err.c_str()); return 1; }
    FieldStage f; StageOverlay ov;
    loadFieldAll(root, id, m.gridW, m.gridH, f, ov, err);
    Image& im = m.image;
    int drawn = 0, missing = 0;
    for (const auto& o : ov.objects) {
        const int frame = o.mode == 1 ? o.loopFrom : o.frame;
        Image pc; int ox = 0, oy = 0;
        const int pi = f.pieceForFrame(frame);
        if (pi < 0 || !f.renderPiece(pi, pc, ox, oy)) { ++missing; continue; }
        const bool add = f.pieces[size_t(pi)].flag != -1;
        for (int y = 0; y < pc.h; ++y)
            for (int x = 0; x < pc.w; ++x) {
                const uint32_t c = pc.px[size_t(y) * pc.w + x];
                const int dx = o.x + ox + x, dy = o.y + oy + y;
                if (!(c >> 24) || !im.inside(dx, dy)) continue;
                uint32_t& d = im.px[size_t(dy) * im.w + dx];
                if (add) {
                    uint32_t r = std::min(255u, (d & 255) + (c & 255)), g = std::min(255u, ((d >> 8) & 255) + ((c >> 8) & 255)),
                             bl = std::min(255u, ((d >> 16) & 255) + ((c >> 16) & 255));
                    d = rgba(uint8_t(r), uint8_t(g), uint8_t(bl));
                } else d = c;
            }
        ++drawn;
    }
    if (overlay) {
        for (int y = 0; y < im.h; ++y)
            for (int x = 0; x < im.w; ++x) {
                uint32_t& d = im.px[size_t(y) * im.w + x];
                if (f.triggerAt(x, y)) d = rgba(0, 255, 255);
                else if (f.walkAt(x, y) && ((x + y) % 4) == 0) d = rgba(0, 255, 0);
            }
        // Trigger zones: bounding box per trigger value (exit / door candidates for data/stages/*.stage).
        std::map<int, std::array<int, 4>> zones;
        for (int y = 0; y < im.h; ++y)
            for (int x = 0; x < im.w; ++x)
                if (const int t = f.triggerAt(x, y)) {
                    auto it = zones.find(t);
                    if (it == zones.end()) zones[t] = {x, y, x, y};
                    else it->second = {std::min(it->second[0], x), std::min(it->second[1], y), std::max(it->second[2], x),
                                       std::max(it->second[3], y)};
                }
        for (const auto& [t, z] : zones)
            std::printf("trigger 0x%02x: x %d y %d w %d h %d\n", t, z[0], z[1], z[2] - z[0] + 1, z[3] - z[1] + 1);
        for (const auto& a : ov.actors)
            for (int k = -4; k <= 4; ++k) {
                if (im.inside(a.x + k, a.y)) im.px[size_t(a.y) * im.w + a.x + k] = rgba(255, 0, 255);
                if (im.inside(a.x, a.y + k)) im.px[size_t(a.y + k) * im.w + a.x] = rgba(255, 0, 255);
            }
    }
    std::printf("%s: %zu objects (%d drawn, %d without piece), %zu pieces, walk %d, trigger %d, %zu actors, spawn %d,%d\n",
                id.c_str(), ov.objects.size(), drawn, missing, f.pieces.size(), f.hasWalk, f.hasTrigger, ov.actors.size(),
                ov.spawnX, ov.spawnY);
    return writeBmp(out, im, rgba(0, 0, 0)) ? 0 : 1;
}

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
// fit = true: scale/center the posed geometry into the cell (effects have no "feet").
static void rasterModel(const Model& m, const std::vector<Image>& tex, const Pose& pose, Image& dst, int ox, int oy,
                        int cell, float scale, float yaw, bool fit = false) {
    std::vector<Mat34> mats;
    poseMatrices(m, pose, mats);
    float fitX = 0, fitY = 0;
    if (fit) {
        float x0 = 1e30f, x1 = -1e30f, y0 = 1e30f, y1 = -1e30f;
        const float c = std::cos(yaw), s = std::sin(yaw);
        for (size_t i = 0; i < m.nodes.size(); ++i) {
            if (m.nodes[i].mesh < 0) continue;
            for (auto& v : m.meshes[size_t(m.nodes[i].mesh)].verts) {
                float x, y, z;
                xform(mats[i], v[0], v[1], v[2], x, y, z);
                float rx = x * c + z * s;
                x0 = std::min(x0, rx); x1 = std::max(x1, rx); y0 = std::min(y0, y); y1 = std::max(y1, y);
            }
        }
        if (x1 < x0) return;
        scale = (cell - 8) / std::max(1.f, std::max(x1 - x0, y1 - y0));
        fitX = -(x0 + x1) * 0.5f * scale;
        fitY = cell * 0.5f - (y0 + y1) * 0.5f * scale - (cell - 4);
    }
    std::vector<float> zb(size_t(cell) * cell, 1e30f);
    const float cy = std::cos(yaw), sy = std::sin(yaw);
    struct P { float x, y, z, u, v; };
    auto tri = [&](const P& a, const P& b, const P& c, const Image& t, int blend) {
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
                uint32_t& d = dst.px[size_t(oy + y) * dst.w + ox + x];
                if (blend == 0) { zz = z; d = col; continue; }
                if (blend == 3) continue;  // subtractive: skipped in previews
                uint32_t outc = 0xff000000u;
                for (int sh = 0; sh < 24; sh += 8) {
                    int dc = int((d >> sh) & 255), sc = int((col >> sh) & 255);
                    int r = blend == 1 ? (dc + sc) / 2 : blend == 4 ? dc + sc / 4 : dc + sc;
                    outc |= uint32_t(std::min(255, r)) << sh;
                }
                d = outc;
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
            vs[k] = {cell * 0.5f + rx * scale + fitX, cell - 4 + y * scale + fitY, rz, 0, 0};
        }
        for (const ModelPrim& pr : mesh.prims) {
            P q[4];
            for (int k = 0; k < pr.count; ++k) { q[k] = vs[pr.v[k]]; q[k].u = pr.u[k] + 0.5f; q[k].v = pr.t[k] + 0.5f; }
            tri(q[0], q[1], q[2], tex[pr.clut], pr.blend);
            if (pr.count == 4) tri(q[1], q[3], q[2], tex[pr.clut], pr.blend);
        }
    }
}

// ---- message text (DAT/COUNTRY/USA) ----
static void printMessage(ByteView v, uint32_t s, uint32_t e, const char* tag) {
    int unk = 0;
    Message m = decodeMessage(v, s, e, "Junpei", &unk);
    std::printf("%s%s%s%s\n", tag, m.speaker.empty() ? "" : "[", m.speaker.c_str(), m.speaker.empty() ? "" : "] ");
    for (size_t p = 0; p < m.pages.size(); ++p) {
        std::string pg = m.pages[p];
        for (size_t i = 0; (i = pg.find('\n', i)) != std::string::npos;) pg.replace(i, 1, " / "), i += 3;
        std::printf("      p%zu: %s\n", p + 1, pg.c_str());
    }
    if (unk || !m.ok) std::printf("      (%d unknown codes%s)\n", unk, m.ok ? "" : ", not terminated");
}

static int dumpText(const fs::path& root, const std::string& what, int only) {
    const fs::path dir = root / "DAT" / "COUNTRY" / "USA";
    Bytes b;
    std::string err;
    char tag[64];
    if (what.rfind("DMG", 0) == 0) {
        EventBank ev;
        if (!readFile(dir / ("US" + what + ".BIN"), b) || !loadEventBank(b, ev, &err)) { std::printf("cannot read %s %s\n", what.c_str(), err.c_str()); return 1; }
        for (size_t k = 0; k < ev.blocks.size(); ++k) {
            if (only >= 0 && int(k) != only) continue;
            for (size_t m = 1; m < ev.blocks[k].size(); ++m) {
                std::snprintf(tag, sizeof tag, "%s:%zu:%zu ", what.c_str(), k, m);
                printMessage(b, ev.blocks[k][m].first, ev.blocks[k][m].second, tag);
            }
        }
        return 0;
    }
    if (what.rfind("TALK", 0) == 0) {
        TalkBank tb;
        if (!readFile(dir / ("US" + what + ".BIN"), b) || !loadTalkBank(b, tb, &err)) { std::printf("cannot read %s %s\n", what.c_str(), err.c_str()); return 1; }
        for (size_t k = 0; k < tb.msgs.size(); ++k) {
            if ((only >= 0 && int(k) != only) || tb.msgs[k].second - tb.msgs[k].first <= 4) continue;
            std::snprintf(tag, sizeof tag, "%s:0x%zx ", what.c_str(), k);
            printMessage(b, tb.msgs[k].first, tb.msgs[k].second, tag);
        }
        return 0;
    }
    // Stage: WSTAG actors with their show conditions and talk tables (lines from TALK00).
    Bytes pro, talk;
    StageOverlay ov;
    TalkBank tb;
    if (!readFile(root / "PRO" / ("WSTAG" + what.substr(1) + ".PRO"), pro) || !loadStageOverlay(pro, ov, &err)) { std::printf("cannot read overlay %s %s\n", what.c_str(), err.c_str()); return 1; }
    const bool haveTalk = readFile(dir / "USTALK00.BIN", talk) && loadTalkBank(talk, tb);
    auto conds = [](const std::vector<FlagCond>& c) {
        std::string s = "[";
        for (auto& f : c) { char t[24]; std::snprintf(t, sizeof t, "%s%04x=%u", s.size() > 1 ? " " : "", f.flag, unsigned(f.value)); s += t; }
        return s + "]";
    };
    for (const auto& a : ov.actors) {
        std::printf("actor char %u slot %u at %d,%d dir %d  show %s\n", a.charId, a.slot, a.x, a.y, a.dir, conds(a.show).c_str());
        for (const auto& t : a.talk) {
            std::printf("    if %s set %s -> talk 0x%x\n", conds(t.cond).c_str(), conds(t.set).c_str(), t.msg);
            if (haveTalk && t.msg < tb.msgs.size()) printMessage(talk, tb.msgs[t.msg].first, tb.msgs[t.msg].second, "      ");
        }
    }
    return 0;
}

// Decodes every USA message bank; a message that does not terminate or has unknown codes is a failure.
static int checkText(const fs::path& root) {
    const fs::path dir = root / "DAT" / "COUNTRY" / "USA";
    int failures = 0, banks = 0, msgs = 0;
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) { std::printf("text   : no DAT/COUNTRY/USA (skipped)\n"); return 0; }
    for (auto& e : fs::directory_iterator(dir)) {
        const std::string n = e.path().filename().string();
        const bool dmg = n.rfind("USDMG", 0) == 0 && n != "USDMPACK.BIN", talk = n.rfind("USTALK", 0) == 0;
        if (!dmg && !talk) continue;
        Bytes b;
        std::string err;
        std::vector<std::pair<uint32_t, uint32_t>> all;
        bool ok = readFile(e.path(), b);
        if (ok && dmg) {
            EventBank ev;
            ok = loadEventBank(b, ev, &err);
            for (auto& blk : ev.blocks) all.insert(all.end(), blk.begin(), blk.end());
        } else if (ok) {
            TalkBank tb;
            ok = loadTalkBank(b, tb, &err);
            all = tb.msgs;
        }
        int total = 0;
        const int bad = ok ? checkMessages(b, all, &total) : 0;
        if (!ok || bad) { std::printf("FAIL text %s: %s %d bad messages\n", n.c_str(), err.c_str(), bad); ++failures; }
        ++banks;
        msgs += total;
    }
    std::printf("text   : %d USA message banks, %d messages decoded\n", banks, msgs);
    return failures;
}

// ---- UI cell sheets (*CS) + texture packs (*TM). Pairs: same stem, plus the odd names. ----
static std::vector<std::pair<fs::path, fs::path>> uiPairs(const fs::path& root) {
    std::vector<std::pair<fs::path, fs::path>> out;
    std::error_code ec;
    for (auto& e : fs::recursive_directory_iterator(root / "DAT", ec)) {
        const std::string n = e.path().filename().string();
        if (n.size() == 12 && n.substr(6) == "CS.BIN" && n.rfind("P", 0) != 0) {
            fs::path tm = e.path().parent_path() / (n.substr(0, 6) + "TM.BIN");
            if (fs::exists(tm, ec)) out.push_back({e.path(), tm});
        }
    }
    out.push_back({root / "DAT/TEXT/JTWNDCST.BIN", root / "DAT/TEXT/JTWNDTIM.BIN"});
    out.push_back({root / "DAT/TRAINING/JTRGCAST.BIN", root / "DAT/TRAINING/JTRNGTIM.BIN"});
    out.push_back({root / "DAT/MODEL/PGFGTC00.BIN", root / "DAT/MODEL/PGFGTW00.BIN"});
    std::sort(out.begin(), out.end());
    return out;
}

static int checkUi(const fs::path& root) {
    int failures = 0, sheets = 0, parts = 0, drawn = 0, pieces = 0;
    for (auto& [csPath, tmPath] : uiPairs(root)) {
        Bytes cs, tm;
        if (!readFile(csPath, cs) || !readFile(tmPath, tm)) continue;
        std::vector<UiCellGroup> cg;
        std::vector<UiTexGroup> tg;
        std::string err;
        if (!loadCellSheet(cs, cg, &err) || !loadTexturePack(tm, tg, &err)) {
            std::printf("FAIL ui %s: %s\n", csPath.filename().string().c_str(), err.c_str());
            ++failures; continue;
        }
        ++sheets;
        for (size_t gi = 0; gi < cg.size(); ++gi) {
            const UiTexGroup* t = uiTexGroupFor(tg, gi);
            pieces += int(cg[gi].pieces.size());
            for (auto& p : cg[gi].parts) {
                ++parts;
                Image img;
                if (t && renderUiPart(*t, p, img)) ++drawn;
            }
        }
    }
    // Font mapping: every encoded character code must have a white glyph part in CMFONT.
    Bytes fcs;
    std::vector<UiCellGroup> fg;
    if (readFile(root / "DAT/COMMON/CMFONTCS.BIN", fcs) && loadCellSheet(fcs, fg) && !fg.empty()) {
        int missing = 0;
        for (int c = 4; c <= 0xe8; ++c)
            if (uiGlyphPartForCode(UiFont::White, uint8_t(c), false) >= int(fg[0].parts.size())) ++missing;
        for (const char* s = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz"; *s; ++s)
            if (uiGlyphPart(UiFont::Orange, *s) < 0 || uiGlyphPart(UiFont::Orange, *s) >= int(fg[0].parts.size())) ++missing;
        if (missing) { std::printf("FAIL ui font: %d codes without a glyph part\n", missing); ++failures; }
    }
    std::printf("ui     : %d cell sheets, %d pieces, %d parts (%d inside their texture pages)\n", sheets, pieces, parts, drawn);
    return failures;
}

static int dumpUi(const fs::path& root, const std::string& name, const fs::path& out, bool allParts) {
    for (auto& [csPath, tmPath] : uiPairs(root)) {
        if (csPath.filename().string().rfind(name, 0) != 0) continue;
        Bytes cs, tm;
        std::vector<UiCellGroup> cg;
        std::vector<UiTexGroup> tg;
        std::string err;
        if (!readFile(csPath, cs) || !readFile(tmPath, tm) || !loadCellSheet(cs, cg, &err) || !loadTexturePack(tm, tg, &err)) {
            std::printf("cannot load %s: %s\n", csPath.string().c_str(), err.c_str());
            return 1;
        }
        std::vector<Image> imgs;
        for (size_t gi = 0; gi < cg.size(); ++gi) {
            const UiTexGroup* t = uiTexGroupFor(tg, gi);
            if (!t) continue;
            const size_t n = allParts ? cg[gi].parts.size() : cg[gi].pieces.size();
            for (size_t i = 0; i < n; ++i) {
                Image img;
                int ox, oy;
                if (allParts) renderUiPart(*t, cg[gi].parts[i], img);
                else {
                    renderUiPiece(*t, cg[gi], int(i), img, ox, oy);
                    std::printf("  piece %zu.%zu (frame 0x%02x, flag %d): origin %d,%d size %dx%d\n", gi, i,
                                i < cg[gi].frameIds.size() ? cg[gi].frameIds[i] : 0xff, cg[gi].pieces[i].flag, ox, oy,
                                img.w, img.h);
                }
                if (img.w > 0 && img.h > 0) imgs.push_back(std::move(img));
            }
        }
        // shelf layout, 512 wide
        int x = 0, y = 0, rowH = 0, W = 512;
        std::vector<std::pair<int, int>> pos;
        for (auto& im : imgs) {
            W = std::max(W, im.w + 2);
            if (x + im.w + 2 > W) { x = 0; y += rowH + 2; rowH = 0; }
            pos.push_back({x, y});
            x += im.w + 2;
            rowH = std::max(rowH, im.h);
        }
        Image sheet;
        sheet.resize(W, y + rowH + 2, 0);
        for (size_t i = 0; i < imgs.size(); ++i)
            for (int yy = 0; yy < imgs[i].h; ++yy)
                for (int xx = 0; xx < imgs[i].w; ++xx)
                    sheet.px[size_t(pos[i].second + yy) * W + pos[i].first + xx] = imgs[i].px[size_t(yy) * imgs[i].w + xx];
        std::printf("%s: %zu images -> %s (scratch only, never commit)\n", csPath.filename().string().c_str(), imgs.size(),
                    out.string().c_str());
        return writeBmp(out, sheet) ? 0 : 1;
    }
    std::printf("no UI sheet starting with %s\n", name.c_str());
    return 1;
}

// Combined title packs (cells + textures in one file), see FORMATS.md "Title screen packs".
static const char* const kUiPacks[] = {"TTCOMMPK", "TLOGOUSA", "TLOGOJPN", "NAMEENT/NE_DATA_"};

static int checkUiPacks(const fs::path& root, int& packs, int& parts, int& drawn) {
    int failures = 0;
    for (const char* name : kUiPacks) {
        Bytes b;
        if (!readFile(std::strchr(name, 0x2f) ? root / "DAT" / (std::string(name) + ".BIN") : root / "DAT" / "SCREEN" / (std::string(name) + ".BIN"), b)) continue;
        UiPack pk;
        std::string err;
        if (!loadUiPack(b, pk, &err)) { std::printf("FAIL ui pack %s: %s\n", name, err.c_str()); ++failures; continue; }
        ++packs;
        for (size_t gi = 0; gi < pk.cells.size(); ++gi)
            for (auto& p : pk.cells[gi].parts) {
                ++parts;
                Image img;
                if (renderUiPart(pk.tex[size_t(pk.texFor[gi])], p, img)) ++drawn;
                else { std::printf("FAIL ui pack %s: group %zu part outside its pages\n", name, gi); ++failures; }
            }
    }
    return failures;
}

static int dumpPack(const fs::path& root, const std::string& name, const fs::path& out, bool allParts) {
    Bytes b;
    UiPack pk;
    std::string err;
    if (!readFile(name.find('/') != std::string::npos ? root / "DAT" / (name + ".BIN") : root / "DAT" / "SCREEN" / (name + ".BIN"), b) || !loadUiPack(b, pk, &err)) {
        std::printf("cannot load pack %s: %s\n", name.c_str(), err.c_str());
        return 1;
    }
    std::vector<Image> imgs;
    for (size_t gi = 0; gi < pk.cells.size(); ++gi) {
        const UiTexGroup& t = pk.tex[size_t(pk.texFor[gi])];
        const size_t n = allParts ? pk.cells[gi].parts.size() : pk.cells[gi].pieces.size();
        std::printf("group %zu -> tex %d: %zu parts, %zu pieces\n", gi, pk.texFor[gi], pk.cells[gi].parts.size(),
                    pk.cells[gi].pieces.size());
        for (size_t i = 0; i < n; ++i) {
            Image img;
            int ox, oy;
            if (allParts) renderUiPart(t, pk.cells[gi].parts[i], img);
            else {
                renderUiPiece(t, pk.cells[gi], int(i), img, ox, oy);
                std::printf("  piece %zu.%zu (frame 0x%02x, flag %d): origin %d,%d size %dx%d\n", gi, i,
                            i < pk.cells[gi].frameIds.size() ? pk.cells[gi].frameIds[i] : 0xff, pk.cells[gi].pieces[i].flag,
                            ox, oy, img.w, img.h);
            }
            if (img.w > 0 && img.h > 0) imgs.push_back(std::move(img));
        }
    }
    int x = 0, y = 0, rowH = 0, W = 512;
    std::vector<std::pair<int, int>> pos;
    for (auto& im : imgs) {
        W = std::max(W, im.w + 2);
        if (x + im.w + 2 > W) { x = 0; y += rowH + 2; rowH = 0; }
        pos.push_back({x, y});
        x += im.w + 2;
        rowH = std::max(rowH, im.h);
    }
    Image sheet;
    sheet.resize(W, y + rowH + 2, 0);
    for (size_t i = 0; i < imgs.size(); ++i)
        for (int yy = 0; yy < imgs[i].h; ++yy)
            for (int xx = 0; xx < imgs[i].w; ++xx)
                sheet.px[size_t(pos[i].second + yy) * W + pos[i].first + xx] = imgs[i].px[size_t(yy) * imgs[i].w + xx];
    std::printf("%s: %zu images -> %s (scratch only, never commit)\n", name.c_str(), imgs.size(), out.string().c_str());
    return writeBmp(out, sheet) ? 0 : 1;
}

static int dumpModel(const fs::path& root, const std::string& id, const fs::path& out, int onlyClip) {
    // "M005GREY" searches DAT/MODEL; "EFFECT/MEFT0024" searches another DAT folder.
    fs::path dir = root / "DAT" / "MODEL";
    std::string prefix = id;
    if (auto slash = id.find('/'); slash != std::string::npos) { dir = root / "DAT" / id.substr(0, slash); prefix = id.substr(slash + 1); }
    const bool fit = prefix.rfind("M", 0) != 0 || !std::isdigit(static_cast<unsigned char>(prefix.size() > 1 ? prefix[1] : 'x'));
    fs::path file;
    for (auto& e : fs::directory_iterator(dir))
        if (e.path().filename().string().rfind(prefix, 0) == 0) { file = e.path(); break; }
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
            rasterModel(m, tex, pl.pose(), sheet, k * cell, int(r) * cell, cell, scale, 0.9f, fit);
        }
    }
    return writeBmp(out, sheet) ? 0 : 1;
}

// Perspective software rasterizer (z-buffer, perspective-correct UV, near-plane clipping) used by
// the arena preview. PS1 axes: y is down, so the camera "up" is -y.
struct PCam {
    float eye[3], fwd[3], right[3], up[3], focal;
    int w, h;
    static PCam look(const float e[3], const float t[3], int w, int h, float fovDeg) {
        PCam c{};
        float f[3] = {t[0] - e[0], t[1] - e[1], t[2] - e[2]};
        float l = std::sqrt(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
        for (float& x : f) x /= l;
        const float wu[3] = {0, -1, 0};
        float r[3] = {f[1] * wu[2] - f[2] * wu[1], f[2] * wu[0] - f[0] * wu[2], f[0] * wu[1] - f[1] * wu[0]};
        l = std::sqrt(r[0] * r[0] + r[1] * r[1] + r[2] * r[2]);
        for (float& x : r) x /= l;
        const float u[3] = {r[1] * f[2] - r[2] * f[1], r[2] * f[0] - r[0] * f[2], r[0] * f[1] - r[1] * f[0]};
        for (int i = 0; i < 3; ++i) { c.eye[i] = e[i]; c.fwd[i] = f[i]; c.right[i] = r[i]; c.up[i] = u[i]; }
        c.w = w; c.h = h;
        c.focal = (h * 0.5f) / std::tan(fovDeg * 3.14159265f / 360.f);
        return c;
    }
};

static void rasterPerspective(const Model& m, const std::vector<Image>& tex, const std::vector<Mat34>& mats, const PCam& cam,
                              Image& dst, std::vector<float>& zb) {
    struct V { float x, y, z, u, v; };  // camera space + texel
    const float nearZ = 16.f;
    auto toCam = [&](float x, float y, float z) {
        const float d[3] = {x - cam.eye[0], y - cam.eye[1], z - cam.eye[2]};
        return V{d[0] * cam.right[0] + d[1] * cam.right[1] + d[2] * cam.right[2],
                 d[0] * cam.up[0] + d[1] * cam.up[1] + d[2] * cam.up[2],
                 d[0] * cam.fwd[0] + d[1] * cam.fwd[1] + d[2] * cam.fwd[2], 0, 0};
    };
    auto drawTri = [&](V a, V b, V c, const Image& t, int blend) {
        V in[3] = {a, b, c}, poly[4];
        int n = 0;
        for (int i = 0; i < 3; ++i) {  // clip against the near plane
            const V& p = in[i];
            const V& q = in[(i + 1) % 3];
            const bool pi = p.z >= nearZ, qi = q.z >= nearZ;
            if (pi) poly[n++] = p;
            if (pi != qi) {
                const float f = (nearZ - p.z) / (q.z - p.z);
                poly[n++] = {p.x + (q.x - p.x) * f, p.y + (q.y - p.y) * f, nearZ, p.u + (q.u - p.u) * f, p.v + (q.v - p.v) * f};
            }
        }
        if (n < 3) return;
        struct S { float x, y, iz, uz, vz; };
        S s[4];
        for (int i = 0; i < n; ++i) {
            const float iz = 1.f / poly[i].z;
            s[i] = {cam.w * 0.5f + poly[i].x * cam.focal * iz, cam.h * 0.5f - poly[i].y * cam.focal * iz, iz, poly[i].u * iz,
                    poly[i].v * iz};
        }
        for (int k = 1; k + 1 < n; ++k) {
            const S &A = s[0], &B = s[k], &C = s[k + 1];
            const float den = (B.y - C.y) * (A.x - C.x) + (C.x - B.x) * (A.y - C.y);
            if (std::fabs(den) < 1e-6f) continue;
            const int x0 = std::max(0, int(std::floor(std::min({A.x, B.x, C.x})))), x1 = std::min(cam.w - 1, int(std::ceil(std::max({A.x, B.x, C.x}))));
            const int y0 = std::max(0, int(std::floor(std::min({A.y, B.y, C.y})))), y1 = std::min(cam.h - 1, int(std::ceil(std::max({A.y, B.y, C.y}))));
            for (int y = y0; y <= y1; ++y)
                for (int x = x0; x <= x1; ++x) {
                    const float px = x + 0.5f, py = y + 0.5f;
                    const float l0 = ((B.y - C.y) * (px - C.x) + (C.x - B.x) * (py - C.y)) / den;
                    const float l1 = ((C.y - A.y) * (px - C.x) + (A.x - C.x) * (py - C.y)) / den;
                    const float l2 = 1 - l0 - l1;
                    if (l0 < 0 || l1 < 0 || l2 < 0) continue;
                    const float iz = l0 * A.iz + l1 * B.iz + l2 * C.iz;
                    float& zz = zb[size_t(y) * cam.w + x];
                    if (iz <= zz) continue;
                    const int u = int((l0 * A.uz + l1 * B.uz + l2 * C.uz) / iz) & 255;
                    const int v = int((l0 * A.vz + l1 * B.vz + l2 * C.vz) / iz) & 255;
                    const uint32_t col = t.at(u, v);
                    if (!(col >> 24)) continue;
                    uint32_t& d = dst.px[size_t(y) * dst.w + x];
                    if (blend == 0) { zz = iz; d = col; continue; }
                    if (blend == 3) continue;
                    uint32_t o = 0xff000000u;
                    for (int sh = 0; sh < 24; sh += 8) {
                        const int dc = int((d >> sh) & 255), sc = int((col >> sh) & 255);
                        const int r = blend == 1 ? (dc + sc) / 2 : blend == 4 ? dc + sc / 4 : dc + sc;
                        o |= uint32_t(std::min(255, r)) << sh;
                    }
                    d = o;
                }
        }
    };
    for (int pass = 0; pass < 2; ++pass)  // opaque first, then blended
        for (size_t i = 0; i < m.nodes.size(); ++i) {
            if (m.nodes[i].mesh < 0) continue;
            const ModelMesh& mesh = m.meshes[size_t(m.nodes[i].mesh)];
            std::vector<V> vs(mesh.verts.size());
            for (size_t k = 0; k < mesh.verts.size(); ++k) {
                float x, y, z;
                xform(mats[i], mesh.verts[k][0], mesh.verts[k][1], mesh.verts[k][2], x, y, z);
                vs[k] = toCam(x, y, z);
            }
            for (const ModelPrim& pr : mesh.prims) {
                if ((pr.blend != 0) != (pass == 1)) continue;
                V q[4];
                for (int k = 0; k < pr.count; ++k) { q[k] = vs[pr.v[k]]; q[k].u = pr.u[k] + 0.5f; q[k].v = pr.t[k] + 0.5f; }
                drawTri(q[0], q[1], q[2], tex[pr.clut], pr.blend);
                if (pr.count == 4) drawTri(q[1], q[3], q[2], tex[pr.clut], pr.blend);
            }
        }
}

// Renders a battle arena (DAT/EFFECT/MEFT10xx) from an orbit camera: yaw/pitch in degrees,
// distance as a multiple of the arena radius. Prints the node table for analysis.
static int dumpArena(const fs::path& root, const std::string& id, const fs::path& out, float yawDeg, float pitchDeg,
                     float distMul) {
    Bytes b;
    std::string name = id;
    if (name.size() == 4) name = "MEFT" + name;  // "1001" -> MEFT1001
    if (!readFile(root / "DAT" / "EFFECT" / (name + ".BIN"), b)) { std::printf("cannot read %s\n", name.c_str()); return 1; }
    Model m; std::string err;
    if (!loadModel(b, m, err)) { std::printf("%s: %s\n", name.c_str(), err.c_str()); return 1; }
    Pose pose;
    samplePose(m, 0.f, pose);
    std::vector<Mat34> mats;
    poseMatrices(m, pose, mats);
    float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f};
    size_t prims = 0;
    std::printf("%s: %zu nodes, %zu meshes, %zu pages, %zu cluts\n", name.c_str(), m.nodes.size(), m.meshes.size(), m.pages.size(), m.cluts.size());
    {
        Arena ar;
        std::string e2;
        const int num = std::atoi(name.c_str() + 4) - 1000;
        if (loadArena(b, num, ar, e2))
            std::printf("ground: %s, play y %.0f, centre (%.0f, %.0f), walk radius %.0f  [%s]\n", ar.flat ? "flat plane" : "floor grid",
                        ar.playY, ar.centerX, ar.centerZ, ar.walkRadius, arenaName(num));
    }
    for (size_t i = 0; i < m.nodes.size(); ++i) {
        const ModelTrack& tr = m.tracks[size_t(m.nodes[i].track)];
        float nl[3] = {1e30f, 1e30f, 1e30f}, nh[3] = {-1e30f, -1e30f, -1e30f};
        int np = 0, blendCnt[5] = {}, unlit = 0;
        if (m.nodes[i].mesh >= 0) {
            const ModelMesh& me = m.meshes[size_t(m.nodes[i].mesh)];
            np = int(me.prims.size());
            for (auto& p : me.prims) { blendCnt[std::min<int>(p.blend, 4)]++; unlit += !p.lit; }
            for (auto& v : me.verts) {
                float w[3];
                xform(mats[i], v[0], v[1], v[2], w[0], w[1], w[2]);
                for (int k = 0; k < 3; ++k) { nl[k] = std::min(nl[k], w[k]); nh[k] = std::max(nh[k], w[k]); }
            }
            for (int k = 0; k < 3; ++k) { lo[k] = std::min(lo[k], nl[k]); hi[k] = std::max(hi[k], nh[k]); }
        }
        prims += size_t(np);
        std::printf("  node %2zu parent %2d prims %4d unlit %4d blend[%d %d %d %d %d] keys p%zu r%zu s%zu pos0 (%d %d %d) rot0 (%d %d %d)",
                    i, m.nodes[i].parent, np, unlit, blendCnt[0], blendCnt[1], blendCnt[2], blendCnt[3], blendCnt[4],
                    tr.pos.size(), tr.rot.size(), tr.scl.size(), tr.pos[0].x, tr.pos[0].y, tr.pos[0].z, tr.rot[0].x,
                    tr.rot[0].y, tr.rot[0].z);
        if (np) std::printf(" box x[%.0f %.0f] y[%.0f %.0f] z[%.0f %.0f]", nl[0], nh[0], nl[1], nh[1], nl[2], nh[2]);
        std::printf("\n");
    }
    std::printf("  total prims %zu, bounds x[%.0f %.0f] y[%.0f %.0f] z[%.0f %.0f]\n", prims, lo[0], hi[0], lo[1], hi[1], lo[2], hi[2]);
    for (size_t c = 0; c < m.cluts.size(); ++c)
        std::printf("  clut %zu: page %d x %d y %d\n", c, m.cluts[c][0], m.cluts[c][1], m.cluts[c][2]);
    std::vector<Image> tex;
    for (size_t c = 0; c < m.cluts.size(); ++c) tex.push_back(m.textureFor(c));
    const float radius = std::max(hi[0] - lo[0], hi[2] - lo[2]) * 0.5f;
    const float tgt[3] = {0, 0, 0};
    const float yaw = yawDeg * 3.14159265f / 180.f, pitch = pitchDeg * 3.14159265f / 180.f, dist = radius * distMul;
    const float eye[3] = {std::sin(yaw) * std::cos(pitch) * dist, -std::sin(pitch) * dist, -std::cos(yaw) * std::cos(pitch) * dist};
    Image img;
    img.resize(640, 480, rgba(20, 20, 30));
    std::vector<float> zb(size_t(img.w) * img.h, 0.f);
    rasterPerspective(m, tex, mats, PCam::look(eye, tgt, img.w, img.h, 60.f), img, zb);
    return writeBmp(out, img) ? 0 : 1;
}

// Every effect model (DAT/EFFECT/MEFT*) in one sheet: 4 frames of clip 0 per row, 2 effects per row.
static int sheetEffects(const fs::path& root, const fs::path& out) {
    std::vector<fs::path> files;
    for (auto& e : fs::directory_iterator(root / "DAT" / "EFFECT"))
        if (e.path().filename().string().rfind("MEFT", 0) == 0) files.push_back(e.path());
    std::sort(files.begin(), files.end());
    const int cell = 80, per = 4, perRow = 2;
    struct Item { std::string name; Model m; };
    std::vector<Item> ok;
    for (auto& f : files) {
        Bytes b; Item it; std::string err;
        it.name = f.stem().string();
        if (!readFile(f, b) || !loadModel(b, it.m, err)) { std::printf("SKIP %s: %s\n", it.name.c_str(), err.c_str()); continue; }
        ok.push_back(std::move(it));
    }
    const int rows = int((ok.size() + perRow - 1) / perRow);
    Image sheet; sheet.resize(per * perRow * cell + (perRow - 1) * 8, rows * cell, rgba(24, 24, 32));
    for (size_t i = 0; i < ok.size(); ++i) {
        const Model& m = ok[i].m;
        const int r = int(i) / perRow, c0 = int(i % perRow) * (per * cell + 8);
        std::printf("row %3d %s: %s\n", r, i % perRow ? "right" : "left ", ok[i].name.c_str());
        std::vector<Image> tex;
        for (size_t c = 0; c < m.cluts.size(); ++c) tex.push_back(m.textureFor(c));
        int clip = 0;
        while (size_t(clip) < m.clips.size() && !m.clips[size_t(clip)].valid()) ++clip;
        ClipPlayer pl;
        pl.play(m, clip);
        const float len = size_t(clip) < m.clips.size() ? float(m.clips[size_t(clip)].length) : 1.f;
        for (int k = 0; k < per; ++k) {
            pl.update(m, k ? len / per : len * 0.1f);
            Pose pose = pl.pose();
            if (pose.size() != m.nodes.size()) samplePose(m, float(k) * 10.f, pose);
            rasterModel(m, tex, pose, sheet, c0 + k * cell, r * cell, cell, 1.f, 0.6f, true);
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
        if (!std::strcmp(argv[i], "--dump-field") && i + 2 < argc)
            return dumpField(root, argv[i + 1], argv[i + 2], i + 3 < argc && !std::strcmp(argv[i + 3], "--overlay"));
        if (!std::strcmp(argv[i], "--dump-sprite") && i + 2 < argc) return dumpSprite(root, argv[i + 1], argv[i + 2]);
        if (!std::strcmp(argv[i], "--sheet-effects") && i + 1 < argc) return sheetEffects(root, argv[i + 1]);
        if (!std::strcmp(argv[i], "--dump-arena") && i + 2 < argc)
            return dumpArena(root, argv[i + 1], argv[i + 2], i + 3 < argc ? float(std::atof(argv[i + 3])) : 30.f,
                             i + 4 < argc ? float(std::atof(argv[i + 4])) : 30.f, i + 5 < argc ? float(std::atof(argv[i + 5])) : 1.6f);
        if (!std::strcmp(argv[i], "--dump-ui") && i + 2 < argc)
            return dumpUi(root, argv[i + 1], argv[i + 2], i + 3 < argc && !std::strcmp(argv[i + 3], "parts"));
        if (!std::strcmp(argv[i], "--dump-pack") && i + 2 < argc)
            return dumpPack(root, argv[i + 1], argv[i + 2], i + 3 < argc && !std::strcmp(argv[i + 3], "parts"));
        if (!std::strcmp(argv[i], "--dump-text") && i + 1 < argc)
            return dumpText(root, argv[i + 1], i + 2 < argc ? int(std::strtol(argv[i + 2], nullptr, 0)) : -1);
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
    int overlaySkipped = 0, fieldData = 0, walkMaps = 0, trigMaps = 0, pieceSets = 0, overlays = 0, objects = 0, objNoPiece = 0, actors = 0;
    for (auto& e : fs::directory_iterator(root / "DAT" / "Z_STAGE")) {
        Bytes b; StageMap m; std::string err;
        if (!readFile(e.path(), b) || !loadStagePack(b, m, &err)) {
            std::printf("FAIL stage %s: %s\n", e.path().filename().string().c_str(), err.c_str());
            ++failures; continue;
        }
        ++stages; chunks += m.chunksDecoded; badChunks += m.chunksFailed;
        if (m.chunksFailed) std::printf("WARN stage %s: %d chunk(s) failed\n", e.path().filename().string().c_str(), m.chunksFailed);
        // FIELD/STAGE MASK+TMPK and the PRO/WSTAG overlay of the same stage.
        const std::string id = e.path().filename().string().substr(0, 4);
        FieldStage f; StageOverlay ov;
        const bool hasTmpk = fs::exists(root / "DAT" / "FIELD" / "STAGE" / (id + "TMPK.BIN"));
        if (loadFieldAll(root, id, m.gridW, m.gridH, f, ov, err)) ++fieldData;
        else if (hasTmpk) { std::printf("FAIL field %s: %s\n", id.c_str(), err.c_str()); ++failures; }
        walkMaps += f.hasWalk; trigMaps += f.hasTrigger; pieceSets += !f.pieces.empty();
        if (fs::exists(root / "PRO" / ("WSTAG" + id.substr(1) + ".PRO"))) {
            // A few overlays use another layout (e.g. WSTAG260 is not word aligned): reported, not fatal.
            if (!ov.ok) { std::printf("SKIP overlay WSTAG%s: no stage descriptor found\n", id.substr(1).c_str()); ++overlaySkipped; }
            else ++overlays;
        }
        objects += int(ov.objects.size()); actors += int(ov.actors.size());
        for (const auto& o : ov.objects)
            if (f.pieceForFrame(o.mode == 1 ? o.loopFrom : o.frame) < 0) ++objNoPiece;
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
    std::printf("field  : %d MASK/TMPK sets (%d walk maps, %d trigger maps, %d piece tables)\n", fieldData, walkMaps, trigMaps, pieceSets);
    std::printf("overlay: %d WSTAG parsed (%d other layout), %d scene objects (%d without piece), %d event actors\n",
                overlays, overlaySkipped, objects, objNoPiece, actors);
    failures += checkText(root);
    failures += checkUi(root);
    {
        int packs = 0, packParts = 0, packDrawn = 0;
        failures += checkUiPacks(root, packs, packParts, packDrawn);
        std::printf("uipack : %d title packs, %d parts (%d inside their texture pages)\n", packs, packParts, packDrawn);
    }
    int talkActors = 0, talkEntries = 0;
    for (auto& e : fs::directory_iterator(root / "PRO")) {
        const std::string n = e.path().filename().string();
        if (n.rfind("WSTAG", 0) != 0) continue;
        Bytes b; StageOverlay ov;
        if (!readFile(e.path(), b) || !loadStageOverlay(b, ov)) continue;
        for (const auto& a : ov.actors)
            if (!a.talk.empty()) { ++talkActors; talkEntries += int(a.talk.size()); }
    }
    std::printf("talk   : %d event actors with talk tables, %d talk entries\n", talkActors, talkEntries);
    // 3D battle arenas DAT/EFFECT/MEFT1001..1055: every one must load (or be a known other layout,
    // which falls back to another arena) and have somewhere to stand.
    int arenas = 0, arenaSkipped = 0, arenaFlat = 0;
    for (int n = 1; n <= kArenaCount; ++n) {
        Bytes b; Arena a; std::string err;
        const fs::path f = root / "DAT" / "EFFECT" / (arenaFileName(n) + ".BIN");
        if (!readFile(f, b)) { std::printf("FAIL arena %s: missing\n", arenaFileName(n).c_str()); ++failures; continue; }
        if (!loadArena(b, n, a, err)) {
            std::printf("SKIP arena %s: %s (falls back to %s)\n", arenaFileName(n).c_str(), err.c_str(), arenaFileName(arenaFallback(n)).c_str());
            ++arenaSkipped; continue;
        }
        if (a.walkRadius < 3000.f || !a.walkable(0, 0)) { std::printf("FAIL arena %s: no floor at the centre\n", arenaFileName(n).c_str()); ++failures; }
        ++arenas;
        arenaFlat += a.flat;
    }
    std::printf("arenas : %d loaded (%d without floor model: flat), %d other layout (fallback)\n", arenas, arenaFlat, arenaSkipped);
    std::printf("%s (%d failures)\n", failures ? "FAILED" : "OK", failures);
    return failures ? 1 : 0;
}
