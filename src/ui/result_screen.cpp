#include "ui/result_screen.h"

#include <algorithm>
#include <cmath>

#include "ui/ui_draw.h"

namespace dw3::ui {

namespace {

// STFRPTCS frame ids. See docs/FORMATS.md "Fight report sheet".
constexpr int kBackground = 0x1e;   // 48x48 checker tiles covering -96..336 x -96..240
constexpr int kTitleBar = 0x1d;     // 320x38
constexpr int kPartnerFrame = 0x1f; // portrait box with diamond stand + name / level tabs (110x42)
constexpr int kNamePlate = 0x20;    // 172x18
constexpr int kInfoPlate = 0x21;    // 108x21
constexpr int kSelectFrame = 0x24;  // yellow highlight frame (portrait + tab)

// Original message indices (USFGTRPT list, see ui_assets.h).
constexpr int kMsgExp = 0, kMsgLevelGained = 2, kMsgNameLevel = 5, kMsgBits = 8, kMsgItem = 9;

// Fallback English (same wording) when USFGTRPT is missing.
const char* fallback(int msg) {
    switch (msg) {
        case kMsgExp: return "EXP gained:\n%1 EXP";
        case kMsgLevelGained: return "Level gained!";
        case kMsgNameLevel: return "%1\nLevel %2!";
        case kMsgBits: return "BIT gained:\n%1 BIT";
        case kMsgItem: return "Item gained:\n%1";
    }
    return "";
}

void drawMessage(float x, float y, int msg, const std::vector<std::string>& args) {
    const auto& b = assets().reportText(msg);
    if (!b.empty()) {
        drawEncoded(x, y, b.data(), b.size(), kWhite, UiFont::White, args);
        return;
    }
    std::string s = fallback(msg);
    for (size_t i = 0; i < args.size(); ++i) {
        const std::string key = "%" + std::to_string(i + 1);
        const size_t p = s.find(key);
        if (p != std::string::npos) s.replace(p, key.size(), args[i]);
    }
    drawText(x, y, s);
}

}  // namespace

void ResultScreen::start(int expGained, int bits, int oldLevel, int newLevel, const std::vector<StatDelta>& statDeltas,
                         const std::string& partnerName, int portraitFrame, const std::string& item) {
    exp_ = std::max(0, expGained);
    bits_ = std::max(0, bits);
    oldLv_ = oldLevel;
    newLv_ = std::max(oldLevel, newLevel);
    stats_ = statDeltas;
    name_ = partnerName;
    portrait_ = portraitFrame;
    item_ = item;
    pages_.clear();
    pages_.push_back(Page::Exp);
    if (bits_ > 0) pages_.push_back(Page::Bits);
    if (!item_.empty()) pages_.push_back(Page::Item);
    if (newLv_ > oldLv_) pages_.push_back(Page::LevelUp);
    page_ = 0;
    ticks_ = pageTicks_ = 0;
    count_ = 0;
    active_ = true;
    done_ = false;
}

void ResultScreen::jumpTo(Page p) {
    for (size_t i = 0; i < pages_.size(); ++i)
        if (pages_[i] == p) { page_ = i; pageTicks_ = 0; count_ = 1.f; }
}

void ResultScreen::update(bool confirm) {
    if (!active_) return;
    ++ticks_;
    ++pageTicks_;
    if (count_ < 1.f) count_ = std::min(1.f, count_ + 1.f / 60.f);
    if (!confirm || pageTicks_ < 6) return;
    if (count_ < 1.f) { count_ = 1.f; return; }  // first press finishes the count-up
    if (page_ + 1 < pages_.size()) {
        ++page_;
        pageTicks_ = 0;
        count_ = 0;
    } else {
        active_ = false;
        done_ = true;
    }
}

void ResultScreen::render() const {
    if (!ready() || pages_.empty()) return;
    const Page pg = pages_[std::min(page_, pages_.size() - 1)];

    // ---- background: original checker tiles, slow diagonal scroll (96 px period = two tiles) ----
    const float s = float((ticks_ / 2) % 96);
    drawPiece(Sheet::Report, kBackground, s, s);  // the piece spans -96..336 x -96..240: always covers the screen

    // ---- title bar ----
    drawPiece(Sheet::Report, kTitleBar, 0, 14);
    drawText(16, 25, "EXP", kWhite, UiFont::Orange);

    // ---- partner frame: portrait on the diamond stand, name tab, level tab ----
    const float fx = 24, fy = 64;
    drawPiece(Sheet::Report, kPartnerFrame, fx, fy);
    if (portrait_ >= 0) drawPiece(Sheet::Report, portrait_, fx + 3, fy + 1);
    if (!name_.empty()) drawText(fx + 41, fy + 2, name_);
    const bool lvUp = pg == Page::LevelUp;
    const int lvShown = lvUp ? newLv_ : oldLv_;
    drawText(fx + 42, fy + 18, "LV", kWhite, UiFont::Small);
    drawTextRight(fx + 74, fy + 18, std::to_string(lvShown), lvUp ? kYellow : kWhite, UiFont::Small);
    if (lvUp && (ticks_ / 8) % 2 == 0) drawPiece(Sheet::Report, kSelectFrame, fx - 1, fy - 1);

    // ---- right column: EXP / BIT plates ----
    const float px = 140, py = 64;
    drawPiece(Sheet::Report, kNamePlate, px, py);
    drawText(px + 8, py + 3, "EXP", kWhite, UiFont::Small);
    const int expShown = pg == Page::Exp ? int(std::lround(float(exp_) * count_)) : exp_;
    drawTextRight(px + 132, py + 4, std::to_string(expShown), kWhite, UiFont::Small);
    drawPiece(Sheet::Report, kInfoPlate, px, py + 22);
    drawText(px + 8, py + 28, "BIT", kWhite, UiFont::Small);
    const int bitShown = pg == Page::Bits ? int(std::lround(float(bits_) * count_)) : (page_ > 0 ? bits_ : 0);
    drawTextRight(px + 98, py + 28, std::to_string(bitShown), kWhite, UiFont::Small);
    // EXP gauge under the frame: fills during the count-up, wraps on a level up
    {
        float k = pg == Page::Exp ? count_ : 1.f;
        if (newLv_ > oldLv_ && pg == Page::Exp) k = std::fmod(count_ * 1.6f, 1.f);
        drawBar(fx + 42, fy + 33, 62, 3, k, BarColor::Orange);
    }

    // ---- level-up: stat increases in an original menu window ----
    if (lvUp && !stats_.empty()) {
        const float wx = 140, wy = 104, ww = 172;
        const float wh = 10.f + 13.f * float((stats_.size() + 1) / 2);
        drawWindow(wx, wy, ww, wh);
        for (size_t i = 0; i < stats_.size(); ++i) {
            const float cx = wx + 18 + float(i % 2) * 78, cy = wy + 5 + float(i / 2) * 13;
            drawText(cx, cy, stats_[i].first);
            drawTextRight(cx + 66, cy, "+" + std::to_string(stats_[i].second), kYellow);
        }
    }

    // ---- message window with the original fight-report text ----
    const float mx = 8, my = 172, mw = 304, mh = 58;
    drawWindow(mx, my, mw, mh);
    switch (pg) {
        case Page::Exp: drawMessage(mx + 20, my + 9, kMsgExp, {std::to_string(exp_)}); break;
        case Page::Bits: drawMessage(mx + 20, my + 9, kMsgBits, {std::to_string(bits_)}); break;
        case Page::Item: drawMessage(mx + 20, my + 9, kMsgItem, {item_}); break;
        case Page::LevelUp:
            if (pageTicks_ < 50 && count_ < 1.f) drawMessage(mx + 20, my + 9, kMsgLevelGained, {});
            else drawMessage(mx + 20, my + 9, kMsgNameLevel, {name_.empty() ? std::string("Partner") : name_, std::to_string(newLv_)});
            break;
    }
    if (count_ >= 1.f) drawPrompt(mx + mw - 22, my + mh - 22, ticks_);
}

}  // namespace dw3::ui
