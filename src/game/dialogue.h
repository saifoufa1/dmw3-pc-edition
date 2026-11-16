// Dialogue box: speaker name, paged text with a typewriter reveal, Space to advance, optional choices.
// All drawing goes through DialogueSkin so the box can switch to the original window frame + font
// (src/ui, being built separately) without touching the dialogue logic.
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "game/common.h"

namespace dw3 {

// Window-pixel drawing primitives used by the dialogue box (HUD pass).
class DialogueSkin {
public:
    virtual ~DialogueSkin() = default;
    virtual void window(int x, int y, int w, int h);  // frame + background
    virtual void nameTab(int x, int y, int w, int h);  // speaker name plate
    virtual void text(int x, int y, const std::string& s, uint32_t rgba);  // y = top of the line
    virtual int textWidth(const std::string& s);
    virtual int lineHeight();
};

class Dialogue {
public:
    Dialogue();
    void setSkin(std::unique_ptr<DialogueSkin> s) { skin_ = std::move(s); }
    DialogueSkin& skin() { return *skin_; }

    void open(const std::string& speaker, const std::vector<std::string>& pages);
    // Shows `pages` (the question), then the options on the last page. choice() = picked index.
    void openChoice(const std::string& speaker, const std::vector<std::string>& pages,
                    const std::vector<std::string>& options, int defaultIndex = 0);
    void close() { active_ = false; }
    bool active() const { return active_; }
    int choice() const { return choice_; }

    void update(const Input& in);
    void renderHud(int windowW, int windowH);

private:
    bool pageDone() const { return reveal_ >= int(pages_.empty() ? 0 : pages_[page_].size()); }
    std::unique_ptr<DialogueSkin> skin_;
    std::string speaker_;
    std::vector<std::string> pages_, options_;
    size_t page_ = 0;
    int reveal_ = 0, choice_ = 0, blink_ = 0;
    bool active_ = false;
};

}  // namespace dw3
