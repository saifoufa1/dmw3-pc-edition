#include "ui/battle_hud.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "ui/ui_draw.h"

namespace dw3::ui {

namespace {

// PGFGTC00 frame ids (piece table entry 1). See docs/FORMATS.md "Battle HUD sheet".
constexpr int kEnemyPlate = 0x00;    // 148x44: name box + HP bar slot
constexpr int kPartnerPlate = 0x01;  // 149x44: name box + level box + HP bar slot + number tab
constexpr int kSmallPlate = 0x23;    // 84x19 plate (MP)
constexpr int kGauge = 0x1e;         // 52x10 glass gauge (DV)
constexpr int kSparkFirst = 0x33;    // 0x33..0x36 sparkle strips 40x8, 0x37..0x3a lightning strips
constexpr int kMessageBar = 0x31;    // 320x38 message bar

constexpr float kEnemyX = 168, kEnemyY = 4;
constexpr float kPartnerX = 4, kPartnerY = 192;
constexpr float kCmdX = 172, kCmdY = 170, kCmdW = 144, kCmdH = 64;

std::string num(int v) { return std::to_string(std::max(0, v)); }

}  // namespace

void BattleHud::setFighters(const HudFighter& partner, const HudFighter& enemy) {
    partner_ = partner;
    enemy_ = enemy;
    if (shownHp_[0] < 0) shownHp_[0] = float(partner.hp);
    if (shownHp_[1] < 0) shownHp_[1] = float(enemy.hp);
}

void BattleHud::setCommands(const HudCommand* cmds, int count) {
    cmds_.assign(cmds, cmds + std::max(0, count));
}

void BattleHud::update() {
    ++ticks_;
    const float target[2] = {float(partner_.hp), float(enemy_.hp)};
    for (int i = 0; i < 2; ++i) {
        if (shownHp_[i] < 0) shownHp_[i] = target[i];
        const float d = target[i] - shownHp_[i];
        // drain smoothly (about half a second for a big hit), refill instantly
        shownHp_[i] = d > 0 ? target[i] : shownHp_[i] + std::min(-0.f, std::max(d, d * 0.12f - 0.5f));
        if (std::abs(target[i] - shownHp_[i]) < 0.5f) shownHp_[i] = target[i];
    }
}

void BattleHud::render() const {
    if (!visible_ || !ready()) return;

    // ---- enemy plate (top right) ----
    drawPiece(Sheet::Fight, kEnemyPlate, kEnemyX, kEnemyY);
    drawText(kEnemyX + 10, kEnemyY + 6, enemy_.name, enemy_.boss ? kRedText : kWhite);
    drawTextRight(kEnemyX + 132, kEnemyY + 8, "LV" + num(enemy_.level), kWhite, UiFont::Small);
    {
        const float k = enemy_.maxHp > 0 ? std::max(0.f, shownHp_[1]) / float(enemy_.maxHp) : 0.f;
        drawBar(kEnemyX + 7, kEnemyY + 22, 128, 6, k, k < 0.25f ? BarColor::Red : BarColor::Green);
    }

    // ---- partner plate (bottom left) ----
    drawPiece(Sheet::Fight, kPartnerPlate, kPartnerX, kPartnerY);
    drawText(kPartnerX + 13, kPartnerY + 6, partner_.name, kWhite);
    drawText(kPartnerX + 82, kPartnerY + 10, "LV", kWhite, UiFont::Small);
    drawTextRight(kPartnerX + 141, kPartnerY + 10, num(partner_.level), kWhite, UiFont::Small);
    {
        const float k = partner_.maxHp > 0 ? std::max(0.f, shownHp_[0]) / float(partner_.maxHp) : 0.f;
        drawBar(kPartnerX + 14, kPartnerY + 22, 128, 6, k, k < 0.25f ? BarColor::Red : BarColor::Green);
        // HP numbers in the plate's lower-right tab
        drawTextRight(kPartnerX + 145, kPartnerY + 35, num(partner_.hp) + "/" + num(partner_.maxHp), kWhite, UiFont::Small);
    }

    // ---- MP plate + DV gauge (above the partner plate) ----
    if (partner_.maxMp > 0) {
        const float px = kPartnerX + 2, py = kPartnerY - 21;
        drawPiece(Sheet::Fight, kSmallPlate, px, py);
        drawText(px + 6, py + 5, "MP", kWhite, UiFont::Small);
        drawBar(px + 22, py + 7, 30, 3, float(partner_.mp) / float(partner_.maxMp), BarColor::Cyan);
        drawTextRight(px + 77, py + 5, num(partner_.mp), kWhite, UiFont::Small);
    }
    if (partner_.dv >= 0) {
        const float gx = kPartnerX + 106, gy = kPartnerY - 16;
        drawText(gx - 18, gy + 1, "DV", partner_.dvReady ? kYellow : kWhite, UiFont::Orange);  // small font has no D
        drawPiece(Sheet::Fight, kGauge, gx, gy);
        drawBar(gx + 5, gy + 3, 40, 5, partner_.dv, BarColor::Orange);
        if (partner_.dvReady) drawPiece(Sheet::Fight, kSparkFirst + (ticks_ / 4) % 8, gx, gy);
    }

    // ---- command window (bottom right): the moves in the original font + cursor ----
    if (!cmds_.empty()) {
        drawWindow(kCmdX, kCmdY, kCmdW, kCmdH, WindowStyle::Battle);
        const float lineH = 16;
        float y = kCmdY + 9;
        for (const HudCommand& c : cmds_) {
            const bool ready = c.available && c.cooldown <= 0.f;
            if (c.pressed) drawCursor(kCmdX + 6, y, ticks_);
            const uint32_t col = c.pressed ? kYellow : ready ? kWhite : kGrey;
            drawText(kCmdX + 16, y, c.name, col);
            if (c.mp > 0) drawTextRight(kCmdX + kCmdW - 24, y + 3, num(c.mp), ready ? kWhite : kGrey, UiFont::Small);
            // hint: keyboard key in the orange font, gamepad button dimmer next to it
            if (!c.keyHint.empty()) drawText(kCmdX + kCmdW - 21, y + 2, c.keyHint, ready ? kWhite : kGrey, UiFont::Orange);
            if (!c.padHint.empty()) drawText(kCmdX + kCmdW - 11, y + 2, c.padHint, 0xff707070u, UiFont::Orange);
            if (c.cooldown > 0.f)  // recharge line under the name
                drawBar(kCmdX + 16, y + 12, 96 * (1.f - std::min(1.f, c.cooldown)), 1, 1.f, BarColor::Cyan);
            y += lineH;
        }
    }

    // ---- message bar ----
    if (!message_.empty()) {
        const float my = 52;
        drawPiece(Sheet::Fight, kMessageBar, 0, my);
        const int w = measureText(message_);
        drawText(float((320 - w) / 2), my + 12, message_);
    }
}

}  // namespace dw3::ui
