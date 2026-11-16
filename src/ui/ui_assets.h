// Original DW3 UI graphics, decoded at runtime from the player's own disc data and packed into one GL texture.
//
//   Sheet::Font   DAT/COMMON/CMFONTCS + CMFONTTM   — font (white 8x12 / orange 8x9 / small HUD 8x8), menu windows,
//                                                    cursor, item/element icons, PS button icons
//   Sheet::Fight  DAT/MODEL/PGFGTC00 + PGFGTW00    — battle HUD: status plates with HP bar slots, command
//                                                    windows, name tabs, gauge, bar pips
//   Sheet::Report DAT/SCREEN/STFRPTCS + STFRPTTM   — post-battle report: partner portraits, tiled background,
//                                                    title bar, portrait frame, name / EXP plates, cursors
//
// Formats: docs/FORMATS.md "UI cell sheet" / "UI texture pack" (src/formats/ui_sheet.*).
// Nothing here is shipped with the game: every pixel comes from the user's data at runtime.
#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "formats/ui_sheet.h"

namespace dw3::ui {

enum class Sheet { Font = 0, Fight = 1, Report = 2 };
constexpr int kSheetCount = 3;

struct AtlasRect {
    int x = 0, y = 0, w = 0, h = 0;
};

struct SheetData {
    UiCellGroup cells;
    UiTexGroup tex;
    std::vector<AtlasRect> partRect;  // where each part landed in the atlas (w = 0 if it failed)
    bool ok = false;
};

// Extra composed images (whole pieces) used for 9-slice windows.
enum class Extra { MenuWindow = 0, BattleWindow = 1, Count = 2 };

// Bar colours read from the battle sheet's CLUT (rows 252-255 at word x 50/51): dark + light shade.
enum class BarColor { Cyan = 0, Green = 1, Red = 2, Orange = 3 };

class Assets {
public:
    // Decodes everything on the CPU (no GL needed). Missing optional files leave that sheet !ok.
    bool load(const std::filesystem::path& dataRoot, std::string* err = nullptr);
    // Uploads the atlas (needs a current GL context). Safe to call twice.
    bool upload();
    void release();

    bool loaded() const { return loaded_; }
    bool uploaded() const { return texId_ != 0; }
    const SheetData& sheet(Sheet s) const { return sheets_[int(s)]; }
    const AtlasRect& extra(Extra e) const { return extras_[int(e)]; }
    unsigned texture() const { return texId_; }
    int texW() const { return atlasW_; }
    int texH() const { return atlasH_; }
    uint32_t barColor(BarColor c, bool light) const { return bar_[int(c)][light ? 1 : 0]; }

    // Original message strings of the fight report (DAT/COUNTRY/USA/USFGTRPT.BIN), raw encoded bytes.
    // 0 "EXP gained:\n{1} EXP", 1 "LV", 2 "Level gained!", 3 "Learned Tech:\n{1}", 4 "Can load Tech:\n{1}",
    // 5 "{1}\nLevel {2}!", 6 "Digivolve learned:\n{1}", 7 "Digivolve Slot full...", 8 "BIT gained:\n{1} BIT",
    // 9 "Item gained:\n{1}". Empty vector when missing.
    const std::vector<uint8_t>& reportText(int i) const;
    int reportTextCount() const { return int(reportText_.size()); }

private:
    std::array<SheetData, kSheetCount> sheets_;
    std::array<AtlasRect, int(Extra::Count)> extras_{};
    Image atlas_;
    int atlasW_ = 0, atlasH_ = 0;
    unsigned texId_ = 0;
    bool loaded_ = false;
    uint32_t bar_[4][2] = {};
    std::vector<std::vector<uint8_t>> reportText_;
};

// Process-wide instance (the UI is shared by battle, story and menus).
Assets& assets();

}  // namespace dw3::ui
