// Layout + colours of the front-end screens (title menu, Digimon Online sign-up, starter pack, pause menu)
// in ONE place, so they are easy to restyle. Coordinates are virtual 320x240 pixels; colours 0xAABBGGRR.
//
// Title screen pieces come from DAT/SCREEN/TTCOMMPK (background + sparks) and TLOGOUSA (logo, plates);
// Digimon Online pieces from DAT/SCREEN/CMNBGD (hex background) and DAT/NAMEENT/NE_DATA_ (terminal window,
// logo, Digimon portraits). See docs/FORMATS.md "Title screen packs".
#pragma once
#include <cstdint>

namespace dw3::style {

// ---- title screen (positions measured on the original, reference shots 01/02) ----
constexpr uint32_t kTitleBgTint = 0xff9a9a9au;   // the original background reads darker than the raw texture
constexpr float kLogoX = 21, kLogoY = 20;         // TLOGOUSA frame 0x00 "DIGIMON Digital Monsters" (+ glow 0x03)
constexpr float kWorldX = 20, kWorldY = 106;      // frame 0x01 "DIGIMON WORLD" (+ glow 0x05)
constexpr float kThreeX = 254, kThreeY = 96;      // frame 0x02 "3"
constexpr float kCopyX = 33, kCopyY = 207;        // frame 0x06 copyright (origin 43,-12 -> drawn at 76,195)
constexpr float kPlateX = 78, kPlateY = 151;      // first menu plate (frames 0x08-0x0A, 164x16)
constexpr float kPlateStep = 21;                  // original: Start at 151, Continue at 172
constexpr uint32_t kPlateDim = 0xffa07878u;       // not-selected plate
constexpr uint32_t kPlateText = 0xffffc8b4u;      // our labels (Options/Quit) in the plates' lavender blue
constexpr int kBlinkTicks = 32;                   // "Press Start Button" blink half-period

// ---- Digimon Online screens (sign-up, starter pack, log-in; reference shot 17) ----
constexpr uint32_t kOnlineBg = 0xff3a1012u;       // deep navy-violet behind the hex layers
constexpr uint32_t kHexTint[3] = {0xffffffffu, 0xffe0e0e0u, 0xffc0c0c0u};
constexpr float kHexSpeed[3] = {0.10f, 0.16f, 0.24f};  // parallax drift (px per tick)
constexpr float kTerminalY = 0;                   // NE_DATA_ frame 0x01 (320x58 window + side wires)
constexpr float kTermTextX = 80, kTermTextY = 9;  // first text line inside the terminal window (box x 72..247)
constexpr float kTermLine = 13;
constexpr float kTermTextW = 160;                 // text squeezes to fit the window
constexpr int kTypeTicks = 2;                     // typewriter: ticks per character
constexpr float kLogoOnlineX = 74, kLogoOnlineY = 212;  // NE_DATA_ frame 0x38 "DIGIMON ONLINE" (origin -9,-52)
constexpr uint32_t kLogoOnlineTint = 0xffffc070u; // the logo texture is grey; the original shows it light blue
constexpr uint32_t kTermText = 0xffffd8a0u;       // terminal text (light blue)
constexpr uint32_t kTermDim = 0xffb08060u;
constexpr uint32_t kPanelFill = 0xe0200a06u;      // entry panels
constexpr uint32_t kPanelEdge = 0xffffb040u;      // cyan-blue edges like the terminal
constexpr uint32_t kSelEdge = 0xff40e8ffu;        // selected box / grid cell (yellow)
constexpr uint32_t kFieldText = 0xffffffffu;
constexpr uint32_t kErrorText = 0xff6060ffu;

// sign-up panel
constexpr float kPanelX = 24, kPanelY = 60, kPanelW = 272, kPanelH = 98;
constexpr float kFieldLabelX = 34, kFieldX = 110, kFieldW = 120, kNameY = 65, kPassY = 80;
constexpr float kGridX = 36, kGridY = 96, kCellW = 18, kCellH = 12;

// starter pack screen (original layout, reference shots 24-26; NE_DATA_ frames 0x0d header, 0x07 panel)
constexpr float kPackHeadX = 24, kPackHeadY = 11;   // frame 0x0e: step 2 of 3 lit
constexpr float kPackPanelX = 16, kPackPanelY = 40;
constexpr float kTabX = 17, kTabY = 47, kTabStep = 24, kTabW = 26, kTabH = 17;
constexpr uint32_t kTabSel = 0xc0a07890u;         // selected A/B/C tab
constexpr float kPackNameX = 58, kPackNameY = 47;
constexpr float kPortraitX = 66, kPortraitDy = -10; // 32x32 portrait standing on the diamond
constexpr float kPackTextX = 115, kMemberY = 63, kMemberStep = 42;
constexpr float kSummaryY = 191;
constexpr float kConfirmY = 92;                  // "Register account?" terminal
constexpr uint32_t kGreenText = 0xff8ce678u;

// pause / options window (original CMFONT window)
constexpr float kMenuW = 150, kMenuLine = 16;

}  // namespace dw3::style
