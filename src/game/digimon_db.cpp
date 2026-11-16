#include "game/digimon_db.h"

#include <algorithm>
#include <fstream>
#include <sstream>

namespace dw3 {

namespace {
// Splits a line into tokens; "quoted strings" stay one token (without the quotes).
std::vector<std::string> tokens(const std::string& line) {
    std::vector<std::string> t;
    size_t i = 0;
    while (i < line.size()) {
        while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
        if (i >= line.size() || line[i] == '#') break;
        if (line[i] == '"') {
            const size_t e = line.find('"', i + 1);
            t.push_back(line.substr(i + 1, e == std::string::npos ? std::string::npos : e - i - 1));
            i = e == std::string::npos ? line.size() : e + 1;
        } else {
            size_t e = i;
            while (e < line.size() && line[e] != ' ' && line[e] != '\t') ++e;
            t.push_back(line.substr(i, e - i));
            i = e;
        }
    }
    return t;
}

int toInt(const std::string& s, int def = 0) {
    try { return std::stoi(s); } catch (...) { return def; }
}
}  // namespace

DigimonDb& digimonDb() {
    static DigimonDb db;
    return db;
}

bool DigimonDb::load(const std::filesystem::path& file, std::string& err) {
    std::ifstream f(file);
    if (!f) { err = "cannot open " + file.string(); return false; }
    defs_.clear();
    std::string line;
    int ln = 0;
    while (std::getline(f, line)) {
        ++ln;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const auto t = tokens(line);
        if (t.empty()) continue;
        auto bad = [&](const std::string& m) { err = file.filename().string() + ":" + std::to_string(ln) + ": " + m; return false; };
        if (t[0] == "digimon" && t.size() >= 3) {
            // digimon SPRITE "Name" hp H +G mp M +G atk A +G def D +G spd S +G
            DigimonDef d;
            d.sprite = t[1];
            d.name = t[2];
            for (size_t i = 3; i + 2 < t.size(); i += 3) {
                int* b = nullptr;
                int* g = nullptr;
                if (t[i] == "hp") { b = &d.base.hp; g = &d.grow.hp; }
                else if (t[i] == "mp") { b = &d.base.mp; g = &d.grow.mp; }
                else if (t[i] == "atk") { b = &d.base.atk; g = &d.grow.atk; }
                else if (t[i] == "def") { b = &d.base.def; g = &d.grow.def; }
                else if (t[i] == "spd") { b = &d.base.spd; g = &d.grow.spd; }
                else return bad("unknown stat '" + t[i] + "'");
                *b = toInt(t[i + 1]);
                *g = toInt(t[i + 2]);  // "+2" parses as 2
            }
            d.path.push_back({d.name, "", 1});
            defs_.push_back(d);
        } else if (t[0] == "path" && t.size() >= 2) {
            // path SPRITE Name@LEVEL[:MODEL] ...   (first = the Rookie itself)
            DigimonDef* d = nullptr;
            for (auto& x : defs_)
                if (x.sprite == t[1]) d = &x;
            if (!d) return bad("path for unknown digimon " + t[1] + " (put its 'digimon' line first)");
            d->path.clear();
            for (size_t i = 2; i < t.size(); ++i) {
                DigiForm fm;
                std::string s = t[i];
                const size_t colon = s.find(':');
                if (colon != std::string::npos) { fm.model = s.substr(colon + 1); s.resize(colon); }
                const size_t at = s.find('@');
                if (at != std::string::npos) { fm.level = toInt(s.substr(at + 1), 1); s.resize(at); }
                fm.name = s;
                d->path.push_back(fm);
            }
            if (d->path.empty()) return bad("empty path");
        } else {
            return bad("unknown line '" + t[0] + "'");
        }
    }
    return true;
}

const DigimonDef* DigimonDb::find(const std::string& sprite) const {
    for (const auto& d : defs_)
        if (d.sprite == sprite) return &d;
    return nullptr;
}

int DigimonDb::maxForm(const TeamMember& m) const {
    const DigimonDef* d = find(m.sprite);
    if (!d) return kMaxEvo;
    int best = 0;
    for (size_t i = 0; i < d->path.size() && int(i) <= kMaxEvo; ++i)
        if (m.level >= d->path[i].level) best = int(i);
    return best;
}

int DigimonDb::addExp(TeamMember& m, int exp) const {
    const DigimonDef* d = find(m.sprite);
    const DigiStats grow = d ? d->grow : DigiStats{10, 4, 2, 2, 1};
    int gained = 0;
    m.exp += std::max(0, exp);
    while (m.exp >= expToNext(m.level) && m.level < 99) {
        m.exp -= expToNext(m.level);
        ++m.level;
        ++gained;
        m.stats.hp += grow.hp;
        m.stats.mp += grow.mp;
        m.stats.atk += grow.atk;
        m.stats.def += grow.def;
        m.stats.spd += grow.spd;
    }
    return gained;
}

TeamMember DigimonDb::make(const std::string& sprite, const std::string& name) const {
    TeamMember m;
    m.sprite = sprite;
    m.name = name;
    if (const DigimonDef* d = find(sprite)) m.stats = d->base;
    return m;
}

}  // namespace dw3
