// DW3 battle arenas ("fight stages"): DAT/EFFECT/MEFT1001.BIN .. MEFT1055.BIN.
//
// An arena is a static file in the battle-model layout (formats/model.h): offset table, clip
// table, one track per node (2 identical keys, no animation), byte-coded meshes, and 1-3
// RLEN/TIM texture pages. Typical layout: 6 nodes = a hexagonal sky / backdrop wall
// (radius ~32000, from y -17000 up in the air to +5000 below the floor), 4 ground quadrants
// (radius ~19200, hills at the rim) and 1-2 centre floor pieces at y ~ 0. Prims are unlit
// (flat colour x texture). See docs/FORMATS.md, "Battle arena".
//
// Arena number n (1..55) = MEFT(1000 + n); the original battle-test overlay WFIGHTTS lists them
// as MFSTG001..029 / MEFT0030..0055 and stores the 1-based number in the fight setup.
#pragma once
#include <string>
#include <vector>

#include "core/bytes.h"
#include "formats/model.h"

namespace dw3 {

constexpr int kArenaCount = 55;

struct Arena {
    int number = 0;               // 1..55
    Model model;
    Pose pose;                    // static pose (frame 0)
    std::vector<Mat34> mats;      // node world matrices for `pose`

    // Ground: a height grid built from the near-horizontal triangles of the arena mesh.
    // Heights are PS1 world y (down is +y). A cell without a floor near the play height is a
    // wall / pit / hill and blocks movement.
    float gridMin = -24000.f;     // world x/z of cell 0
    float cell = 250.f;
    int gridN = 192;
    std::vector<float> floor;     // gridN * gridN, kNoFloor = none
    static constexpr float kNoFloor = 1e30f;
    float playY = 0;              // floor height at the arena centre
    float centerX = 0, centerZ = 0;  // middle of the open floor (some arenas are off-centre corridors)
    float walkRadius = 9000;      // fighters stay inside this circle around the centre (and on walkable cells)
    bool flat = false;            // no floor found at the centre: an infinite flat plane at y = 0

    // Floor height under (x, z); false where there is no walkable floor.
    bool floorAt(float x, float z, float& y) const;
    bool walkable(float x, float z) const;
};

// Parses an arena file and builds its ground grid.
bool loadArena(ByteView file, int number, Arena& out, std::string& err);

// File name of arena n: "MEFT1001" .. "MEFT1055".
std::string arenaFileName(int number);
// Short description of an arena (for logs and the dev overlay).
const char* arenaName(int number);
// Arena used when fighting on field stage `stageId` ("S200"), see docs/FORMATS.md
// (hand-made by matching the field art; the original stage -> arena table is not decoded yet).
int arenaForStage(const std::string& stageId);
// Same theme, other number: the replacement used when arena n cannot be loaded.
int arenaFallback(int number);

}  // namespace dw3
