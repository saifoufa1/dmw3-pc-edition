#include "ui/pack_art.h"

#include "core/bytes.h"

namespace dw3::ui {

bool PackArt::loadPack(const std::filesystem::path& file, std::string* err) {
    release();
    Bytes b;
    UiPack pk;
    if (!readFile(file, b)) {
        if (err) *err = "cannot read " + file.string();
        return false;
    }
    if (!loadUiPack(b, pk, err)) return false;
    tex_ = std::move(pk.tex);
    for (size_t i = 0; i < pk.cells.size(); ++i) {
        Group g;
        g.cells = std::move(pk.cells[i]);
        g.tex = pk.texFor[i];
        groups_.push_back(std::move(g));
    }
    return true;
}

bool PackArt::loadSheet(const std::filesystem::path& cs, const std::filesystem::path& tm, std::string* err) {
    release();
    Bytes c, t;
    std::vector<UiCellGroup> cells;
    if (!readFile(cs, c) || !readFile(tm, t)) {
        if (err) *err = "cannot read " + cs.string();
        return false;
    }
    if (!loadCellSheet(c, cells, err) || !loadTexturePack(t, tex_, err)) return false;
    for (size_t i = 0; i < cells.size(); ++i) {
        const UiTexGroup* tg = uiTexGroupFor(tex_, i);
        if (!tg) continue;
        Group g;
        g.cells = std::move(cells[i]);
        g.tex = int(tg - tex_.data());
        groups_.push_back(std::move(g));
    }
    return !groups_.empty();
}

void PackArt::release() {
    for (auto& g : groups_) {
        for (auto& t : g.pieces) gfx::destroy(t.tex);
        for (auto& t : g.parts) gfx::destroy(t.tex);
    }
    groups_.clear();
    tex_.clear();
}

int PackArt::pieceCount(int group) const {
    return group >= 0 && group < int(groups_.size()) ? int(groups_[size_t(group)].cells.pieces.size()) : 0;
}

int PackArt::frameOf(int group, int i) const {
    if (group < 0 || group >= int(groups_.size())) return -1;
    const auto& ids = groups_[size_t(group)].cells.frameIds;
    return i >= 0 && i < int(ids.size()) ? ids[size_t(i)] : -1;
}

PackArt::Tex* PackArt::piece(int group, int frame) {
    if (group < 0 || group >= int(groups_.size())) return nullptr;
    Group& g = groups_[size_t(group)];
    const int pi = g.cells.pieceForFrame(frame);
    if (pi < 0) return nullptr;
    if (g.pieces.size() != g.cells.pieces.size()) g.pieces.resize(g.cells.pieces.size());
    Tex& t = g.pieces[size_t(pi)];
    if (!t.tried) {
        t.tried = true;
        Image img;
        if (renderUiPiece(tex_[size_t(g.tex)], g.cells, pi, img, t.ox, t.oy) && img.w > 0) t.tex = gfx::upload(img);
    }
    return t.tex.id ? &t : nullptr;
}

bool PackArt::draw(int group, int frame, float x, float y, uint32_t color, Blend blend) {
    Tex* t = piece(group, frame);
    if (!t) return false;
    const Group& g = groups_[size_t(group)];
    const int flag = g.cells.pieces[size_t(g.cells.pieceForFrame(frame))].flag;
    const bool add = blend == Blend::Add || (blend == Blend::Auto && (flag == 2 || flag == 3));
    gfx::drawSprite(t->tex, x + float(t->ox), y + float(t->oy), false, 1.f, color, add);
    return true;
}

bool PackArt::drawPart(int group, int part, float x, float y, uint32_t color, bool additive) {
    if (group < 0 || group >= int(groups_.size())) return false;
    Group& g = groups_[size_t(group)];
    if (part < 0 || part >= int(g.cells.parts.size())) return false;
    if (g.parts.size() != g.cells.parts.size()) g.parts.resize(g.cells.parts.size());
    Tex& t = g.parts[size_t(part)];
    if (!t.tried) {
        t.tried = true;
        Image img;
        if (renderUiPart(tex_[size_t(g.tex)], g.cells.parts[size_t(part)], img)) t.tex = gfx::upload(img);
    }
    if (!t.tex.id) return false;
    gfx::drawSprite(t.tex, x, y, false, 1.f, color, additive);
    return true;
}

bool PackArt::bounds(int group, int frame, int& x0, int& y0, int& w, int& h) {
    Tex* t = piece(group, frame);
    if (!t) return false;
    x0 = t->ox;
    y0 = t->oy;
    w = t->tex.w;
    h = t->tex.h;
    return true;
}

}  // namespace dw3::ui
