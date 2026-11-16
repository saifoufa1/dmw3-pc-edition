// Starter packs offered after the Digimon Online sign-up (data/starter_sets.cfg, our own editable file).
#pragma once
#include <filesystem>
#include <string>
#include <vector>

namespace dw3 {

struct StarterMember {
    std::string sprite, name;
    int icon = -1, iconFrames = 1;  // animated 32x32 portrait in DAT/NAMEENT/NE_DATA_ (frame ids)
    std::string desc;               // "@N" = USNAMEET message N, else our own text
};

struct StarterPack {
    std::string letter, name, desc;  // name/desc: "@N" = USNAMEET message N, else our own text
    std::vector<StarterMember> members;  // first = active partner
};

bool loadStarterPacks(const std::filesystem::path& file, std::vector<StarterPack>& out, std::string& err);

}  // namespace dw3
