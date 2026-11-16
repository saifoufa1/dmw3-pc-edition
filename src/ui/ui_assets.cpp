#include "ui/ui_assets.h"

#include <windows.h>
#include <GL/gl.h>

#include <algorithm>

#include "core/bytes.h"

#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif

namespace dw3::ui {

namespace fs = std::filesystem;

Assets& assets() {
    static Assets a;
    return a;
}

namespace {

struct SheetFiles {
    const char* cs;
    const char* tm;
};
// Cell sheet group 0 is used for all three; texture group = uiTexGroupFor(.., 0).
constexpr SheetFiles kFiles[kSheetCount] = {
    {"DAT/COMMON/CMFONTCS.BIN", "DAT/COMMON/CMFONTTM.BIN"},
    {"DAT/MODEL/PGFGTC00.BIN", "DAT/MODEL/PGFGTW00.BIN"},
    {"DAT/SCREEN/STFRPTCS.BIN", "DAT/SCREEN/STFRPTTM.BIN"},
};

bool loadSheet(const fs::path& root, int i, SheetData& sd, std::string& why) {
    Bytes cs, tm;
    if (!readFile(root / kFiles[i].cs, cs) || !readFile(root / kFiles[i].tm, tm)) {
        why = std::string("missing ") + kFiles[i].cs;
        return false;
    }
    std::vector<UiCellGroup> cg;
    std::vector<UiTexGroup> tg;
    std::string e;
    if (!loadCellSheet(ByteView(cs), cg, &e) || cg.empty() || cg[0].empty()) { why = std::string(kFiles[i].cs) + ": " + e; return false; }
    if (!loadTexturePack(ByteView(tm), tg, &e)) { why = std::string(kFiles[i].tm) + ": " + e; return false; }
    sd.cells = std::move(cg[0]);
    if (const UiTexGroup* g = uiTexGroupFor(tg, 0)) sd.tex = *g;
    if (sd.tex.empty()) { why = std::string(kFiles[i].tm) + ": no pages"; return false; }
    return true;
}

// Message bank with one offset list: u32 at 0 -> u32 absolute string offsets (0-terminated list).
void loadReportText(const fs::path& root, std::vector<std::vector<uint8_t>>& out) {
    out.clear();
    Bytes b;
    if (!readFile(root / "DAT/COUNTRY/USA/USFGTRPT.BIN", b)) return;
    ByteView v(b);
    uint32_t list;
    if (!v.u32(0, list)) return;
    for (size_t k = 0; k < 32; ++k) {
        uint32_t off;
        if (!v.u32(list + k * 4, off) || off == 0 || off >= v.size) break;
        std::vector<uint8_t> s;
        size_t i = off;
        while (i < v.size) {
            const uint8_t c = v.data[i];
            if (c == 0) break;
            size_t n = 1;
            if (c == 1) n = 2;
            else if (c == 2) {
                if (i + 1 >= v.size || v.data[i + 1] == 0) break;
                n = v.data[i + 1] == 5 ? 3 : 2;
            }
            for (size_t j = 0; j < n && i + j < v.size; ++j) s.push_back(v.data[i + j]);
            i += n;
        }
        out.push_back(std::move(s));
    }
}

}  // namespace

const std::vector<uint8_t>& Assets::reportText(int i) const {
    static const std::vector<uint8_t> kEmpty;
    return i >= 0 && i < int(reportText_.size()) ? reportText_[size_t(i)] : kEmpty;
}

bool Assets::load(const fs::path& root, std::string* err) {
    release();
    loaded_ = false;
    std::string firstErr;
    for (int i = 0; i < kSheetCount; ++i) {
        sheets_[i] = {};
        std::string why;
        sheets_[i].ok = loadSheet(root, i, sheets_[i], why);
        if (!sheets_[i].ok && firstErr.empty()) firstErr = why;
    }
    if (!sheets_[0].ok) {
        if (err) *err = firstErr;
        return false;
    }

    // ---- render every part + the extra window images, then shelf-pack them into one atlas ----
    struct Item {
        Image img;
        AtlasRect* dst;
    };
    std::vector<Item> items;
    for (auto& sd : sheets_) {
        if (!sd.ok) continue;
        sd.partRect.assign(sd.cells.parts.size(), {});
        for (size_t p = 0; p < sd.cells.parts.size(); ++p) {
            Item it;
            if (!renderUiPart(sd.tex, sd.cells.parts[p], it.img) || it.img.w == 0 || it.img.h == 0) continue;
            it.dst = &sd.partRect[p];
            items.push_back(std::move(it));
        }
    }
    auto addPiece = [&](Sheet s, int frame, Extra e) {
        const SheetData& sd = sheets_[int(s)];
        extras_[int(e)] = {};
        if (!sd.ok) return;
        Item it;
        int ox, oy;
        if (!renderUiPiece(sd.tex, sd.cells, sd.cells.pieceForFrame(frame), it.img, ox, oy) || it.img.w == 0) return;
        it.dst = &extras_[int(e)];
        items.push_back(std::move(it));
    };
    addPiece(Sheet::Font, 0x45, Extra::MenuWindow);     // 320x50 lavender-edged message window
    addPiece(Sheet::Fight, 0x25, Extra::BattleWindow);  // 136x105 cyan-edged battle command window

    std::vector<size_t> order(items.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return items[a].img.h > items[b].img.h; });
    const int W = 1024;
    int x = 0, y = 0, rowH = 0;
    for (size_t k : order) {
        Image& im = items[k].img;
        if (x + im.w + 1 > W) { x = 0; y += rowH + 1; rowH = 0; }
        *items[k].dst = {x, y, im.w, im.h};
        x += im.w + 1;
        rowH = std::max(rowH, im.h);
    }
    int H = 1;
    while (H < y + rowH + 1) H <<= 1;
    atlasW_ = W;
    atlasH_ = H;
    atlas_.resize(W, H, 0);
    for (auto& it : items) {
        const AtlasRect& r = *it.dst;
        for (int yy = 0; yy < r.h; ++yy)
            std::copy(it.img.px.begin() + size_t(yy) * r.w, it.img.px.begin() + size_t(yy + 1) * r.w,
                      atlas_.px.begin() + size_t(r.y + yy) * W + r.x);
    }

    // ---- bar colours: battle sheet CLUT rows 252..255, words 50 (dark) / 51 (light) ----
    static const uint32_t kFallback[4][2] = {{rgba(0, 184, 197), rgba(0, 238, 247)}, {rgba(115, 180, 49), rgba(189, 255, 98)},
                                             {rgba(156, 16, 0), rgba(156, 24, 0)}, {rgba(164, 90, 0), rgba(205, 148, 0)}};
    for (int c = 0; c < 4; ++c)
        for (int l = 0; l < 2; ++l) {
            uint32_t col = 0;
            if (sheets_[1].ok) col = ps1Color(sheets_[1].tex.word(50 + l, 252 + c));
            bar_[c][l] = col ? col : kFallback[c][l];
        }

    loadReportText(root, reportText_);
    loaded_ = true;
    if (err && !firstErr.empty()) *err = firstErr;  // partial load: font ok, another sheet missing
    return true;
}

bool Assets::upload() {
    if (!loaded_) return false;
    if (texId_) return true;
    GLuint id = 0;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, atlasW_, atlasH_, 0, GL_RGBA, GL_UNSIGNED_BYTE, atlas_.px.data());
    texId_ = id;
    return true;
}

void Assets::release() {
    if (texId_) {
        GLuint id = texId_;
        glDeleteTextures(1, &id);
        texId_ = 0;
    }
}

}  // namespace dw3::ui
