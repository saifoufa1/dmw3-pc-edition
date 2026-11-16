// Whole-screen ORIGINAL UI art (title screen, Digimon Online sign-up) decoded at runtime from the player's data.
// Each piece (and, on request, each part) of a cell sheet becomes its own GL texture, so big 320x240 pieces
// do not have to fit the shared UI atlas of ui_assets.
//
//   PackArt title;
//   title.loadPack(root / "DAT/SCREEN/TLOGOUSA.BIN");          // combined pack (cells + textures in one file)
//   title.loadSheet(root / "DAT/SCREEN/CMNBGDCS.BIN", root / "DAT/SCREEN/CMNBGDTM.BIN");   // CS + TM pair
//   title.draw(0, 0x00, 21, 20);   // group 0, frame id 0x00, piece origin at (21, 20) in the 320x240 screen
//
// Formats: docs/FORMATS.md "UI cell sheet" + "Title screen packs". Nothing is shipped with the game.
#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "formats/ui_sheet.h"
#include "render/gl_renderer.h"

namespace dw3::ui {

class PackArt {
public:
    PackArt() = default;
    PackArt(const PackArt&) = delete;
    PackArt& operator=(const PackArt&) = delete;
    ~PackArt() { release(); }

    bool loadPack(const std::filesystem::path& file, std::string* err = nullptr);
    bool loadSheet(const std::filesystem::path& cs, const std::filesystem::path& tm, std::string* err = nullptr);
    void release();  // frees the textures (GL context must be current)
    bool ok() const { return !groups_.empty(); }

    // Draws the piece with frame id `frame` of cell group `group`; (x, y) = piece origin. flag 2/3 pieces are
    // additive unless `blend` says otherwise. Returns false when the piece does not exist.
    enum class Blend { Auto, Normal, Add };
    bool draw(int group, int frame, float x, float y, uint32_t color = 0xffffffffu, Blend blend = Blend::Auto);
    // One part (top-left at x, y).
    bool drawPart(int group, int part, float x, float y, uint32_t color = 0xffffffffu, bool additive = false);
    // Piece bounds relative to its origin.
    bool bounds(int group, int frame, int& x0, int& y0, int& w, int& h);
    int pieceCount(int group) const;
    int frameOf(int group, int pieceIndex) const;  // frame id of the i-th piece, -1 if none

private:
    struct Tex {
        Texture tex;
        int ox = 0, oy = 0;
        bool tried = false;
    };
    struct Group {
        UiCellGroup cells;
        int tex = 0;
        std::vector<Tex> pieces, parts;
    };
    Tex* piece(int group, int frame);
    std::vector<Group> groups_;
    std::vector<UiTexGroup> tex_;
};

}  // namespace dw3::ui
