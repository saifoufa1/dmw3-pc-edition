// Per-Digimon data: base stats, growth per level and digivolve path (data/digivolve_paths.cfg, our own file).
// Each team member has its own level / EXP / stats (TeamMember); this table says how a species starts, grows and
// which forms it reaches at which level.
#pragma once
#include <filesystem>
#include <string>
#include <vector>

#include "game/common.h"

namespace dw3 {

struct DigiForm {
    std::string name;   // "Greymon"
    std::string model;  // DAT/MODEL prefix of its 3D battle model ("" = field sprite, tinted)
    int level = 1;      // team member level needed to digivolve to this form in battle
};

struct DigimonDef {
    std::string sprite, name;     // FIELD/SPRT id of the Rookie, e.g. "P006AGUM"
    DigiStats base, grow;         // Lv 1 stats, added per level up
    std::vector<DigiForm> path;   // [0] = Rookie, then Champion, Ultimate, Mega
};

class DigimonDb {
public:
    bool load(const std::filesystem::path& file, std::string& err);
    const DigimonDef* find(const std::string& sprite) const;
    // Highest form index the member may digivolve to (0 = Rookie only). Unknown species: every stage allowed.
    int maxForm(const TeamMember& m) const;
    // Adds EXP to a member, levels it up (stats grow by the species' growth). Returns the levels gained.
    int addExp(TeamMember& m, int exp) const;
    // A fresh Lv 1 member of this species (base stats; defaults when the species is not listed).
    TeamMember make(const std::string& sprite, const std::string& name) const;
    static int expToNext(int level) { return 30 + 20 * level; }

private:
    std::vector<DigimonDef> defs_;
};

// The table used by the game (loaded once by Game from data/digivolve_paths.cfg; empty until then).
DigimonDb& digimonDb();

}  // namespace dw3
