// Drawing with the ORIGINAL DW3 UI graphics (window frames, font, cursor, bars) in the 320x240 virtual screen.
// Call inside a frame after gfx::beginFrame(); all coordinates are virtual pixels (snapped to whole pixels).
//
// Quick start (e.g. a dialogue box):
//     ui::init(dataRoot);                                   // once, after the GL context exists
//     ui::drawWindow(8, 176, 304, 56);                      // original lavender-edged message window
//     ui::drawText(20, 184, "Agumon", ui::kYellow);         // original white font, tinted
//     ui::drawText(20, 198, "Hello!\nSecond line");         // '\n' = new line (14 px)
//     ui::drawEncoded(20, 198, bytes, n);                   // raw DW3 message bytes (01/02/03 codes)
//     ui::drawPrompt(296, 220, ticks);                      // blinking "next page" arrow
//
// Fonts (CMFONT): White 8x12 proportional (full Latin + kana), Orange 8x9 (Latin + kana), Small 8x8 HUD
// (digits, ': - /' and the letters B E H I L M P R T V X only — enough for HP MP LV EXP BIT).
#pragma once
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "ui/ui_assets.h"

namespace dw3::ui {

// Colours are 0xAABBGGRR (same as rgba()).
constexpr uint32_t kWhite = 0xffffffffu;
constexpr uint32_t kGrey = 0xff8c8c8cu;
constexpr uint32_t kYellow = 0xff40f0ffu;
constexpr uint32_t kRedText = 0xff5a5affu;

// Loads + uploads the original UI assets. Returns false (and the UI falls back to nothing) if CMFONT is missing.
bool init(const std::filesystem::path& dataRoot, std::string* err = nullptr);
bool ready();
void shutdown();  // frees the GL texture (call while the context is current)

enum class Blend { Normal, Half, Add };

// Draws one part / one piece (by frame id) of a sheet with its top-left (piece origin) at (x, y).
void drawPart(Sheet s, int part, float x, float y, uint32_t color = kWhite, Blend b = Blend::Normal, bool flipX = false);
void drawPiece(Sheet s, int frameId, float x, float y, uint32_t color = kWhite);
// Piece bounding box relative to its origin. False if the frame id does not exist.
bool pieceBounds(Sheet s, int frameId, int& x0, int& y0, int& w, int& h);

// 9-slice window from an original window piece (any size >= 24x24).
enum class WindowStyle {
    Menu,    // CMFONT frame 0x45: dark navy, lavender double edge (menus, dialogue, messages)
    Battle,  // PGFGTC00 frame 0x25: navy, cyan edge, chamfered corner (battle command lists)
};
void drawWindow(float x, float y, float w, float h, WindowStyle style = WindowStyle::Menu, float alpha = 1.f);

// Text. Returns the advance width of the widest line. '\n' starts a new line.
int drawText(float x, float y, const std::string& ascii, uint32_t color = kWhite, UiFont font = UiFont::White);
int measureText(const std::string& ascii, UiFont font = UiFont::White);
// Like drawText, but squeezes the text horizontally (down to 55%) so it fits in maxW.
int drawTextFit(float x, float y, const std::string& ascii, float maxW, uint32_t color = kWhite, UiFont font = UiFont::White);
int lineHeight(UiFont font);
// Right-aligned helper: text ends at x.
void drawTextRight(float x, float y, const std::string& ascii, uint32_t color = kWhite, UiFont font = UiFont::White);
// Raw DW3-encoded message bytes; "02 05 n" inserts args[n-1] (ASCII). Stops at 00 / 02 00. 02 07 (speaker
// tag), 02 02 (wait) and 03 (page) are not drawn. Returns the height used.
int drawEncoded(float x, float y, const uint8_t* bytes, size_t n, uint32_t color = kWhite,
                UiFont font = UiFont::White, const std::vector<std::string>& args = {});
int measureEncoded(const uint8_t* bytes, size_t n, UiFont font = UiFont::White, const std::vector<std::string>& args = {});

// Gauge fill in the original bar colours (dark body + light top line). frac 0..1; empty part left transparent.
void drawBar(float x, float y, float w, float h, float frac, BarColor color);

// Original cursors: menu pointer (yellow '>' glyph) and the "next page" down arrow (CMFONT frame 0x0D part).
void drawCursor(float x, float y, int ticks = 0);
void drawPrompt(float x, float y, int ticks);

// Untextured rectangle (virtual pixels).
void fillRect(float x, float y, float w, float h, uint32_t color);

}  // namespace dw3::ui
