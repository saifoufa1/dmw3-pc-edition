#include "ui/ui_preview.h"

#include <algorithm>

#include "core/bytes.h"
#include "formats/stage_pack.h"
#include "ui/ui_draw.h"

namespace dw3::ui {

bool Preview::init(const std::filesystem::path& root, const std::string& mode, std::string& err) {
    mode_ = mode;
    if (!ui::init(root, &err)) {
        if (err.empty()) err = "UI assets (DAT/COMMON/CMFONT*.BIN) could not be loaded";
        return false;
    }
    // Field backdrop: a 320x240 crop of the S200 background.
    Bytes b;
    StageMap sm;
    if (readFile(root / "DAT/Z_STAGE/S200PACK.BIN", b) && loadStagePack(ByteView(b), sm) && sm.image.w >= 320) {
        Image crop;
        crop.resize(320, 240, rgba(0, 0, 0));
        const int ox = std::min(sm.image.w - 320, 820), oy = std::min(std::max(0, sm.image.h - 240), 300);
        for (int y = 0; y < 240 && oy + y < sm.image.h; ++y)
            for (int x = 0; x < 320; ++x) {
                const uint32_t c = sm.image.at(ox + x, oy + y);
                crop.px[size_t(y) * 320 + x] = (c >> 24) ? c : rgba(0, 0, 0);
            }
        backdrop_ = gfx::upload(crop);
    }

    HudFighter partner{"Agumon", 12, 168, 240, 31, 52, 0.7f, false, false};
    HudFighter enemy{"Goburimon", 9, 37, 150, 0, 0, -1.f, false, false};
    hud_.setFighters(partner, enemy);
    HudCommand cmds[3] = {
        {"Pepper Breath", 8, 0.f, true, true, "J", "X"},
        {"Claw Attack", 0, 0.f, true, false, "K", "B"},
        {"Spit Fire", 16, 0.45f, false, false, "L", "Y"},
    };
    hud_.setCommands(cmds, 3);
    if (mode_ == "hud2") {
        partner.dv = 1.f;
        partner.dvReady = true;
        enemy.boss = true;
        hud_.setFighters(partner, enemy);
        hud_.setMessage("Agumon used Pepper Breath!");
    }
    result_.start(128, 45, 12, mode_ == "levelup" ? 13 : 12,
                  {{"HP", 12}, {"MP", 5}, {"STR", 3}, {"DEF", 2}, {"SPR", 1}, {"WIS", 2}, {"SPD", 4}}, "Agumon", 0x00, "");
    return true;
}

Preview::~Preview() { gfx::destroy(backdrop_); ui::shutdown(); }

void Preview::update(bool confirm) {
    ++ticks_;
    hud_.update();
    if (mode_ == "levelup" && ticks_ == 2) result_.jumpTo(ResultScreen::Page::LevelUp);
    result_.update(confirm);
}

void Preview::render(int ww, int wh) {
    gfx::beginFrame(ww, wh);
    if (mode_ != "result" && mode_ != "levelup" && mode_ != "font") gfx::drawTexture(backdrop_, 0, 0);

    if (mode_ == "hud" || mode_ == "hud2") {
        hud_.render();
    } else if (mode_ == "result" || mode_ == "levelup") {
        result_.render();
    } else if (mode_ == "window") {
        // Menu list with the original cursor
        drawWindow(12, 12, 132, 92);
        const char* items[5] = {"Items", "Techniques", "Status", "Map", "Save"};
        for (int i = 0; i < 5; ++i) {
            if (i == 1) drawCursor(20, 20.f + float(i) * 16, ticks_);
            drawText(32, 20.f + float(i) * 16, items[i], i == 1 ? kYellow : kWhite);
        }
        // Battle-style window
        drawWindow(160, 12, 148, 70, WindowStyle::Battle);
        drawText(176, 22, "Battle window");
        drawText(176, 38, "HP 168/240", kWhite, UiFont::Small);
        drawText(176, 50, "Orange font 123", kWhite, UiFont::Orange);
        // Dialogue box
        drawWindow(8, 168, 304, 62);
        drawText(24, 176, "Agumon", kYellow);
        drawText(24, 192, "This box, its edge and these letters\nare decoded from your disc at runtime.");
        drawPrompt(286, 210, ticks_);
    } else if (mode_ == "font") {
        fillRect(0, 0, 320, 240, rgba(8, 8, 40));
        drawText(4, 4, "ABCDEFGHIJKLMNOPQRSTUVWXYZ");
        drawText(4, 18, "abcdefghijklmnopqrstuvwxyz");
        drawText(4, 32, "0123456789 .,:;?!'\"()[]+-=/<>~");
        drawText(4, 50, "ABCDEFGHIJKLMNOPQRSTUVWXYZ", kWhite, UiFont::Orange);
        drawText(4, 62, "abcdefghijklmnopqrstuvwxyz", kWhite, UiFont::Orange);
        drawText(4, 74, "0123456789 .,?!-~'", kWhite, UiFont::Orange);
        drawText(4, 90, "0123456789 : - / BEHILMPRTVX", kWhite, UiFont::Small);
        drawText(4, 100, "HP 168/240  MP 31  LV 12  EXP 128  BIT 45", kWhite, UiFont::Small);
        // encoded sample: "EXP gained:" + newline + arg ("02 05 01") + " EXP", built from codes
        const uint8_t enc[] = {0x12, 0x25, 0x1d, 0x01, 0x01, 0x2e, 0x28, 0x30, 0x35, 0x2c, 0x2b, 0x01, 0x07,
                               0x02, 0x01, 0x02, 0x05, 0x01, 0x01, 0x01, 0x12, 0x25, 0x1d, 0xe7, 0x00};
        drawEncoded(4, 116, enc, sizeof enc, kWhite, UiFont::White, {"128"});
        // kana block (codes 0x42..)
        float x = 4, y = 148;
        for (int c = 0x42; c <= 0xe4; ++c) {
            drawPart(Sheet::Font, uiGlyphPartForCode(UiFont::White, uint8_t(c), false), x, y);
            x += 12;
            if (x > 304) { x = 4; y += 13; }
        }
    } else if (mode_ == "sheets") {
        // every Fight-sheet piece laid out (debug)
        float x = 2, y = 2, rowH = 0;
        const SheetData& sd = assets().sheet(Sheet::Fight);
        for (size_t i = 0; i < sd.cells.frameIds.size(); ++i) {
            int x0, y0, w, h;
            if (!pieceBounds(Sheet::Fight, sd.cells.frameIds[i], x0, y0, w, h)) continue;
            if (x + float(w) > 318) { x = 2; y += rowH + 2; rowH = 0; }
            drawPiece(Sheet::Fight, sd.cells.frameIds[i], x - float(x0), y - float(y0));
            x += float(w) + 2;
            rowH = std::max(rowH, float(h));
        }
    }
}

}  // namespace dw3::ui
