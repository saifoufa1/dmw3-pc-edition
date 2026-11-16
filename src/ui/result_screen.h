// Post-battle report ("fight report": EXP / BIT / item gained, level-up with stat increases), drawn with the
// ORIGINAL graphics of DAT/SCREEN/STFRPTCS + STFRPTTM (tiled background, title bar, partner portrait frame,
// plates), the original font and the original English messages of DAT/COUNTRY/USA/USFGTRPT.BIN
// ("EXP gained:\n{n} EXP", "BIT gained:\n{n} BIT", "Item gained:\n{item}", "Level gained!", "{name}\nLevel {n}!").
//
// API:
//     ui::ResultScreen rs;
//     rs.start(expGained, bits, oldLevel, newLevel, {{"HP", 12}, {"MP", 4}, ...}, "Agumon", portraitFrame, item);
//     each tick: rs.update(input.confirmP);   then in the frame: rs.render();
//     when rs.done(): return to the field.
// Pages: EXP (counts up) -> BIT (if > 0) -> item (if any) -> level up (if newLevel > oldLevel, with stat deltas).
// Confirm skips the count-up, then advances a page. Layout is our reconstruction (STFGTREP.PRO holds the original
// coordinates — open).
#pragma once
#include <string>
#include <utility>
#include <vector>

namespace dw3::ui {

using StatDelta = std::pair<std::string, int>;  // label (ASCII), increase

class ResultScreen {
public:
    // portraitFrame: STFRPT portrait frame id 0x00..0x1C (-1 = none). See docs/FORMATS.md.
    void start(int expGained, int bits, int oldLevel, int newLevel, const std::vector<StatDelta>& statDeltas,
               const std::string& partnerName = "", int portraitFrame = -1, const std::string& item = "");
    void update(bool confirmPressed);
    void render() const;
    bool active() const { return active_; }
    bool done() const { return done_; }
    // For previews/tests: jump to a page kind.
    enum class Page { Exp, Bits, Item, LevelUp };
    void jumpTo(Page p);

private:
    std::vector<Page> pages_;
    size_t page_ = 0;
    int exp_ = 0, bits_ = 0, oldLv_ = 1, newLv_ = 1, portrait_ = -1;
    std::vector<StatDelta> stats_;
    std::string name_, item_;
    int ticks_ = 0, pageTicks_ = 0;
    float count_ = 0;  // 0..1 count-up progress of the current page
    bool active_ = false, done_ = false;
};

}  // namespace dw3::ui
