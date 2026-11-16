#include "game/starter_sets.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>

namespace dw3 {

namespace {
std::vector<std::string> tokens(const std::string& line) {
    std::vector<std::string> t;
    size_t i = 0;
    while (i < line.size()) {
        while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
        if (i >= line.size() || line[i] == '#') break;
        std::string tok;
        if (line[i] == '"') {
            ++i;
            while (i < line.size() && line[i] != '"') tok += line[i++];
            ++i;
        } else {
            while (i < line.size() && line[i] != ' ' && line[i] != '\t') tok += line[i++];
        }
        t.push_back(tok);
    }
    return t;
}
}  // namespace

bool loadStarterPacks(const std::filesystem::path& file, std::vector<StarterPack>& out, std::string& err) {
    out.clear();
    std::ifstream f(file);
    if (!f) { err = "cannot open " + file.string(); return false; }
    std::string line;
    int ln = 0;
    while (std::getline(f, line)) {
        ++ln;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const auto t = tokens(line);
        if (t.empty()) continue;
        if (t[0] == "pack" && t.size() >= 3) {
            out.push_back({t[1], t[2], t.size() > 3 ? t[3] : "", {}});
        } else if (t[0] == "member" && t.size() >= 3 && !out.empty()) {
            StarterMember m;
            m.sprite = t[1];
            m.name = t[2];
            if (t.size() > 3) m.icon = int(std::strtol(t[3].c_str(), nullptr, 0));
            if (t.size() > 4) m.iconFrames = std::max(1, int(std::strtol(t[4].c_str(), nullptr, 0)));
            if (t.size() > 5) m.desc = t[5];
            out.back().members.push_back(m);
        } else {
            err = file.filename().string() + ":" + std::to_string(ln) + ": unknown line";
            return false;
        }
    }
    for (const auto& p : out)
        if (p.members.empty()) { err = "pack " + p.letter + " has no members"; return false; }
    if (out.empty()) { err = "no packs in " + file.string(); return false; }
    return true;
}

}  // namespace dw3
