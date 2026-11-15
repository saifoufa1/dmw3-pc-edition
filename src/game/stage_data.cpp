#include "game/stage_data.h"

#include <algorithm>
#include <fstream>
#include <sstream>

namespace dw3 {

void StageData::resize(int mapW, int mapH) {
    cols = (mapW + cell - 1) / cell;
    rows = (mapH + cell - 1) / cell;
    wall.assign(size_t(cols) * rows, 0);
}

bool StageData::isWall(int px, int py) const {
    if (px < 0 || py < 0) return true;
    int cx = px / cell, cy = py / cell;
    if (cx >= cols || cy >= rows) return true;
    return wall[size_t(cy) * cols + cx] != 0;
}

void StageData::setWallCell(int cx, int cy, bool v) {
    if (cx >= 0 && cy >= 0 && cx < cols && cy < rows) wall[size_t(cy) * cols + cx] = v ? 1 : 0;
}

void StageData::autoWalls(const Image& map) {
    for (int cy = 0; cy < rows; ++cy)
        for (int cx = 0; cx < cols; ++cx)
            setWallCell(cx, cy, !map.opaque(cx * cell + cell / 2, cy * cell + cell / 2));
}

const char* npcKindName(NpcKind k) {
    switch (k) {
        case NpcKind::Fight: return "fight";
        case NpcKind::Boss: return "boss";
        default: return "talk";
    }
}

namespace {
// Splits a line into tokens; "double quoted" tokens may contain spaces.
std::vector<std::string> tokenize(const std::string& line) {
    std::vector<std::string> t;
    size_t i = 0;
    while (i < line.size()) {
        while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
        if (i >= line.size()) break;
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

int toInt(const std::vector<std::string>& t, size_t i, int def) {
    if (i >= t.size()) return def;
    try { return std::stoi(t[i]); } catch (...) { return def; }
}

NpcKind kindFrom(const std::string& s) {
    if (s == "fight") return NpcKind::Fight;
    if (s == "boss") return NpcKind::Boss;
    return NpcKind::Talk;
}
}  // namespace

bool loadStageData(const std::filesystem::path& p, StageData& d, std::string& err) {
    std::ifstream f(p);
    if (!f) { err = "cannot open " + p.string(); return false; }
    d = {};
    std::string line;
    int wallRow = -1;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (wallRow >= 0) {
            if (wallRow < d.rows)
                for (int x = 0; x < d.cols && x < int(line.size()); ++x) d.setWallCell(x, wallRow, line[x] == '#');
            ++wallRow;
            continue;
        }
        if (line.empty() || line[0] == '#') continue;
        auto t = tokenize(line);
        if (t.empty()) continue;
        const std::string& k = t[0];
        if (k == "cell") d.cell = std::max(2, toInt(t, 1, 8));
        else if (k == "size") { d.cols = toInt(t, 1, 0); d.rows = toInt(t, 2, 0); d.wall.assign(size_t(d.cols) * d.rows, 0); }
        else if (k == "spawn") { d.spawnX = toInt(t, 1, -1); d.spawnY = toInt(t, 2, -1); }
        else if (k == "exit" && t.size() >= 6)
            d.exits.push_back({toInt(t, 1, 0), toInt(t, 2, 0), toInt(t, 3, 0), toInt(t, 4, 0), t[5], toInt(t, 6, -1), toInt(t, 7, -1)});
        else if (k == "npc" && t.size() >= 6) {
            NpcDef n;
            n.kind = kindFrom(t[1]);
            n.sprite = t[2];
            n.x = toInt(t, 3, 0);
            n.y = toInt(t, 4, 0);
            n.dir = toInt(t, 5, 0) & 7;
            n.name = t.size() > 6 ? t[6] : t[2];
            if (n.kind == NpcKind::Talk) n.text = t.size() > 7 ? t[7] : "...";
            else n.level = toInt(t, 7, 1);
            d.npcs.push_back(n);
        } else if (k == "item" && t.size() >= 4) d.items.push_back({t[1], toInt(t, 2, 0), toInt(t, 3, 0)});
        else if (k == "walls") wallRow = 0;
    }
    d.fromFile = true;
    return true;
}

bool saveStageData(const std::filesystem::path& p, const StageData& d) {
    std::error_code ec;
    std::filesystem::create_directories(p.parent_path(), ec);
    std::ofstream f(p);
    if (!f) return false;
    f << "# DMW3 PC Edition stage data v1 (edit in-game with F2, or by hand)\n";
    f << "# exit  x y w h TARGET [arriveX arriveY]\n";
    f << "# npc   talk  SPRITE x y dir \"Name\" \"Line one|Line two\"\n";
    f << "# npc   fight|boss SPRITE x y dir \"Name\" level\n";
    f << "# item  sword x y\n";
    f << "cell " << d.cell << "\nsize " << d.cols << ' ' << d.rows << "\n";
    if (d.spawnX >= 0) f << "spawn " << d.spawnX << ' ' << d.spawnY << "\n";
    for (auto& e : d.exits) {
        f << "exit " << e.x << ' ' << e.y << ' ' << e.w << ' ' << e.h << ' ' << e.target;
        if (e.arriveX >= 0) f << ' ' << e.arriveX << ' ' << e.arriveY;
        f << "\n";
    }
    for (auto& n : d.npcs) {
        f << "npc " << npcKindName(n.kind) << ' ' << n.sprite << ' ' << n.x << ' ' << n.y << ' ' << n.dir << " \""
          << n.name << "\" ";
        if (n.kind == NpcKind::Talk) f << '"' << n.text << "\"\n";
        else f << n.level << "\n";
    }
    for (auto& it : d.items) f << "item " << it.id << ' ' << it.x << ' ' << it.y << "\n";
    f << "walls\n";
    for (int y = 0; y < d.rows; ++y) {
        std::string row(size_t(d.cols), '.');
        for (int x = 0; x < d.cols; ++x)
            if (d.wall[size_t(y) * d.cols + x]) row[size_t(x)] = '#';
        f << row << "\n";
    }
    return bool(f);
}

}  // namespace dw3
