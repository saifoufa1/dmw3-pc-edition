#include "game/dialogue.h"

#include <algorithm>

#include "render/gl_renderer.h"
#include "ui/ui_draw.h"

namespace dw3 {

// ---------------------------------------------------------------- default skin (until src/ui lands)

void DialogueSkin::window(int x, int y, int w, int h) {
    gfx::fillRect(float(x), float(y), float(w), float(h), rgba(16, 24, 56, 228));
    gfx::fillRect(float(x), float(y), float(w), 2.f, rgba(150, 190, 255, 255));
    gfx::fillRect(float(x), float(y + h - 2), float(w), 2.f, rgba(150, 190, 255, 255));
    gfx::fillRect(float(x), float(y), 2.f, float(h), rgba(150, 190, 255, 255));
    gfx::fillRect(float(x + w - 2), float(y), 2.f, float(h), rgba(150, 190, 255, 255));
}

void DialogueSkin::nameTab(int x, int y, int w, int h) {
    gfx::fillRect(float(x), float(y), float(w), float(h), rgba(40, 70, 140, 240));
    gfx::fillRect(float(x), float(y), float(w), 2.f, rgba(150, 190, 255, 255));
}

void DialogueSkin::text(int x, int y, const std::string& s, uint32_t c) { gfx::drawText(x, y + 12, s.c_str(), c); }
int DialogueSkin::textWidth(const std::string& s) { return int(s.size()) * gfx::kCharW; }
int DialogueSkin::lineHeight() { return 19; }

// ---------------------------------------------------------------- logic

Dialogue::Dialogue() : skin_(std::make_unique<DialogueSkin>()) {}

void Dialogue::open(const std::string& speaker, const std::vector<std::string>& pages) {
    speaker_ = speaker;
    pages_ = pages.empty() ? std::vector<std::string>{"..."} : pages;
    options_.clear();
    page_ = 0;
    reveal_ = 0;
    choice_ = 0;
    active_ = true;
}

void Dialogue::openChoice(const std::string& speaker, const std::vector<std::string>& pages,
                          const std::vector<std::string>& options, int defaultIndex) {
    open(speaker, pages);
    options_ = options;
    choice_ = std::clamp(defaultIndex, 0, std::max(0, int(options.size()) - 1));
}

void Dialogue::update(const Input& in) {
    if (!active_) return;
    ++blink_;
    if (!pageDone()) {
        reveal_ += in.run ? 4 : 1;  // hold Shift to fast-forward
        if (in.confirmP) reveal_ = int(pages_[page_].size());
        return;
    }
    const bool last = page_ + 1 >= pages_.size();
    if (last && !options_.empty()) {
        static int repeat = 0;
        const int n = int(options_.size());
        const bool up = in.up, down = in.down;
        if (up || down) {
            if (repeat == 0) choice_ = (choice_ + (up ? n - 1 : 1)) % n;
            repeat = (repeat + 1) % 12;
        } else {
            repeat = 0;
        }
        if (in.confirmP) active_ = false;
        return;
    }
    if (in.confirmP) {
        if (last) active_ = false;
        else { ++page_; reveal_ = 0; }
    }
}

// Original DW3 message window + font (virtual 320x240 coordinates).
void Dialogue::renderOriginal() {
    constexpr float x0 = 8, y0 = 170, w = 304, h = 62;
    ui::drawWindow(x0, y0, w, h);
    if (!speaker_.empty()) {
        const float tw = float(ui::measureText(speaker_)) + 34.f;
        ui::drawWindow(x0 + 6, y0 - 18, std::max(24.f, tw), 24);
        ui::drawText(x0 + 21, y0 - 12, speaker_, rgba(255, 220, 120));
    }
    const std::string& pg = pages_[page_];
    const std::string shown = pg.substr(0, size_t(std::clamp(reveal_, 0, int(pg.size()))));
    ui::drawText(x0 + 18, y0 + 9, shown);
    const bool last = page_ + 1 >= pages_.size();
    if (pageDone() && last && !options_.empty()) {
        int ow = 0;
        for (auto& o : options_) ow = std::max(ow, ui::measureText(o));
        const float bw = float(ow) + 30.f, lh = float(ui::lineHeight(UiFont::White)) + 2.f;
        const float bh = float(options_.size()) * lh + 14.f;
        const float bx = x0 + w - bw - 4, by = y0 - bh - 4;
        ui::drawWindow(bx, by, bw, bh);
        for (size_t i = 0; i < options_.size(); ++i) {
            const bool sel = int(i) == choice_;
            if (sel) ui::drawCursor(bx + 6, by + 7 + float(i) * lh, blink_);
            ui::drawText(bx + 18, by + 7 + float(i) * lh, options_[i], sel ? rgba(255, 230, 90) : ui::kWhite);
        }
    } else if (pageDone()) {
        ui::drawPrompt(x0 + w - 16, y0 + h - 14, blink_);
    }
}

void Dialogue::renderHud(int ww, int wh) {
    if (!active_) return;
    if (ui::ready()) {
        gfx::beginVirtual();
        renderOriginal();
        gfx::beginHud(ww, wh);
        return;
    }
    DialogueSkin& s = *skin_;
    int x0, y0, x1, y1;
    gfx::toWindow(10, 164, x0, y0);
    gfx::toWindow(310, 232, x1, y1);
    const int lh = s.lineHeight(), pad = std::max(10, (y1 - y0) / 10);
    s.window(x0, y0, x1 - x0, y1 - y0);
    if (!speaker_.empty()) {
        const int tw = s.textWidth(speaker_) + 24, th = lh + 8;
        s.nameTab(x0 + pad, y0 - th + 2, tw, th);
        s.text(x0 + pad + 12, y0 - th + 6, speaker_, rgba(255, 220, 120));
    }
    // Typewriter: draw the revealed part of the page, line by line.
    const std::string& pg = pages_[page_];
    const std::string shown = pg.substr(0, size_t(std::clamp(reveal_, 0, int(pg.size()))));
    int ty = y0 + pad + (speaker_.empty() ? 0 : 4);
    size_t st = 0;
    while (st <= shown.size()) {
        size_t nl = shown.find('\n', st);
        s.text(x0 + pad + 6, ty, shown.substr(st, nl == std::string::npos ? std::string::npos : nl - st), rgba(255, 255, 255));
        ty += lh + 4;
        if (nl == std::string::npos) break;
        st = nl + 1;
    }
    const bool last = page_ + 1 >= pages_.size();
    if (pageDone() && last && !options_.empty()) {
        int w = 0;
        for (auto& o : options_) w = std::max(w, s.textWidth(o));
        w += 44;
        const int h = int(options_.size()) * (lh + 6) + 16;
        const int bx = x1 - w - pad, by = y0 - h - 8;
        s.window(bx, by, w, h);
        for (size_t i = 0; i < options_.size(); ++i) {
            const bool sel = int(i) == choice_;
            s.text(bx + 12, by + 8 + int(i) * (lh + 6), (sel ? "> " : "  ") + options_[i],
                   sel ? rgba(255, 230, 90) : rgba(200, 210, 230));
        }
    } else if (pageDone() && (blink_ / 20) % 2 == 0) {
        s.text(x1 - pad - 2 * gfx::kCharW - 4, y1 - pad - lh, last ? "[]" : "\\/", rgba(160, 210, 255));
    }
}

}  // namespace dw3
