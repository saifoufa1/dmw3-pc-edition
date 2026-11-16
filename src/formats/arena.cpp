#include "formats/arena.h"

#include <algorithm>
#include <cmath>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <utility>

namespace dw3 {

namespace {

// What each arena looks like (from rendering all 55 with dw3check --dump-arena).
const char* const kArenaNames[kArenaCount + 1] = {
    "",
    "sky island, grass",               // 1001
    "sky island, beach and cliffs",    // 1002
    "sky, sand ruins",                 // 1003
    "sky, rocky canyon",               // 1004
    "sky, desert dunes",               // 1005
    "stone dungeon",                   // 1006
    "dark stone dungeon",              // 1007
    "pine forest clearing",            // 1008
    "night, moon, rock",               // 1009
    "digital grid (cyber)",            // 1010
    "water",                           // 1011
    "factory hall",                    // 1012
    "sky island, forest",              // 1013
    "snow mountain",                   // 1014
    "space station hull",              // 1015
    "space station deck",              // 1016
    "space, planet",                   // 1017
    "(other layout)",                  // 1018
    "crystal spikes (purple)",         // 1019
    "tech arena platform",             // 1020
    "space, planet and satellite",     // 1021
    "space, planet close",             // 1022
    "digital ring arena",              // 1023
    "wooden barrel / tree trunk",      // 1024
    "ice crystal arena",               // 1025
    "dark violet void",                // 1026
    "ice cave",                        // 1027
    "data rings",                      // 1028
    "light tower",                     // 1029
    "binary data sphere",              // 1030
    "data rings (gold)",               // 1031
    "red vortex",                      // 1032
    "red vortex, ring",                // 1033
    "red vortex, fire wall",           // 1034
    "blue vortex",                     // 1035
    "blue vortex, ring",               // 1036
    "blue vortex, water wall",         // 1037
    "cyan vortex",                     // 1038
    "cyan vortex, ring",               // 1039
    "cyan vortex, crystal wall",       // 1040
    "green vortex",                    // 1041
    "green vortex, ring",              // 1042
    "green vortex, wind wall",         // 1043
    "yellow vortex",                   // 1044
    "yellow vortex, ring",             // 1045
    "yellow vortex, lightning wall",   // 1046
    "white vortex",                    // 1047
    "white vortex, ring",              // 1048
    "white vortex, machine wall",      // 1049
    "violet vortex",                   // 1050
    "violet vortex, ring",             // 1051
    "violet vortex, demon wall",       // 1052
    "grey sky dome",                   // 1053
    "sky, desert dunes (2)",           // 1054
    "city lights dome",                // 1055
};

struct StageRange { int from, to, arena; };
// Field stage -> arena (see docs/FORMATS.md "Battle arena", confidence low: matched by theme).
const StageRange kStageArenas[] = {
    // Matched by look: the field stage's own art (dw3check --dump-stage) next to the arena's.
    {200, 209, 55},  // Asuka City (towns, castle)                -> city lights dome
    {210, 229, 10},  // Asuka server interiors (cyber rooms)       -> digital grid
    {230, 234, 23},  // server core                                -> digital ring arena
    {235, 249, 12},  // homes, shops, offices                      -> factory hall
    {250, 254, 13},  // garden                                     -> sky island forest
    {255, 299, 12},  // buildings / corridors                      -> factory hall
    {300, 329, 20},  // tech towers, server halls                  -> tech platform
    {330, 339, 1},   // grassland roads                            -> sky island, grass
    {340, 344, 2},   // beach                                      -> sky island, beach
    {345, 349, 3},   // sandy paths                                -> sand ruins
    {350, 359, 1},   // meadows                                    -> sky island, grass
    {360, 369, 24},  // wooden interiors                           -> tree trunk
    {370, 374, 8},   // forest                                     -> pine forest
    {375, 379, 6},   // stone maze                                 -> stone dungeon
    {380, 384, 1},   // crater meadow                              -> sky island, grass
    {385, 394, 2},   // coast                                      -> beach
    {395, 399, 4},   // canyon                                     -> rocky canyon
    {400, 404, 8},   // forest                                     -> pine forest
    {405, 409, 3},   // fossil cliffs                              -> sand ruins
    {410, 419, 20},  // tech building in the woods                 -> tech platform
    {420, 424, 13},  // castle on the hill                         -> sky island forest
    {425, 434, 24},  // wooden interiors                           -> tree trunk
    {435, 439, 6},   // stone pit                                  -> stone dungeon
    {440, 444, 20},  // tech base                                  -> tech platform
    {445, 459, 8},   // jungle                                     -> pine forest
    {460, 464, 7},   // cave                                       -> dark stone dungeon
    {465, 469, 11},  // swamp village                              -> water
    {470, 479, 24},  // wooden interiors                           -> tree trunk
    {480, 484, 9},   // dark swamp                                 -> night, moon, rock
    {485, 489, 11},  // marsh                                      -> water
    {490, 494, 8},   // jungle                                     -> pine forest
    {495, 499, 2},   // beach                                      -> beach
    {500, 519, 2},   // harbour town                               -> beach
    {520, 524, 24},  // temple interior                            -> tree trunk
    {525, 529, 11},  // sunken ruins                               -> water
    {530, 539, 7},   // dark dungeon                               -> dark stone dungeon
    {540, 549, 23},  // digital hex field                          -> digital ring arena
    {550, 554, 3},   // desert canyon                              -> sand ruins
    {555, 559, 5},   // desert                                     -> desert dunes
    {560, 579, 3},   // desert canyon, ruins                       -> sand ruins
    {580, 629, 12},  // factory                                    -> factory hall
    {630, 644, 5},   // desert                                     -> desert dunes
    {645, 649, 54},  // desert tower                               -> desert dunes (2)
    {650, 654, 19},  // crystal cave                               -> crystal spikes
    {655, 659, 25},  // blue crystal                               -> ice crystal arena
    {660, 674, 3},   // desert fortress                            -> sand ruins
    {675, 684, 6},   // stone interiors                            -> stone dungeon
    {685, 689, 27},  // ice cave                                   -> ice cave
    {690, 694, 4},   // red rock                                   -> rocky canyon
    {695, 709, 14},  // snow mountain                              -> snow mountain
    {710, 714, 4},   // autumn cliffs                              -> rocky canyon
    {715, 719, 27},  // frozen lake                                -> ice cave
    {720, 724, 25},  // ice platforms                              -> ice crystal arena
    {725, 734, 7},   // dark caves                                 -> dark stone dungeon
    {735, 739, 25},  // ice crystal cliffs                         -> ice crystal arena
    {740, 744, 17},  // space                                      -> space, planet
    {745, 749, 26},  // violet void grid                           -> dark violet void
    {750, 754, 29},  // tower                                      -> light tower
    {755, 759, 14},  // snow base                                  -> snow mountain
    {760, 779, 12},  // machine halls                              -> factory hall
    {780, 784, 55},  // city streets                               -> city lights dome
    {785, 789, 53},  // white hall                                 -> grey sky dome
    {790, 794, 16},  // machine core                               -> space station deck
    {795, 819, 15},  // space corridors                            -> space station hull
    {820, 824, 26},  // dark lab                                   -> dark violet void
    {825, 864, 4},   // rock spires                                -> rocky canyon
    {865, 874, 28},  // circuit board                              -> data rings
    {875, 884, 30},  // circuit board                              -> binary sphere
    {885, 899, 31},  // circuit board                              -> data rings (gold)
};

}  // namespace

std::string arenaFileName(int n) {
    char buf[16];
    std::snprintf(buf, sizeof buf, "MEFT%04d", 1000 + std::clamp(n, 1, kArenaCount));
    return buf;
}

const char* arenaName(int n) { return n >= 1 && n <= kArenaCount ? kArenaNames[n] : "?"; }

int arenaForStage(const std::string& id) {
    int num = -1;
    if (id.size() >= 4 && (id[0] == 'S' || id[0] == 's')) num = std::atoi(id.c_str() + 1);
    for (const StageRange& r : kStageArenas)
        if (num >= r.from && num <= r.to) return r.arena;
    return 1;  // default: the open sky island
}

int arenaFallback(int n) {
    switch (n) {
        case 18: return 1;   // other layout (mesh command 3) -> sky island
        case 2: case 3: case 4: case 5: case 13: case 54: return 1;
        case 7: return 6;
        case 17: case 21: case 22: return 16;
        default: return n == 1 ? 13 : 1;
    }
}

bool Arena::floorAt(float x, float z, float& y) const {
    if (flat) { y = playY; return true; }
    const int i = int(std::floor((x - gridMin) / cell)), j = int(std::floor((z - gridMin) / cell));
    if (i < 0 || j < 0 || i >= gridN || j >= gridN) return false;
    const float v = floor[size_t(j) * size_t(gridN) + size_t(i)];
    if (v == kNoFloor) return false;
    y = v;
    return true;
}

bool Arena::walkable(float x, float z) const {
    const float dx = x - centerX, dz = z - centerZ;
    if (dx * dx + dz * dz > walkRadius * walkRadius) return false;
    float y;
    return floorAt(x, z, y);
}

bool loadArena(ByteView file, int number, Arena& a, std::string& err) {
    a = Arena{};
    a.number = number;
    if (!loadModel(file, a.model, err)) return false;
    samplePose(a.model, 0.f, a.pose);
    poseMatrices(a.model, a.pose, a.mats);

    // Candidate floor heights per grid cell from every near-horizontal triangle.
    const int N = a.gridN;
    std::vector<std::pair<int, float>> cand;
    auto addTri = [&](const float* p0, const float* p1, const float* p2) {
        const float ux = p1[0] - p0[0], uy = p1[1] - p0[1], uz = p1[2] - p0[2];
        const float vx = p2[0] - p0[0], vy = p2[1] - p0[1], vz = p2[2] - p0[2];
        const float nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
        const float nl = std::sqrt(nx * nx + ny * ny + nz * nz);
        if (nl < 1e-3f || std::fabs(ny) / nl < 0.55f) return;  // walls and steep slopes
        const float x0 = std::min({p0[0], p1[0], p2[0]}), x1 = std::max({p0[0], p1[0], p2[0]});
        const float z0 = std::min({p0[2], p1[2], p2[2]}), z1 = std::max({p0[2], p1[2], p2[2]});
        const int i0 = std::max(0, int(std::ceil((x0 - a.gridMin) / a.cell - 0.5f)));
        const int i1 = std::min(N - 1, int(std::floor((x1 - a.gridMin) / a.cell - 0.5f)));
        const int j0 = std::max(0, int(std::ceil((z0 - a.gridMin) / a.cell - 0.5f)));
        const int j1 = std::min(N - 1, int(std::floor((z1 - a.gridMin) / a.cell - 0.5f)));
        const float den = (p1[2] - p2[2]) * (p0[0] - p2[0]) + (p2[0] - p1[0]) * (p0[2] - p2[2]);
        if (std::fabs(den) < 1e-3f) return;
        for (int j = j0; j <= j1; ++j)
            for (int i = i0; i <= i1; ++i) {
                const float px = a.gridMin + (float(i) + 0.5f) * a.cell, pz = a.gridMin + (float(j) + 0.5f) * a.cell;
                const float l0 = ((p1[2] - p2[2]) * (px - p2[0]) + (p2[0] - p1[0]) * (pz - p2[2])) / den;
                const float l1 = ((p2[2] - p0[2]) * (px - p2[0]) + (p0[0] - p2[0]) * (pz - p2[2])) / den;
                const float l2 = 1.f - l0 - l1;
                if (l0 < -1e-4f || l1 < -1e-4f || l2 < -1e-4f) continue;
                cand.push_back({j * N + i, l0 * p0[1] + l1 * p1[1] + l2 * p2[1]});
            }
    };
    for (size_t n = 0; n < a.model.nodes.size(); ++n) {
        if (a.model.nodes[n].mesh < 0) continue;
        const ModelMesh& m = a.model.meshes[size_t(a.model.nodes[n].mesh)];
        std::vector<std::array<float, 3>> w(m.verts.size());
        for (size_t k = 0; k < m.verts.size(); ++k)
            xform(a.mats[n], m.verts[k][0], m.verts[k][1], m.verts[k][2], w[k][0], w[k][1], w[k][2]);
        for (const ModelPrim& p : m.prims) {
            addTri(w[p.v[0]].data(), w[p.v[1]].data(), w[p.v[2]].data());
            if (p.count == 4) addTri(w[p.v[1]].data(), w[p.v[3]].data(), w[p.v[2]].data());
        }
    }

    // Play height: the candidate closest to y = 0 near the centre (arenas are built around it).
    const int c0 = int((0.f - a.gridMin) / a.cell);
    float best = Arena::kNoFloor;
    for (const auto& c : cand) {
        const int i = c.first % N, j = c.first / N;
        if (std::abs(i - c0) <= 3 && std::abs(j - c0) <= 3 && std::fabs(c.second) < std::fabs(best)) best = c.second;
    }
    a.floor.assign(size_t(N) * size_t(N), Arena::kNoFloor);
    if (best == Arena::kNoFloor || std::fabs(best) > 3000.f) {
        a.flat = true;  // no floor model under the fighters (space / vortex arenas): a plane at 0
        a.playY = 0;
        a.walkRadius = 9000;
        return true;
    }
    a.playY = best;
    constexpr float kStep = 900.f;  // higher / lower than this from the play height = blocked
    std::vector<float> near(size_t(N) * size_t(N), Arena::kNoFloor);
    for (const auto& c : cand) {
        float& f = near[size_t(c.first)];
        if (std::fabs(c.second - a.playY) > kStep) continue;
        if (f == Arena::kNoFloor || std::fabs(c.second - a.playY) < std::fabs(f - a.playY)) f = c.second;
    }
    // Keep only the floor connected to the centre without a step (rubble, ramps and hills
    // that merely touch the floor stay out).
    constexpr float kClimb = 220.f;
    std::vector<int> todo{c0 * N + c0};
    a.floor[size_t(c0 * N + c0)] = near[size_t(c0 * N + c0)];
    double sx = 0, sz = 0;
    int cells = 0;
    while (!todo.empty()) {
        const int id = todo.back();
        todo.pop_back();
        const int i = id % N, j = id / N;
        const float h = a.floor[size_t(id)];
        sx += a.gridMin + (i + 0.5) * a.cell;
        sz += a.gridMin + (j + 0.5) * a.cell;
        ++cells;
        const int nb[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
        for (const auto& d : nb) {
            const int ni = i + d[0], nj = j + d[1];
            if (ni < 0 || nj < 0 || ni >= N || nj >= N) continue;
            const size_t k = size_t(nj * N + ni);
            if (a.floor[k] != Arena::kNoFloor || near[k] == Arena::kNoFloor || std::fabs(near[k] - h) > kClimb) continue;
            a.floor[k] = near[k];
            todo.push_back(int(k));
        }
    }
    // Centre of the fight: middle of the open floor, pulled back toward the arena origin (where
    // the original camera looks) when the floor is a long corridor.
    if (cells > 0) {
        a.centerX = float(sx / cells) * 0.5f;
        a.centerZ = float(sz / cells) * 0.5f;
        float y;
        if (!a.floorAt(a.centerX, a.centerZ, y)) a.centerX = a.centerZ = 0;
    }
    // Walk radius: the largest ring around the centre that is (almost) all floor.
    a.walkRadius = 14000;
    for (float r = 1500; r <= 14000; r += 250) {
        int ok = 0;
        constexpr int kDirs = 64;
        for (int k = 0; k < kDirs; ++k) {
            const float ang = float(k) * 6.2831853f / kDirs;
            float y;
            ok += a.floorAt(a.centerX + std::cos(ang) * r, a.centerZ + std::sin(ang) * r, y);
        }
        if (ok < kDirs * 9 / 10) { a.walkRadius = std::max(3000.f, r - 250.f); break; }
    }
    if (a.walkRadius < 4500.f) {
        // Only a small platform (space / crystal / tower arenas): too cramped for a free fight,
        // so the fighters move on the plane of the platform instead.
        a.flat = true;
        a.walkRadius = 7000;
    }
    return true;
}

}  // namespace dw3
