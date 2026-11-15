// Our own per-stage gameplay data (walls, exits, NPCs, items, spawn), stored as editable text
// in data/stages/<StageId>.stage. This is project data, NOT copied from the game.
#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "formats/image.h"

namespace dw3 {

struct ExitZone {
    int x = 0, y = 0, w = 0, h = 0;  // map pixels
    std::string target;              // stage id, e.g. "S400"
    int arriveX = -1, arriveY = -1;  // -1 = target stage's spawn
};

enum class NpcKind { Talk, Fight, Boss };

struct NpcDef {
    NpcKind kind = NpcKind::Talk;
    std::string sprite;  // FIELD/SPRT id, e.g. "P011MINO"
    int x = 0, y = 0;
    int dir = 0;         // 0..7 (S, SE, E, NE, N, NW, W, SW)
    std::string name;
    int level = 1;       // fight/boss strength
    std::string text;    // talk line(s); '|' separates pages
};

struct ItemDef {
    std::string id;  // "sword"
    int x = 0, y = 0;
};

struct StageData {
    int cell = 8, cols = 0, rows = 0;
    std::vector<uint8_t> wall;  // 1 = blocked
    int spawnX = -1, spawnY = -1;
    std::vector<ExitZone> exits;
    std::vector<NpcDef> npcs;
    std::vector<ItemDef> items;
    bool fromFile = false;
    bool hasWallGrid = false;    // the file had a "walls" grid
    bool originalWalls = false;  // "walls original": build walls from the game's own walk map (TMPK)

    void resize(int mapW, int mapH);
    bool isWall(int px, int py) const;  // outside the grid counts as wall
    void setWallCell(int cx, int cy, bool v);
    // Blocks every cell whose centre is transparent in the background (good default).
    void autoWalls(const Image& map);
};

const char* npcKindName(NpcKind k);

bool loadStageData(const std::filesystem::path& p, StageData& out, std::string& err);
bool saveStageData(const std::filesystem::path& p, const StageData& d);

}  // namespace dw3
