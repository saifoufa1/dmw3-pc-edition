#include "formats/stage_tables.h"

#include <cstdio>
#include <fstream>
#include <sstream>

namespace dw3 {

std::string flagListText(const std::vector<FlagCond>& l) {
    if (l.empty()) return "-";
    std::string s;
    char b[24];
    for (const auto& c : l) {
        std::snprintf(b, sizeof b, "%s%04x=%u", s.empty() ? "" : ",", c.flag, c.value);
        s += b;
    }
    return s;
}

bool parseFlagList(const std::string& s, std::vector<FlagCond>& out) {
    out.clear();
    if (s == "-" || s.empty()) return true;
    std::istringstream is(s);
    std::string item;
    while (std::getline(is, item, ',')) {
        const size_t eq = item.find('=');
        if (eq == std::string::npos) return false;
        try {
            FlagCond c;
            c.flag = uint16_t(std::stoul(item.substr(0, eq), nullptr, 16));
            c.value = uint16_t(std::stoul(item.substr(eq + 1), nullptr, 10));
            out.push_back(c);
        } catch (...) {
            return false;
        }
    }
    return true;
}

bool loadNpcTable(const std::string& path, std::vector<NpcRecord>& out, std::string* err) {
    out.clear();
    std::ifstream f(path);
    if (!f) { if (err) *err = "cannot open " + path; return false; }
    std::string line;
    int no = 0;
    while (std::getline(f, line)) {
        ++no;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::istringstream is(line);
        std::string k;
        if (!(is >> k) || k[0] == '#' || k != "npc") continue;
        NpcRecord r;
        if (!(is >> r.slot >> r.charId >> r.sprite >> r.x >> r.y >> r.dir)) {
            if (err) *err = path + ":" + std::to_string(no) + ": bad npc line";
            return false;
        }
        std::string w;
        while (is >> w) {
            std::string v;
            if (!(is >> v)) break;
            if (w == "show") parseFlagList(v, r.show);
            else if (w == "talk") {  // COND/SET/MSG
                const size_t a = v.find('/'), b = a == std::string::npos ? a : v.find('/', a + 1);
                if (b == std::string::npos) continue;
                TalkEntry t;
                parseFlagList(v.substr(0, a), t.cond);
                parseFlagList(v.substr(a + 1, b - a - 1), t.set);
                t.msg = uint32_t(std::stoul(v.substr(b + 1), nullptr, 16));
                r.talk.push_back(std::move(t));
            }
        }
        out.push_back(std::move(r));
    }
    return true;
}

bool saveNpcTable(const std::string& path, const std::string& header, const std::vector<NpcRecord>& in) {
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    f << header;
    f << "# npc SLOT CHAR SPRITE X Y DIR [show F=V,..] [talk COND/SET/MSG]...   (flags hex, values decimal, MSG hex)\n";
    char b[64];
    for (const auto& r : in) {
        std::snprintf(b, sizeof b, "npc %d %d %s %d %d %d", r.slot, r.charId, r.sprite.c_str(), r.x, r.y, r.dir);
        f << b;
        if (!r.show.empty()) f << " show " << flagListText(r.show);
        for (const auto& t : r.talk) {
            std::snprintf(b, sizeof b, "/0x%x", t.msg);
            f << " talk " << flagListText(t.cond) << "/" << flagListText(t.set) << b;
        }
        f << "\n";
    }
    return bool(f);
}

bool loadDoorTable(const std::string& path, std::vector<DoorRecord>& out, std::string* err) {
    out.clear();
    std::ifstream f(path);
    if (!f) { if (err) *err = "cannot open " + path; return false; }
    std::string line;
    int no = 0;
    while (std::getline(f, line)) {
        ++no;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::istringstream is(line);
        std::string k, kind;
        if (!(is >> k) || k[0] == '#' || k != "door") continue;
        DoorRecord r;
        if (!(is >> r.index >> kind)) { if (err) *err = path + ":" + std::to_string(no) + ": bad door line"; return false; }
        std::string a;
        try {
            if (kind == "warp") { r.kind = 1; is >> r.target >> r.x >> r.y >> r.dir; }
            else if (kind == "event") { r.kind = 8; is >> a; r.arg = int(std::stoul(a, nullptr, 16)); }
            else if (kind == "kind") { is >> r.kind >> a >> r.x >> r.y >> r.dir; r.arg = int(std::stoul(a, nullptr, 16)); }
            std::string w, v;
            while (is >> w >> v) {
                if (w == "link") r.link = int(std::stoul(v, nullptr, 16));
                else if (w == "if") parseFlagList(v, r.cond);
            }
        } catch (...) {
            if (err) *err = path + ":" + std::to_string(no) + ": bad door line";
            return false;
        }
        out.push_back(std::move(r));
    }
    return true;
}

bool saveDoorTable(const std::string& path, const std::string& header, const std::vector<DoorRecord>& in) {
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    f << header;
    f << "# door INDEX warp STAGE X Y DIR [link 0xNN] [if F=V,..] | door INDEX event 0xID [if ..] | door INDEX kind K 0xARG X Y DIR\n";
    f << "# INDEX = trigger zone value & 31 (zones: dw3check --dump-field STAGE out.bmp --overlay)\n";
    char b[128];
    for (const auto& r : in) {
        if (r.kind == 1) std::snprintf(b, sizeof b, "door %d warp %s %d %d %d", r.index, r.target.c_str(), r.x, r.y, r.dir);
        else if (r.kind == 8) std::snprintf(b, sizeof b, "door %d event 0x%x", r.index, r.arg);
        else std::snprintf(b, sizeof b, "door %d kind %d 0x%x %d %d %d", r.index, r.kind, r.arg, r.x, r.y, r.dir);
        f << b;
        if (r.link) { std::snprintf(b, sizeof b, " link 0x%x", r.link); f << b; }
        if (!r.cond.empty()) f << " if " << flagListText(r.cond);
        f << "\n";
    }
    return bool(f);
}

}  // namespace dw3
