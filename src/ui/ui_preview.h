// --ui-preview MODE: shows one original-UI component over a field backdrop (for screenshots / visual checks)
// without touching the battle or field code. MODE = hud | result | levelup | window | font | sheets.
#pragma once
#include <filesystem>
#include <string>

#include "render/gl_renderer.h"
#include "ui/battle_hud.h"
#include "ui/result_screen.h"

namespace dw3::ui {

class Preview {
public:
    bool init(const std::filesystem::path& dataRoot, const std::string& mode, std::string& err);
    void update(bool confirmPressed);
    void render(int windowW, int windowH);
    ~Preview();

private:
    std::string mode_;
    Texture backdrop_;
    BattleHud hud_;
    ResultScreen result_;
    int ticks_ = 0;
};

}  // namespace dw3::ui
