#include "game/story.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

#include "game/digimon_db.h"
#include "render/gl_renderer.h"

namespace fs = std::filesystem;

namespace dw3 {

namespace {
std::vector<std::string> tokenize(const std::string& line) {
    std::vector<std::string> t;
    size_t i = 0;
    while (i < line.size()) {
        while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
        if (i >= line.size() || line[i] == '#') break;
        std::string tok;
        if (line[i] == '"') {
            tok = "\"";  // keep a leading quote so quoted text is distinguishable from refs/keywords
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
bool quoted(const std::string& s) { return !s.empty() && s[0] == '"'; }
std::string unq(const std::string& s) { return quoted(s) ? s.substr(1) : s; }
int toInt(const std::string& s, int def = 0) {
    char* e = nullptr;
    const long v = std::strtol(s.c_str(), &e, 0);
    return (e && e != s.c_str()) ? int(v) : def;
}
// Our own text: '|' = new page, "\n" = new line.
std::vector<std::string> ownPages(const std::string& s) {
    std::vector<std::string> pages;
    std::string cur;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '|') { pages.push_back(cur); cur.clear(); }
        else if (s[i] == '\\' && i + 1 < s.size() && s[i + 1] == 'n') { cur += '\n'; ++i; }
        else cur += s[i];
    }
    pages.push_back(cur);
    return pages;
}
bool parseCond(const std::string& t, StoryCond& c) {
    if (t.empty()) return false;
    if (t == "won") { c.kind = StoryCond::Won; return true; }
    if (t == "!won" || t == "lost") { c.kind = StoryCond::NotWon; return true; }
    if (t.rfind("answer=", 0) == 0) { c.kind = StoryCond::Answer; c.value = toInt(t.substr(7)); return true; }
    if (t.rfind("g:", 0) == 0) {  // g:6004=1  original game flag
        const size_t eq = t.find('=');
        c.kind = StoryCond::GFlag;
        c.g = uint16_t(std::strtoul(t.substr(2, eq == std::string::npos ? std::string::npos : eq - 2).c_str(), nullptr, 16));
        c.value = eq == std::string::npos ? 1 : toInt(t.substr(eq + 1));
        return true;
    }
    if (t[0] == '!') { c.kind = StoryCond::NotFlag; c.name = t.substr(1); return true; }
    c.kind = StoryCond::Flag;
    c.name = t;
    return true;
}
std::vector<StoryCond> parseConds(const std::vector<std::string>& t, size_t from) {
    std::vector<StoryCond> out;
    for (size_t i = from; i < t.size(); ++i) {
        StoryCond c;
        if (parseCond(t[i], c)) out.push_back(c);
    }
    return out;
}
}  // namespace

// ------------------------------------------------------------------ loading

bool Story::load(const fs::path& file, TextDb* text, std::string& err) {
    text_ = text;
    std::ifstream f(file);
    if (!f) { err = "cannot open " + file.string(); return false; }
    file_ = file.filename().string();
    actors_.clear(); events_.clear(); gates_.clear(); starters_.clear(); talkBanks_.clear(); initG_.clear();
    packBlock_.clear();
    skipFlags_.clear(); skipDone_.clear(); skipObjective_.clear();
    std::string line;
    int ln = 0;
    StoryEvent* ev = nullptr;
    std::vector<size_t> ifs;
    auto bad = [&](const std::string& m) { err = file_ + ":" + std::to_string(ln) + ": " + m; return false; };
    while (std::getline(f, line)) {
        ++ln;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        auto t = tokenize(line);
        if (t.empty()) continue;
        const std::string& k = t[0];
        if (ev) {
            if (k == "end") {
                if (!ifs.empty()) return bad("missing endif");
                ev = nullptr;
                continue;
            }
            StoryCmd c;
            c.op = k;
            c.line = ln;
            c.a.assign(t.begin() + 1, t.end());
            if (k == "if") { c.cond = parseConds(t, 1); ifs.push_back(ev->cmds.size()); }
            else if (k == "else") {
                if (ifs.empty()) return bad("else without if");
                ev->cmds[ifs.back()].jump = int(ev->cmds.size()) + 1;
                ifs.back() = ev->cmds.size();
            } else if (k == "endif") {
                if (ifs.empty()) return bad("endif without if");
                ev->cmds[ifs.back()].jump = int(ev->cmds.size()) + 1;
                ifs.pop_back();
            }
            ev->cmds.push_back(c);
            continue;
        }
        if (k == "player" && t.size() > 1) { if (text_) text_->setPlayerName(unq(t[1])); }
        else if (k == "start" && t.size() >= 2) { startStage_ = t[1]; startX_ = t.size() > 3 ? toInt(t[2], -1) : -1; startY_ = t.size() > 3 ? toInt(t[3], -1) : -1; }
        else if (k == "talkbank" && t.size() >= 3) { for (size_t i = 2; i < t.size(); ++i) talkBanks_[t[i]] = toInt(t[1]); }
        else if (k == "gflag" && t.size() >= 3) initG_[uint32_t(std::strtoul(t[1].c_str(), nullptr, 16))] = uint16_t(toInt(t[2]));
        else if (k == "chapter") chapterInit_ = t.size() > 2 ? unq(t[2]) : "";
        else if (k == "starter" && t.size() >= 3) starters_.push_back({t[1], unq(t[2])});
        else if (k == "packblock" && t.size() >= 3) {  // packblock A 3 B 4 C 5 default 4
            for (size_t i = 1; i + 1 < t.size(); i += 2) packBlock_[t[i] == "default" ? "" : t[i]] = t[i + 1];
        }
        else if (k == "newgame" && t.size() >= 3) {  // what New Game (PC flow, opening skipped) starts with
            if (t[1] == "flags") for (size_t i = 2; i < t.size(); ++i) skipFlags_.push_back(t[i]);
            else if (t[1] == "done") for (size_t i = 2; i < t.size(); ++i) skipDone_.push_back(t[i]);
            else if (t[1] == "objective") skipObjective_ = unq(t[2]);
            else return bad("newgame expects flags|done|objective");
        }
        else if (k == "gate" && t.size() >= 4) {
            StoryGate g{t[1], t[2], unq(t[3]), {}};
            size_t i = 4;
            if (i < t.size() && t[i] == "if") g.when = parseConds(t, i + 1);
            gates_.push_back(g);
        } else if (k == "actor" && (t.size() >= 9 || (t.size() >= 7 && t[4] == "slot"))) {
            // actor ID STAGE talk|fight|boss SPRITE X Y DIR "Name" [level N] [line REF|"text"] [if CONDS...]
            // actor ID STAGE talk|fight|boss slot N "Name" ...   = the ORIGINAL NPC of that actor slot
            //                                                      (data/npcs: its sprite, position and facing)
            // actor ID STAGE talk slot N at X Y DIR "Name" ...     = that NPC's sprite at a spot of an event scene
            StoryActor a;
            a.id = t[1];
            a.stage = t[2];
            a.def.kind = t[3] == "fight" ? NpcKind::Fight : t[3] == "boss" ? NpcKind::Boss : NpcKind::Talk;
            size_t first = 9;
            if (t[4] == "slot") {
                a.slot = toInt(t[5]);
                a.def.x = a.def.y = -1;
                size_t n = 6;
                if (t.size() >= 11 && t[6] == "at") {
                    a.def.x = toInt(t[7]);
                    a.def.y = toInt(t[8]);
                    a.def.dir = toInt(t[9]) & 7;
                    n = 10;
                }
                a.def.name = unq(t[n]);
                first = n + 1;
            } else {
                a.def.sprite = t[4];
                a.def.x = toInt(t[5]);
                a.def.y = toInt(t[6]);
                a.def.dir = toInt(t[7]) & 7;
                a.def.name = unq(t[8]);
            }
            a.def.level = 1;
            for (size_t i = first; i < t.size(); ++i) {
                if (t[i] == "level" && i + 1 < t.size()) a.def.level = toInt(t[++i], 1);
                else if (t[i] == "line" && i + 1 < t.size()) a.def.text = t[++i];
                else if (t[i] == "enemy" && i + 2 < t.size()) { a.enemySprite = t[i + 1]; a.enemyName = unq(t[i + 2]); i += 2; }
                else if (t[i] == "if") { a.when = parseConds(t, i + 1); break; }
            }
            actors_.push_back(a);
        } else if (k == "event") {
            if (t.size() < 4 || t[2] != "on") return bad("expected: event NAME on TRIGGER ...");
            StoryEvent e;
            e.name = t[1];
            const std::string& tr = t[3];
            size_t i = 4;
            if (tr == "start") e.trig = StoryTrigger::Start;
            else if (tr == "talk") e.trig = StoryTrigger::Talk;
            else if (tr == "touch") e.trig = StoryTrigger::Touch;
            else if (tr == "win") e.trig = StoryTrigger::Win;
            else if (tr == "lose") e.trig = StoryTrigger::Lose;
            else if (tr == "stage") e.trig = StoryTrigger::Stage;
            else if (tr == "enter") e.trig = StoryTrigger::Enter;
            else return bad("unknown trigger '" + tr + "'");
            if (e.trig != StoryTrigger::Start) { if (i >= t.size()) return bad("trigger needs an argument"); e.arg = t[i++]; }
            if (e.trig == StoryTrigger::Enter && i + 3 < t.size() && t[i] != "if" && t[i] != "repeat") {
                e.x = toInt(t[i]); e.y = toInt(t[i + 1]); e.w = toInt(t[i + 2]); e.h = toInt(t[i + 3]);
                e.hasRect = true;
                i += 4;
            }
            for (; i < t.size(); ++i) {
                if (t[i] == "repeat") e.repeat = true;
                else if (t[i] == "if") {
                    for (size_t j = i + 1; j < t.size(); ++j) {
                        if (t[j] == "repeat") { e.repeat = true; continue; }
                        StoryCond c;
                        if (parseCond(t[j], c)) e.when.push_back(c);
                    }
                    break;
                }
            }
            events_.push_back(e);
            ev = &events_.back();
            ifs.clear();
        } else {
            return bad("unknown line '" + k + "'");
        }
    }
    if (ev) return bad("event '" + ev->name + "' has no 'end'");
    loaded_ = true;
    newGame();
    return true;
}

void Story::newGame() {
    flags_.clear();
    done_.clear();
    g_ = initG_;
    objective_.clear();
    chapter_ = chapterInit_;
    queue_.clear();
    cur_ = -1;
    wait_ = Wait::None;
    blackout_ = false;
    title_.clear();
    dlg_.close();
    ++revision_;
    party_.partnerId = "P006AGUM";
    party_.partnerName = "Agumon";
    party_.team.clear();
    party_.active = 0;
    party_.pack.clear();
    start(StoryTrigger::Start, "");
}

namespace {
fs::path g_saveDir;  // --save-dir override (tests); empty = %APPDATA%/DMW3PC
}

void Story::setSaveDir(const fs::path& dir) { g_saveDir = dir; }

fs::path Story::saveDir() {
    if (!g_saveDir.empty()) return g_saveDir;
    const char* app = std::getenv("APPDATA");
    fs::path base = app && *app ? fs::path(app) : fs::current_path();
    return base / "DMW3PC";
}

fs::path Story::defaultSavePath() { return saveDir() / "story.sav"; }

void Story::newGameSkipOpening() {
    newGame();
    abortEvents();  // newGame() already began the start event (login cutscene): drop it
    for (const auto& f : skipFlags_) flags_.insert(f);
    for (const auto& e : skipDone_) done_.insert(e);
    if (!skipObjective_.empty()) objective_ = skipObjective_;
    ++revision_;
}

bool Story::saveGame(const fs::path& p, const std::string& stage, int x, int y) const {
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    std::ofstream f(p);
    if (!f) return false;
    f << "dmw3-story 1\n";
    f << "stage " << stage << ' ' << x << ' ' << y << "\n";
    f << "partner " << party_.partnerId << ' ' << party_.partnerName << "\n";
    if (!account_.empty()) f << "account " << account_ << "\n";
    if (!party_.pack.empty()) f << "pack " << party_.pack << "\n";
    // member SPRITE LEVEL EXP HP MP ATK DEF SPD Name   (each Digimon keeps its own level, EXP and stats)
    for (const auto& m : party_.team)
        f << "member " << m.sprite << ' ' << m.level << ' ' << m.exp << ' ' << m.stats.hp << ' ' << m.stats.mp << ' '
          << m.stats.atk << ' ' << m.stats.def << ' ' << m.stats.spd << ' ' << m.name << "\n";
    if (!party_.team.empty()) f << "active " << party_.active << "\n";
    f << "partners " << (party_.partnersOut ? 1 : 0) << "\n";
    f << "playtime " << party_.playSeconds << "\n";
    f << "wins " << party_.wins << "\n";
    f << "level " << party_.level << " " << party_.exp << " " << party_.bits << "\n";
    f << "chapter " << chapter_ << "\n";
    f << "objective " << objective_ << "\n";
    for (auto& s : flags_) f << "flag " << s << "\n";
    for (auto& s : done_) f << "done " << s << "\n";
    for (auto& [k, v] : g_) { char b[32]; std::snprintf(b, sizeof b, "g %08x %u\n", k, unsigned(v)); f << b; }
    return bool(f);
}

bool Story::loadGame(const fs::path& p, std::string& stage, int& x, int& y) {
    std::ifstream f(p);
    std::string line;
    if (!f || !std::getline(f, line) || line.rfind("dmw3-story", 0) != 0) return false;
    flags_.clear();
    done_.clear();
    g_.clear();
    queue_.clear();
    cur_ = -1;
    wait_ = Wait::None;
    dlg_.close();
    blackout_ = false;
    party_.team.clear();
    party_.pack.clear();
    party_.active = 0;
    party_.partnersOut = true;  // old saves: partners already downloaded
    party_.playSeconds = 0;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const size_t sp = line.find(' ');
        const std::string k = line.substr(0, sp), v = sp == std::string::npos ? "" : line.substr(sp + 1);
        std::istringstream is(v);
        if (k == "stage") is >> stage >> x >> y;
        else if (k == "partner") { is >> party_.partnerId; std::getline(is >> std::ws, party_.partnerName); partnerChanged_ = true; }
        else if (k == "account") account_ = v;
        else if (k == "pack") party_.pack = v;
        else if (k == "member") {
            TeamMember m;
            is >> m.sprite >> std::ws;
            if (std::isdigit(is.peek()))  // new format: level exp hp mp atk def spd, then the name
                is >> m.level >> m.exp >> m.stats.hp >> m.stats.mp >> m.stats.atk >> m.stats.def >> m.stats.spd;
            else  // old saves: name only -> species base stats
                m.stats = digimonDb().make(m.sprite, "").stats;
            std::getline(is >> std::ws, m.name);
            party_.team.push_back(m);
        }
        else if (k == "active") is >> party_.active;
        else if (k == "partners") { int on = 1; is >> on; party_.partnersOut = on != 0; }
        else if (k == "playtime") is >> party_.playSeconds;
        else if (k == "wins") is >> party_.wins;
        else if (k == "level") is >> party_.level >> party_.exp >> party_.bits;
        else if (k == "chapter") chapter_ = v;
        else if (k == "objective") objective_ = v;
        else if (k == "flag") flags_.insert(v);
        else if (k == "done") done_.insert(v);
        else if (k == "g") { std::string key; unsigned val = 0; is >> key >> val; g_[uint32_t(std::strtoul(key.c_str(), nullptr, 16))] = uint16_t(val); }
    }
    if (!party_.team.empty()) party_.setActive(party_.active < int(party_.team.size()) ? party_.active : 0);
    ++revision_;
    return true;
}

// ------------------------------------------------------------------ queries

std::vector<const StoryActor*> Story::actorsOn(const std::string& stage) const {
    std::vector<const StoryActor*> out;
    for (const auto& a : actors_)
        if (a.stage == stage && eval(a.when)) out.push_back(&a);
    return out;
}

bool Story::hasActorAt(const std::string& stage, int x, int y) const {
    for (const auto& a : actors_)
        if (a.stage == stage && a.slot < 0 && std::abs(a.def.x - x) < 8 && std::abs(a.def.y - y) < 8) return true;
    return false;
}

bool Story::hasActorSlot(const std::string& stage, int slot) const {
    for (const auto& a : actors_)
        if (a.stage == stage && a.slot == slot && eval(a.when)) return true;
    return false;
}

const StoryActor* Story::actor(const std::string& id) const {
    for (const auto& a : actors_)
        if (a.id == id) return &a;
    return nullptr;
}

int Story::talkBank(const std::string& stage) const {
    auto it = talkBanks_.find(stage);
    return it == talkBanks_.end() ? -1 : it->second;
}

bool Story::exitBlocked(const std::string& stage, const std::string& target, std::string& msg) const {
    for (const auto& g : gates_)
        if (g.stage == stage && (g.target == target || g.target == "*") && eval(g.when)) { msg = g.msg; return true; }
    return false;
}

uint32_t Story::gkey(const std::string& stage, int slot, uint16_t flag) {
    if (flag >= 0x100) return flag;
    const uint32_t s = stage.size() > 1 ? uint32_t(std::atoi(stage.c_str() + 1)) & 0xFFF : 0;
    return 0x80000000u | (s << 16) | (uint32_t(slot & 0xFF) << 8) | flag;
}
uint16_t Story::gflag(uint32_t key) const {
    auto it = g_.find(key);
    return it == g_.end() ? 0 : it->second;
}
void Story::setGflag(uint32_t key, uint16_t v) {
    if (gflag(key) != v) { g_[key] = v; ++revision_; }
}
void Story::setFlag(const std::string& f, bool on) {
    const bool had = flags_.count(f) > 0;
    if (on && !had) flags_.insert(f);
    else if (!on && had) flags_.erase(f);
    else return;
    ++revision_;
}

bool Story::eval(const std::vector<StoryCond>& cs, const std::string& stage, int slot) const {
    for (const auto& c : cs) {
        bool ok = true;
        switch (c.kind) {
            case StoryCond::Flag: ok = flag(c.name); break;
            case StoryCond::NotFlag: ok = !flag(c.name); break;
            case StoryCond::Answer: ok = answer_ == c.value; break;
            case StoryCond::Won: ok = lastWon_; break;
            case StoryCond::NotWon: ok = !lastWon_; break;
            case StoryCond::GFlag: ok = gflag(gkey(stage, slot, c.g)) == c.value; break;
        }
        if (!ok) return false;
    }
    return true;
}

// ------------------------------------------------------------------ triggers

bool Story::start(StoryTrigger t, const std::string& arg, const std::string& stage, float x, float y) {
    bool any = false;
    for (size_t i = 0; i < events_.size(); ++i) {
        const auto& e = events_[i];
        if (e.trig != t || (!e.repeat && done_.count(e.name))) continue;
        if (t != StoryTrigger::Start && t != StoryTrigger::Enter && e.arg != arg) continue;
        if (t == StoryTrigger::Enter) {
            if (e.arg != stage) continue;
            if (e.hasRect && !(x >= e.x && x < e.x + e.w && y >= e.y && y < e.y + e.h)) continue;
        }
        if (!eval(e.when)) continue;
        if (std::find(queue_.begin(), queue_.end(), i) != queue_.end() || cur_ == int(i)) continue;
        queue_.push_back(i);
        any = true;
        if (t == StoryTrigger::Talk || t == StoryTrigger::Touch) break;  // one event per interaction
    }
    if (any && cur_ < 0) step();
    return any;
}

bool Story::onTalk(const std::string& id) { return !busy() && start(StoryTrigger::Talk, id); }
bool Story::onTouch(const std::string& id) { return !busy() && start(StoryTrigger::Touch, id); }
void Story::onStage(const std::string& stage) { start(StoryTrigger::Stage, stage); }
void Story::onEnter(const std::string& stage, float x, float y) {
    if (!busy()) start(StoryTrigger::Enter, "", stage, x, y);
}
void Story::onBattleEnd(const std::string& id, bool won) {
    lastWon_ = won;
    if (wait_ == Wait::Fight) { wait_ = Wait::None; }
    start(won ? StoryTrigger::Win : StoryTrigger::Lose, id);
    if (cur_ >= 0 && wait_ == Wait::None) step();
}
void Story::abortEvents() {
    queue_.clear();
    cur_ = -1;
    wait_ = Wait::None;
    blackout_ = false;
    title_.clear();
    dlg_.close();
    starterActors_.clear();
}

bool Story::runEvent(const std::string& name) {
    for (size_t i = 0; i < events_.size(); ++i)
        if (events_[i].name == name) {
            queue_.push_back(i);
            if (cur_ < 0) step();
            return true;
        }
    return false;
}

// ------------------------------------------------------------------ dialogue helpers

bool Story::message(const std::string& tok, Message& m) {
    if (quoted(tok)) {
        m = {};
        m.pages = ownPages(unq(tok));
        m.ok = true;
        return true;
    }
    std::string ref = tok;
    const size_t d = ref.find('$');
    if (d != std::string::npos) {  // dmg200:$:15 -> the starter pack's own event block (packblock lines)
        auto it = packBlock_.find(party_.pack);
        if (it == packBlock_.end()) it = packBlock_.find("");
        if (it == packBlock_.end()) return false;
        ref.replace(d, 1, it->second);
    }
    return text_ && text_->get(ref, m);
}

void Story::say(const std::string& speaker, const std::vector<std::string>& pages) {
    dlg_.open(speaker == "<player>" && text_ ? text_->playerName() : speaker, pages);
    if (wait_ == Wait::None) wait_ = Wait::Dialogue;
}

bool Story::sayRef(const std::string& ref, const std::string& fbSpeaker, const std::string& fbText) {
    Message m;
    if (ref.find('$') != std::string::npos && message(ref, m)) { say(m.speaker, m.pages); return true; }  // pack block
    if (TextDb::isRef(ref) && text_ && text_->get(ref, m)) { say(m.speaker, m.pages); return true; }
    if (!ref.empty() && !TextDb::isRef(ref)) { say(fbSpeaker, ownPages(unq(ref))); return true; }
    say(fbSpeaker, ownPages(fbText.empty() ? "..." : fbText));
    return false;
}

bool Story::sayOriginal(const std::string& stage, const StageOverlay& ov, int slot, const std::string& fbSpeaker) {
    const int bank = talkBank(stage);
    if (bank < 0 || !text_) return false;
    for (const auto& a : ov.actors) {  // first variant of this slot whose show-conditions hold
        if (a.slot != slot || a.talk.empty()) continue;
        bool show = true;
        for (const auto& c : a.show) show = show && gflag(gkey(stage, slot, c.flag)) == c.value;
        if (!show) continue;
        for (const auto& t : a.talk) {
            bool ok = true;
            for (const auto& c : t.cond) ok = ok && gflag(gkey(stage, slot, c.flag)) == c.value;
            if (!ok) continue;
            char ref[32];
            std::snprintf(ref, sizeof ref, "talk%02d:%u", bank, unsigned(t.msg));
            Message m;
            if (!text_->get(ref, m)) return false;
            for (const auto& s : t.set) setGflag(gkey(stage, slot, s.flag), s.value);
            say(m.speaker.empty() ? fbSpeaker : m.speaker, m.pages);
            return true;
        }
        return false;
    }
    return false;
}

void Story::error(const std::string& s) {
    errorText_ = s;
    errorTimer_ = 300;
    std::fprintf(stderr, "story: %s\n", s.c_str());
}

// ------------------------------------------------------------------ interpreter

bool Story::busy() const { return cur_ >= 0 || !queue_.empty() || dlg_.active() || wait_ != Wait::None; }

void Story::begin(size_t i) {
    cur_ = int(i);
    pc_ = 0;
    if (!events_[i].repeat) done_.insert(events_[i].name);
}

// Runs commands of the current event until one has to wait (dialogue, title, fight...).
void Story::step() {
    for (int guard = 0; guard < 4096; ++guard) {
        if (wait_ != Wait::None) return;
        if (cur_ < 0) {
            if (queue_.empty()) return;
            const size_t i = queue_.front();
            queue_.erase(queue_.begin());
            begin(i);
        }
        StoryEvent& e = events_[size_t(cur_)];
        if (pc_ >= e.cmds.size()) { cur_ = -1; continue; }
        const StoryCmd& c = e.cmds[pc_++];
        const auto& a = c.a;
        const std::string where = file_ + ":" + std::to_string(c.line) + ": ";
        if (c.op == "if") { if (!eval(c.cond)) pc_ = size_t(c.jump); }
        else if (c.op == "else") pc_ = size_t(c.jump);
        else if (c.op == "endif") {}
        else if (c.op == "stop") cur_ = -1;
        else if (c.op == "say") {
            // say REF ["Speaker" "fallback"]  |  say "Speaker" "Our own text"
            if (a.empty()) continue;
            Message m;
            if (!quoted(a[0])) {
                if (message(a[0], m)) say(m.speaker, m.pages);
                else {
                    say(a.size() > 1 ? unq(a[1]) : "", ownPages(a.size() > 2 ? unq(a[2]) : "..."));
                    if (a.size() < 3) error(where + "message " + a[0] + " not found in your game data");
                }
            } else {
                say(unq(a[0]), ownPages(a.size() > 1 ? unq(a[1]) : ""));
            }
        } else if (c.op == "ask") {
            // ask QUESTION OPTION1 OPTION2 ...   (each a ref or "own text")
            if (a.size() < 3) { error(where + "ask needs a question and 2+ options"); continue; }
            Message q;
            if (!message(a[0], q)) { q.pages = {"?"}; error(where + "question " + a[0] + " not found"); }
            std::vector<std::string> opts;
            for (size_t i = 1; i < a.size(); ++i) {
                Message m;
                std::string o = message(a[i], m) && !m.pages.empty() ? m.pages[0] : unq(a[i]);
                std::replace(o.begin(), o.end(), '\n', ' ');
                opts.push_back(o);
            }
            dlg_.openChoice(q.speaker, q.pages, opts, 0);
            wait_ = Wait::Dialogue;
            answer_ = -1;  // set when the box closes
        } else if (c.op == "set") { for (auto& f : a) setFlag(f, true); }
        else if (c.op == "clear") { for (auto& f : a) setFlag(f, false); }
        else if (c.op == "gset" && a.size() >= 2) setGflag(uint32_t(std::strtoul(a[0].c_str(), nullptr, 16)), uint16_t(toInt(a[1])));
        else if (c.op == "objective") objective_ = a.empty() ? "" : unq(a[0]);
        else if (c.op == "chapter") { chapter_ = a.size() > 1 ? unq(a[1]) : (a.empty() ? "" : unq(a[0])); }
        else if (c.op == "blackout") blackout_ = a.empty() || a[0] != "off";
        else if (c.op == "title") {
            title_.clear();
            for (auto& s : a) {
                Message m;
                std::string t = message(s, m) && !m.pages.empty() ? m.pages[0] : unq(s);
                title_ += (title_.empty() ? "" : "\n") + t;
            }
            wait_ = Wait::Title;
        } else if (c.op == "fight" && !a.empty()) { fight_ = a[0]; wait_ = Wait::Fight; }
        else if (c.op == "partner") {
            if (!a.empty() && a[0] == "choose") {
                if (starters_.empty()) { error(where + "no 'starter' lines"); continue; }
                starterActors_.clear();
                for (auto& s : starters_) {
                    Actor ac;
                    ac.sprite = sprites_.get(s.sprite);
                    ac.dir = S;
                    starterActors_.push_back(ac);
                }
                partnerSel_ = 0;
                for (size_t i = 0; i < starters_.size(); ++i)
                    if (starters_[i].sprite == party_.partnerId) partnerSel_ = int(i);
                wait_ = Wait::Partner;
            } else if (a.size() >= 2) {
                party_.partnerId = a[0];
                party_.partnerName = unq(a[1]);
                partnerChanged_ = true;
            }
        } else if (c.op == "place" && a.size() >= 2) {
            place_ = Place{toInt(a[0]), toInt(a[1])};  // move the player inside the current stage (no reload)
        } else if (c.op == "beam") {
            // partner download: the team beams in around the player (single-partner tests: the starters)
            if (party_.team.empty())
                for (const auto& st : starters_) party_.team.push_back(digimonDb().make(st.sprite, st.name));
            if (party_.team.empty()) { error(where + "beam: no team and no 'starter' lines"); continue; }
            party_.setActive(std::clamp(party_.active, 0, int(party_.team.size()) - 1));
            partnerChanged_ = true;
            beamReq_ = true;
            wait_ = Wait::Beam;
        } else if (c.op == "warp" && !a.empty()) warp_ = Warp{a[0], a.size() > 2 ? toInt(a[1], -1) : -1, a.size() > 2 ? toInt(a[2], -1) : -1};
        else if (c.op == "wait") { waitTicks_ = a.empty() ? 30 : toInt(a[0], 30); wait_ = Wait::Ticks; }
        else if (c.op == "save") saveReq_ = true;
        else if (c.op == "run" && !a.empty()) {
            for (size_t i = 0; i < events_.size(); ++i)
                if (events_[i].name == a[0]) queue_.insert(queue_.begin(), i);
        } else error(where + "unknown command '" + c.op + "'");
    }
}

void Story::update(const Input& in) {
    ++ticks_;
    if (errorTimer_ > 0) --errorTimer_;
    for (auto& s : starterActors_) s.tick();
    switch (wait_) {
        case Wait::Dialogue:
            dlg_.update(in);
            if (!dlg_.active()) {
                if (answer_ < 0) answer_ = dlg_.choice() + 1;
                wait_ = Wait::None;
            }
            break;
        case Wait::Title:
            if (in.confirmP) { title_.clear(); wait_ = Wait::None; }
            break;
        case Wait::Partner: {
            const int n = int(starters_.size());
            const bool l = in.left || in.up, r = in.right || in.down;
            if (l && !prevL_) partnerSel_ = (partnerSel_ + n - 1) % n;
            if (r && !prevR_) partnerSel_ = (partnerSel_ + 1) % n;
            for (size_t i = 0; i < starterActors_.size(); ++i) starterActors_[i].move = int(i) == partnerSel_ ? Move::Walk : Move::Idle;
            if (in.confirmP) {
                party_.partnerId = starters_[size_t(partnerSel_)].sprite;
                party_.partnerName = starters_[size_t(partnerSel_)].name;
                partnerChanged_ = true;
                answer_ = partnerSel_ + 1;
                starterActors_.clear();
                wait_ = Wait::None;
            }
            break;
        }
        case Wait::Ticks:
            if (--waitTicks_ <= 0) wait_ = Wait::None;
            break;
        default: break;
    }
    prevL_ = in.left || in.up;
    prevR_ = in.right || in.down;
    if (wait_ == Wait::None) step();
}

// ------------------------------------------------------------------ rendering

void Story::render() {
    if (blackout_ || wait_ == Wait::Title || wait_ == Wait::Partner)
        gfx::fillRect(0, 0, kScreenW, kScreenH, wait_ == Wait::Partner && !blackout_ ? rgba(0, 0, 0, 200) : rgba(0, 0, 0, 255));
    if (wait_ == Wait::Partner) {
        const int n = int(starterActors_.size());
        for (int i = 0; i < n; ++i) {
            const float cx = kScreenW * (i + 1) / float(n + 1), cy = 128;
            if (i == partnerSel_) gfx::drawGlow(cx, cy - 14, 34, rgba(255, 200, 80, 90));
            Actor a = starterActors_[size_t(i)];
            a.x = cx;
            a.y = cy;
            a.draw(0, 0, i == partnerSel_ ? 2.0f : 1.6f, i == partnerSel_ ? 0xffffffffu : 0xff909090u);
        }
    }
}

void Story::renderHud(int ww, int wh) {
    // objective line (always visible in the field when set)
    if (!objective_.empty() && !blackout_ && wait_ != Wait::Title && wait_ != Wait::Partner) {
        const std::string s = "Objective: " + objective_;
        const int w = int(s.size()) * gfx::kCharW + 20;
        const int x = ww - w - 10, y = 30;
        gfx::fillRect(float(x), float(y), float(w), 24.f, rgba(10, 20, 40, 190));
        gfx::fillRect(float(x), float(y), 3.f, 24.f, rgba(255, 200, 60, 255));
        gfx::drawText(x + 10, y + 17, s.c_str(), rgba(255, 230, 140));
        if (!chapter_.empty()) gfx::drawText(x + 10, y + 40, chapter_.c_str(), rgba(170, 190, 220));
    }
    if (wait_ == Wait::Title) {
        int cy = wh / 2 - 20;
        std::istringstream is(title_);
        std::string l;
        while (std::getline(is, l)) {
            gfx::drawTextCentered(ww / 2, cy, l.c_str(), rgba(255, 255, 255));
            cy += 26;
        }
        gfx::fillRect(float(ww / 2 - 160), float(wh / 2 - 46), 320.f, 2.f, rgba(90, 160, 255));
        gfx::fillRect(float(ww / 2 - 160), float(cy - 6), 320.f, 2.f, rgba(90, 160, 255));
        if ((ticks_ / 30) % 2 == 0) gfx::drawTextCentered(ww / 2, cy + 40, "Press U / Enter", rgba(150, 190, 255));
    }
    if (wait_ == Wait::Partner) {
        gfx::drawTextCentered(ww / 2, wh / 6, "Choose your partner Digimon", rgba(255, 230, 140));
        const int n = int(starters_.size());
        for (int i = 0; i < n; ++i) {
            int x, y;
            gfx::toWindow(kScreenW * (i + 1) / float(n + 1), 150, x, y);
            gfx::drawTextCentered(x, y, starters_[size_t(i)].name.c_str(), i == partnerSel_ ? rgba(255, 230, 90) : rgba(170, 170, 170));
        }
        gfx::drawTextCentered(ww / 2, wh - wh / 6, "Left/Right: choose   U/Enter: confirm", rgba(150, 190, 255));
    }
    dlg_.renderHud(ww, wh);
    if (errorTimer_ > 0) gfx::drawText(10, wh - 32, errorText_.c_str(), rgba(255, 90, 90));
}

}  // namespace dw3
