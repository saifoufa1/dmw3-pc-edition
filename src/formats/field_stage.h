// Per-stage field data from DAT/FIELD/STAGE: S###MASK.BIN (scene-piece texture atlas) and
// S###TMPK.BIN (scene-piece part/frame tables + walk map + trigger map). See docs/FORMATS.md.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "core/bytes.h"
#include "formats/image.h"

namespace dw3 {

// One textured rectangle of the MASK atlas (TMPK parts table, 14 bytes).
struct FieldPart {
    uint16_t x = 0, y = 0, w = 0, h = 0;  // kind 1: 8bpp texels; kind 0: 4bpp texels
    uint16_t clut = 0;                    // kind 0: palette (row = clut >> 4, 16-colour bank = clut & 15)
    uint16_t rowOrDepth = 0;              // kind 1: 256 + CLUT row; kind 0: unknown (0xE9..0xFC)
    uint16_t kind = 0;                    // 1 = 8bpp opaque piece, 0 = 4bpp effect (drawn additive)
};

// One scene piece = parts drawn at (x + dx, y + dy) (wide pieces are cut into vertical strips).
struct FieldPieceLayer {
    uint16_t part = 0;
    int16_t dx = 0, dy = 0;
};
struct FieldPiece {
    int16_t flag = 0;  // -1 for 8bpp pieces, 3 for 4bpp effect pieces (open)
    std::vector<FieldPieceLayer> layers;
};

struct FieldStage {
    int w = 0, h = 0;                  // map size in pixels (grid * 128)
    std::vector<uint8_t> walk;         // w*h, 0 = blocked, 1 = floor, 6 = stairs (others: floor)
    std::vector<uint8_t> trigger;      // w*h, 0 = none, else (type << 5) | index
    bool hasWalk = false, hasTrigger = false;

    std::vector<FieldPart> parts;
    std::vector<FieldPiece> pieces;    // pieces[i] = TMPK entry i + 2
    std::vector<uint8_t> frameIds;     // frame id of each piece (TMPK entry 1), sorted ascending

    int atlasW = 0;                    // 8bpp texels (128 per MASK block), height 256
    std::vector<uint8_t> atlas;        // atlasW * 256 palette indices
    int clutRows = 0;
    std::vector<uint16_t> clut;        // clutRows * 256 PS1 colours

    uint8_t walkAt(int x, int y) const { return (x < 0 || y < 0 || x >= w || y >= h || walk.empty()) ? 0 : walk[size_t(y) * w + x]; }
    uint8_t triggerAt(int x, int y) const { return (x < 0 || y < 0 || x >= w || y >= h || trigger.empty()) ? 0 : trigger[size_t(y) * w + x]; }
    // Piece index for an object frame id, -1 if none.
    int pieceForFrame(int frameId) const;
    // Renders one part into an RGBA image (colour 0 = transparent).
    bool renderPart(int part, Image& out) const;
    // Renders a whole piece; the image's top-left sits at piece origin + (originX, originY).
    bool renderPiece(int piece, Image& out, int& originX, int& originY) const;
};

// gridW/gridH come from the matching Z_STAGE pack (the TMPK maps use the same 128 px grid).
// Either view may be empty (missing file); returns false only if nothing could be decoded.
bool loadFieldStage(ByteView mask, ByteView tmpk, int gridW, int gridH, FieldStage& out, std::string* err = nullptr);

}  // namespace dw3
