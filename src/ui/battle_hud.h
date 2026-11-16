// Battle HUD drawn with the ORIGINAL DW3 battle graphics (DAT/MODEL/PGFGTC00 + PGFGTW00) and font (CMFONT).
//
// API for the battle (call every tick, then render() inside the frame after the 3D scene):
//     ui::BattleHud hud;
//     hud.setFighters(partner, enemy);           // HudFighter: name, level, hp/maxHp, mp/maxMp, dv 0..1
//     hud.setCommands(cmds, 3);                  // HudCommand: name, mp cost, cooldown 0..1, available, pressed
//     hud.setMessage("Agumon used Pepper Breath!");  // optional top message bar, "" hides it
//     hud.update();                              // bar easing / cursor animation (60 Hz)
//     hud.render();                              // virtual 320x240 coordinates
//
// Layout (virtual px): enemy status plate top-right, partner status plate + MP / DV gauges bottom-left,
// command window bottom-right listing the moves in the original white font with the original cursor.
// The key / gamepad hint ("J X") is drawn small in the orange font at the right of each line.
// Positions are our own reconstruction: the exact original coordinates live in FIGHTSTG.PRO (open).
#pragma once
#include <string>
#include <vector>

namespace dw3::ui {

struct HudFighter {
    std::string name;
    int level = 1;
    int hp = 0, maxHp = 1;
    int mp = 0, maxMp = 0;   // maxMp 0 = no MP display
    float dv = -1.f;         // digivolve gauge 0..1; < 0 = hidden
    bool dvReady = false;    // gauge full: the gauge sparkles
    bool boss = false;
};

struct HudCommand {
    std::string name;        // move name (ASCII)
    int mp = 0;              // MP cost (0 = none shown)
    float cooldown = 0.f;    // remaining recharge 0..1 (0 = ready)
    bool available = true;   // false = greyed (no MP, busy, ...)
    bool pressed = false;    // highlighted with the cursor (just used / selected)
    std::string keyHint;     // e.g. "J"   (keyboard)
    std::string padHint;     // e.g. "X"   (gamepad)
};

class BattleHud {
public:
    void setFighters(const HudFighter& partner, const HudFighter& enemy);
    void setCommands(const HudCommand* cmds, int count);
    void setMessage(const std::string& text) { message_ = text; }
    void setVisible(bool v) { visible_ = v; }
    // Mouse: index of the command line under virtual point (x, y), -1 = none. setHover() shows the cursor there.
    int commandAt(float x, float y) const;
    void setHover(int i) { hover_ = i; }
    void update();
    void render() const;

private:
    HudFighter partner_, enemy_;
    std::vector<HudCommand> cmds_;
    std::string message_;
    float shownHp_[2] = {-1.f, -1.f};  // eased bar values (partner, enemy)
    int ticks_ = 0;
    bool visible_ = true;
    int hover_ = -1;
};

}  // namespace dw3::ui
