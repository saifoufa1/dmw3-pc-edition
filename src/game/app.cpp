#include "game/app.h"

#include <cctype>
#include <cstdio>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <sstream>

#include "core/bytes.h"

#include "game/screen_style.h"
#include "ui/ui_draw.h"

namespace fs = std::filesystem;

namespace dw3 {

namespace {
// Win32 virtual-key codes (Input::key / keyP).
enum : int {
    kVkBack = 0x08, kVkTab = 0x09, kVkReturn = 0x0D, kVkShift = 0x10, kVkEscape = 0x1B, kVkSpace = 0x20,
    kVkLeft = 0x25, kVkUp = 0x26, kVkRight = 0x27, kVkDown = 0x28, kVkMinus = 0xBD, kVkSubtract = 0x6D,
};

// Sign-up letter grid (gamepad / arrow keys). The last row ends with DEL and OK (two cells each).
const char* const kGridRows[] = {"ABCDEFGHIJKLM", "NOPQRSTUVWXYZ", "abcdefghijklm", "nopqrstuvwxyz", "0123456789-"};
constexpr int kGridRowsN = 5;
constexpr int kGridCols = 13;
int rowLen(int r) { return r == kGridRowsN - 1 ? 11 + 2 : 13; }  // last row: 11 chars + DEL + OK
// Cell kind on the last row: x 0..10 chars, 11 DEL, 12 OK.

enum MenuItem { kNewGame = 0, kContinue = 1, kOptions = 2, kQuit = 3 };
const char* kMenuLabels[] = {"New Game", "Continue", "Options", "Quit"};

constexpr int kDoneTicks = 70;  // "downloading" / "logging in" pause before the next screen

constexpr int kVkF4 = 0x73, kVkF11 = 0x7A;
constexpr int kOptCount = 8;      // Options items (the last one is Back)
constexpr float kOptW = 196;      // Options window width
const char* kPauseItems[] = {"Resume", "Save", "Options", "Return to Title", "Quit Game"};
constexpr int kPauseCount = 5;

bool inRect(float mx, float my, float x, float y, float w, float h) { return mx >= x && my >= y && mx < x + w && my < y + h; }

// Sign-up: the show/hide button right of the password box.
constexpr float kEyeX = 238, kEyeW = 36;

void stroke(float x, float y, float w, float h, uint32_t c) {
    ui::fillRect(x, y, w, 1, c);
    ui::fillRect(x, y + h - 1, w, 1, c);
    ui::fillRect(x, y, 1, h, c);
    ui::fillRect(x + w - 1, y, 1, h, c);
}
}  // namespace

// ------------------------------------------------------------------ setup

bool App::init(const fs::path& dataRoot, const fs::path& stageDir, const DebugOptions& dbg, const FlowOptions& flow,
               std::string& err) {
    root_ = dataRoot;
    stageDir_ = stageDir;
    dbg_ = dbg;
    flow_ = flow;
    ui::init(root_);  // original font + windows (shared with the game)
    std::string e;
    if (!titleBg_.loadPack(root_ / "DAT" / "SCREEN" / "TTCOMMPK.BIN", &e) ||
        !logo_.loadPack(root_ / "DAT" / "SCREEN" / "TLOGOUSA.BIN", &e)) {
        err = "title screen: " + e;
        return false;
    }
    if (!hexBg_.loadSheet(root_ / "DAT" / "SCREEN" / "CMNBGDCS.BIN", root_ / "DAT" / "SCREEN" / "CMNBGDTM.BIN", &e) ||
        !online_.loadPack(root_ / "DAT" / "NAMEENT" / "NE_DATA_.BIN", &e)) {
        err = "Digimon Online screens: " + e;
        return false;
    }
    if (!loadStarterPacks(stageDir_.parent_path() / "starter_sets.cfg", packs_, err)) return false;
    loadRegText();
    loadAreaNames();
    baseDir_ = Story::saveDir();
    // One-time move of the single save of earlier versions (account.cfg + story.sav in the save folder) to slot 1.
    std::error_code ec;
    if (fs::exists(baseDir_ / "account.cfg", ec) && !fs::exists(slotDir(0), ec)) {
        fs::create_directories(slotDir(0), ec);
        fs::rename(baseDir_ / "account.cfg", slotDir(0) / "account.cfg", ec);
        if (fs::exists(baseDir_ / "story.sav", ec)) fs::copy_file(baseDir_ / "story.sav", slotDir(0) / "story.sav", ec);
    }
    refreshSlots();
    loadOptions();

    const std::string& s = flow_.stopAt;
    auto pick = [&](bool used) {  // --slot N, else the first used / free slot
        if (flow_.slot > 0) return std::min(flow_.slot, style::kSlotCount) - 1;
        for (int i = 0; i < style::kSlotCount; ++i)
            if (slots_[size_t(i)].used == used) return i;
        return 0;
    };
    if (s == "menu") go(State::MainMenu);
    else if (s == "options") go(State::Options);
    else if (s == "slots" || s == "newslot") { slotsForNew_ = s == "newslot"; go(State::MainMenu); go(State::Slots); }
    else if (s == "signup" || s == "pack" || s == "newgame") { go(State::MainMenu); selectSlot(pick(false)); go(State::SignUp); }
    else if (s == "continue") {
        selectSlot(pick(true));
        if (!saveExists()) { err = "--flow continue: no save in " + slotDir(slot_).string(); return false; }
        go(State::LogIn);
    } else go(State::Title);
    return true;
}

// ------------------------------------------------------------------ save slots

fs::path App::slotDir(int slot) const { return baseDir_ / ("slot" + std::to_string(slot + 1)); }

// Reads the summary of a slot's save (account, area, play time, lead Digimon) without loading the game.
App::SlotInfo App::readSlot(int slot) const {
    SlotInfo si;
    std::ifstream f(slotDir(slot) / "story.sav");
    std::string line;
    if (!f || !std::getline(f, line) || line.rfind("dmw3-story", 0) != 0) return si;
    si.used = true;
    std::vector<std::pair<std::string, int>> team;  // name, level
    int active = 0;
    bool out = true;
    std::string stage;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::istringstream is(line);
        std::string k;
        is >> k;
        if (k == "account") std::getline(is >> std::ws, si.account);
        else if (k == "stage") is >> stage;
        else if (k == "playtime") is >> si.seconds;
        else if (k == "active") is >> active;
        else if (k == "partners") { int v = 1; is >> v; out = v != 0; }
        else if (k == "member") {
            std::string sprite;
            int level = 1;
            is >> sprite >> std::ws;
            if (std::isdigit(is.peek())) {
                int skip;
                is >> level;
                for (int i = 0; i < 6; ++i) is >> skip;  // exp hp mp atk def spd
            }
            std::string name;
            std::getline(is >> std::ws, name);
            team.push_back({name, level});
        }
    }
    auto it = areaNames_.find(stage);
    si.area = it != areaNames_.end() ? it->second : stage;
    if (!out || team.empty()) si.lead = "No partner yet";
    else {
        const auto& m = team[size_t(std::clamp(active, 0, int(team.size()) - 1))];
        si.lead = m.first + "  Lv " + std::to_string(m.second);
    }
    if (si.account.empty()) si.account = "?";
    return si;
}

void App::refreshSlots() {
    slots_.clear();
    for (int i = 0; i < style::kSlotCount; ++i) slots_.push_back(readSlot(i));
}

bool App::anySlotUsed() const {
    for (const auto& s : slots_)
        if (s.used) return true;
    return false;
}

void App::selectSlot(int slot) {
    slot_ = slot;
    Story::setSaveDir(slotDir(slot));
    accounts_ = std::make_unique<LocalAccountService>(slotDir(slot));
}

void App::loadAreaNames() {
    areaNames_.clear();
    std::ifstream f(stageDir_.parent_path() / "areas.cfg");
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream is(line);
        std::string id, name;
        is >> id >> std::ws;
        std::getline(is, name);
        if (!name.empty() && name.back() == '\r') name.pop_back();
        if (name.size() >= 2 && name.front() == '"') name = name.substr(1, name.find('"', 1) - 1);
        if (!id.empty()) areaNames_[id] = name;
    }
}

void App::shutdown() {
    game_.reset();
    titleBg_.release();
    logo_.release();
    hexBg_.release();
    online_.release();
    ui::shutdown();
}

// Original registration texts: DAT/COUNTRY/USA/USNAMEET.BIN = u32 count, count x u32 absolute offsets of
// 0-terminated DW3-encoded strings (see FORMATS.md "Message text").
void App::loadRegText() {
    regText_.clear();
    Bytes b;
    if (!readFile(root_ / "DAT" / "COUNTRY" / "USA" / "USNAMEET.BIN", b)) return;
    ByteView v(b);
    uint32_t n = 0;
    if (!v.u32(0, n) || n > 256) return;
    for (uint32_t i = 0; i < n; ++i) {
        uint32_t off = 0;
        std::vector<uint8_t> msg;
        if (v.u32(4 + i * 4, off))
            for (size_t q = off; q < b.size() && b[q] != 0; ++q) msg.push_back(b[q]);
        regText_.push_back(std::move(msg));
    }
}

bool App::saveExists() const {
    std::error_code ec;
    return slot_ >= 0 && fs::exists(slotDir(slot_) / "story.sav", ec) && accounts_ && !accounts_->rememberedAccount().empty();
}

void App::loadOptions() {
    std::ifstream f(baseDir_ / "options.cfg");
    std::string k;
    int v = 0;
    VideoOptions vo;
    while (f >> k >> v) {
        if (k == "fullscreen") fullscreen_ = v != 0;
        else if (k == "scale") vo.scale = ScaleMode(std::clamp(v, 0, 2));
        else if (k == "widescreen") vo.wide = v != 0;
        else if (k == "hires") vo.hiRes = v != 0;
        else if (k == "sharp") vo.sharp = v != 0;
        else if (k == "msaa") vo.msaa = v;
        else if (k == "crt") vo.crt = v != 0;
    }
    gfx::setVideoOptions(vo);
}

void App::saveOptions() const {
    std::error_code ec;
    fs::create_directories(baseDir_, ec);
    std::ofstream f(baseDir_ / "options.cfg");
    const VideoOptions& vo = gfx::videoOptions();
    f << "fullscreen " << (fullscreen_ ? 1 : 0) << "\n"
      << "scale " << int(vo.scale) << "\n"
      << "widescreen " << (vo.wide ? 1 : 0) << "\n"
      << "hires " << (vo.hiRes ? 1 : 0) << "\n"
      << "sharp " << (vo.sharp ? 1 : 0) << "\n"
      << "msaa " << vo.msaa << "\n"
      << "crt " << (vo.crt ? 1 : 0) << "\n";
}

void App::go(State s) {
    state_ = s;
    stateTicks_ = 0;
    term_.clear();
    termStart_ = ticks_;
    doneTicks_ = -1;
    switch (s) {
        case State::MainMenu:
            refreshSlots();
            menuItems_ = {kNewGame};
            if (anySlotUsed()) menuItems_.push_back(kContinue);
            menuItems_.push_back(kOptions);
            menuItems_.push_back(kQuit);
            menuSel_ = std::clamp(menuSel_, 0, int(menuItems_.size()) - 1);
            break;
        case State::SignUp:
            name_ = flow_.account.empty() && (flow_.stopAt == "pack" || flow_.stopAt == "newgame") ? "Tamer" : flow_.account;
            pass_ = flow_.password;
            showPass_ = false;
            field_ = 0;
            gridX_ = gridY_ = 0;
            signError_ = false;
            term_ = {"Welcome to Digimon Online.", "Register your account."};
            term_.push_back(slot_ >= 0 ? "Save slot " + std::to_string(slot_ + 1) + ". Type, or arrows." : "Type, or arrows + confirm.");
            break;
        case State::Slots: {
            refreshSlots();
            overwriteAsk_ = false;
            yes_ = false;
            slotSel_ = 0;
            if (!slotsForNew_) {  // Continue: start on the first save
                for (int i = 0; i < style::kSlotCount; ++i)
                    if (slots_[size_t(i)].used) { slotSel_ = i; break; }
            } else {  // New Game: start on the first empty slot
                for (int i = 0; i < style::kSlotCount; ++i)
                    if (!slots_[size_t(i)].used) { slotSel_ = i; break; }
            }
            break;
        }
        case State::PackSelect:
            packSel_ = flow_.selectPack > 0 ? std::min(flow_.selectPack, int(packs_.size())) - 1 : 0;
            confirm_ = false;
            break;
        case State::LogIn: {
            const AccountResult r = accounts_->resume(account_);
            if (r != AccountResult::Ok) term_ = {"Log-in failed:", accountResultText(r)};
            else term_ = {"Welcome back to Digimon Online.", "Account: " + account_.name, "Logging in..."};
            doneTicks_ = r == AccountResult::Ok ? kDoneTicks : -1;
            break;
        }
        case State::Pause: pauseSel_ = 0; break;
        case State::Options: optSel_ = 0; break;
        default: break;
    }
}

void App::startNewGame() {
    const StarterPack& p = packs_[size_t(packSel_)];
    FlowStart fs;
    fs.newGame = true;
    fs.account = account_.name;
    fs.pack = p.letter;
    for (const auto& m : p.members) fs.team.push_back({m.sprite, m.name});
    fs.stage = flow_.lobbyStage;
    game_ = std::make_unique<Game>();
    std::string err;
    if (!game_->initFlow(root_, stageDir_, fs, dbg_, err)) {
        game_.reset();
        error_ = err;
        go(State::MainMenu);
        return;
    }
    go(State::Playing);
}

void App::startContinue() {
    FlowStart fs;
    fs.newGame = false;
    fs.account = account_.name;
    game_ = std::make_unique<Game>();
    std::string err;
    if (!game_->initFlow(root_, stageDir_, fs, dbg_, err)) {
        game_.reset();
        error_ = err;
        go(State::MainMenu);
        return;
    }
    go(State::Playing);
}

// ------------------------------------------------------------------ update

void App::update(const Input& in) {
    ++ticks_;
    ++stateTicks_;
    mouseMoved_ = in.mx != lastMx_ || in.my != lastMy_;
    lastMx_ = in.mx;
    lastMy_ = in.my;
    if (in.keyP[kVkF11]) {  // borderless fullscreen toggle, anywhere
        fullscreen_ = !fullscreen_;
        saveOptions();
    }
    switch (state_) {
        case State::Title: updateTitle(in); break;
        case State::MainMenu: updateMenu(in); break;
        case State::Options: updateOptions(in); break;
        case State::Slots: updateSlots(in); break;
        case State::SignUp: updateSignUp(in); break;
        case State::PackSelect: updatePack(in); break;
        case State::LogIn: updateLogIn(in); break;
        case State::Pause: updatePause(in); break;
        case State::Playing:
            if (!game_) { go(State::Title); break; }
            if (in.keyP[kVkEscape] && game_->inField() && !game_->storyBusy()) { go(State::Pause); break; }
            game_->update(in);
            break;
        case State::Quit: break;
    }
    if (state_ != State::SignUp) {  // generic edge tracking for menus
        prevUp_ = in.up; prevDown_ = in.down; prevLeft_ = in.left; prevRight_ = in.right;
    }
}

void App::updateTitle(const Input& in) {
    if (stateTicks_ > 10 && (in.confirmP || in.keyP[kVkReturn] || in.lmbP)) go(State::MainMenu);
}

void App::updateMenu(const Input& in) {
    const int n = int(menuItems_.size());
    if (in.up && !prevUp_) menuSel_ = (menuSel_ + n - 1) % n;
    if (in.down && !prevDown_) menuSel_ = (menuSel_ + 1) % n;
    if (in.keyP[kVkEscape]) { go(State::Title); return; }
    bool click = false;
    for (int i = 0; i < n; ++i)  // mouse: the plates
        if (inRect(in.mx, in.my, style::kPlateX, style::kPlateY + style::kPlateStep * float(i), 164, 16)) {
            if (mouseMoved_) menuSel_ = i;
            if (in.lmbP) { menuSel_ = i; click = true; }
        }
    if (!(in.confirmP || in.keyP[kVkReturn] || click) || stateTicks_ < 5) return;
    error_.clear();
    switch (menuItems_[size_t(menuSel_)]) {
        case kNewGame: slotsForNew_ = true; go(State::Slots); break;
        case kContinue: slotsForNew_ = false; go(State::Slots); break;
        case kOptions: go(State::Options); break;
        case kQuit: state_ = State::Quit; break;
        default: break;
    }
}

// Save slot screen. New Game: any slot (a used one asks before it is overwritten). Continue: used slots only.
void App::updateSlots(const Input& in) {
    const int n = style::kSlotCount;
    bool ok = stateTicks_ > 5 && (in.confirmP || in.keyP[kVkReturn]);
    if (overwriteAsk_) {
        if ((in.left && !prevLeft_) || (in.right && !prevRight_) || (in.up && !prevUp_) || (in.down && !prevDown_)) yes_ = !yes_;
        for (int b = 0; b < 2; ++b)  // mouse: YES / NO
            if (inRect(in.mx, in.my, b == 0 ? 96.f : 166.f, 92 + 22, 44, 16)) {
                if (mouseMoved_) yes_ = b == 0;
                if (in.lmbP) { yes_ = b == 0; ok = true; }
            }
        if (in.keyP[kVkEscape] || (ok && !yes_)) { overwriteAsk_ = false; return; }
        if (ok && yes_) {
            std::error_code ec;
            fs::remove_all(slotDir(slotSel_), ec);  // the old account + save of this slot
            overwriteAsk_ = false;
            selectSlot(slotSel_);
            go(State::SignUp);
        }
        return;
    }
    auto step = [&](int d) {
        for (int k = 1; k <= n; ++k) {
            const int i = (slotSel_ + d * k + n * k) % n;
            if (slotsForNew_ || slots_[size_t(i)].used) { slotSel_ = i; return; }
        }
    };
    if (in.up && !prevUp_) step(-1);
    if (in.down && !prevDown_) step(1);
    if (in.keyP[kVkEscape] || in.rmbP) { go(State::MainMenu); return; }
    {  // mouse: the slot boxes
        const float x = (kScreenW - style::kSlotW) / 2;
        for (int i = 0; i < n; ++i)
            if ((slotsForNew_ || slots_[size_t(i)].used) &&
                inRect(in.mx, in.my, x, style::kSlotTop + (style::kSlotH + style::kSlotGap) * float(i), style::kSlotW, style::kSlotH)) {
                if (mouseMoved_) slotSel_ = i;
                if (in.lmbP && stateTicks_ > 5) { slotSel_ = i; ok = true; }
            }
    }
    if (!ok) return;
    const SlotInfo& si = slots_[size_t(slotSel_)];
    if (slotsForNew_) {
        if (si.used) { overwriteAsk_ = true; yes_ = false; return; }
        selectSlot(slotSel_);
        go(State::SignUp);
    } else if (si.used) {
        selectSlot(slotSel_);
        go(State::LogIn);
    }
}

std::vector<std::string> App::optionLabels() const {
    const VideoOptions& o = gfx::videoOptions();
    static const char* kScale[] = {"Fit", "Stretch", "Integer"};
    std::string aa = o.msaa == 0 ? "Off" : std::to_string(o.msaa) + "x";
    if (gfx::maxMsaa() == 0) aa = "n/a";
    return {std::string("Display: ") + (fullscreen_ ? "Fullscreen" : "Window"),
            std::string("Scale: ") + kScale[int(o.scale)],
            std::string("Widescreen: ") + (o.wide ? "On" : "Off"),
            std::string("Resolution: ") + (o.hiRes ? "High" : "Original"),
            std::string("Upscale: ") + (o.sharp ? "Sharp" : "Nearest"),
            "Anti-aliasing: " + aa,
            std::string("CRT filter: ") + (o.crt ? "On" : "Off"),
            "Back"};
}

// Cycles option `item` by dir (+1 / -1) and saves options.cfg.
void App::changeOption(int item, int dir) {
    VideoOptions o = gfx::videoOptions();
    switch (item) {
        case 0: fullscreen_ = !fullscreen_; break;
        case 1: o.scale = ScaleMode((int(o.scale) + 3 + dir) % 3); break;
        case 2: o.wide = !o.wide; break;
        case 3: o.hiRes = !o.hiRes; break;
        case 4: o.sharp = !o.sharp; break;
        case 5: {
            static const int kSteps[] = {0, 2, 4, 8};
            int i = 0;
            while (i < 3 && kSteps[i] < o.msaa) ++i;
            const int maxS = gfx::maxMsaa();
            do i = (i + 4 + dir) % 4; while (kSteps[i] > maxS);
            o.msaa = kSteps[i];
            break;
        }
        case 6: o.crt = !o.crt; break;
        default: return;
    }
    gfx::setVideoOptions(o);
    saveOptions();
}

void App::updateOptions(const Input& in) {
    if (in.up && !prevUp_) optSel_ = (optSel_ + kOptCount - 1) % kOptCount;
    if (in.down && !prevDown_) optSel_ = (optSel_ + 1) % kOptCount;
    bool ok = in.confirmP || in.keyP[kVkReturn];
    const int m = menuWindowItem(kOptCount, kOptW, in.mx, in.my);
    if (m >= 0 && mouseMoved_) optSel_ = m;
    if (m >= 0 && in.lmbP) { optSel_ = m; ok = true; }
    const bool back = in.keyP[kVkEscape] || in.rmbP || (optSel_ == kOptCount - 1 && ok);
    if (back) {
        if (optFromPause_ && game_) { go(State::Pause); pauseSel_ = 2; }
        else go(State::MainMenu);
        optFromPause_ = false;
        return;
    }
    if (ok || (in.right && !prevRight_)) changeOption(optSel_, 1);
    else if (in.left && !prevLeft_) changeOption(optSel_, -1);
}

int App::menuWindowItem(int count, float w, float mx, float my) const {
    if (w <= 0) w = style::kMenuW;
    const float h = 30 + style::kMenuLine * float(count);
    const float x = (kScreenW - w) / 2, y = (kScreenH - h) / 2;
    for (int i = 0; i < count; ++i)
        if (inRect(mx, my, x + 6, y + 22 + style::kMenuLine * float(i), w - 12, style::kMenuLine)) return i;
    return -1;
}

void App::updateSignUp(const Input& in) {
    // Keyboard typing goes straight into the focused field; the letter grid is for arrow keys / gamepad.
    std::string& f = field_ == 0 ? name_ : pass_;
    const size_t maxLen = field_ == 0 ? 12 : 16;
    const bool shift = in.key[kVkShift];
    bool typed = false;
    if (doneTicks_ < 0) {
        for (int vk = 'A'; vk <= 'Z'; ++vk)
            if (in.keyP[vk] && f.size() < maxLen) { f += char(shift ? vk : vk - 'A' + 'a'); typed = true; }
        for (int vk = '0'; vk <= '9'; ++vk)
            if (in.keyP[vk] && f.size() < maxLen) { f += char(vk); typed = true; }
        if ((in.keyP[kVkMinus] || in.keyP[kVkSubtract]) && f.size() < maxLen) { f += '-'; typed = true; }
        if (in.keyP[kVkSpace] && field_ == 0 && !f.empty() && f.size() < maxLen) { f += ' '; typed = true; }
        if (in.keyP[kVkBack] && !f.empty()) { f.pop_back(); typed = true; }
        if (in.keyP[kVkTab]) field_ = 1 - field_;
        if (typed) signError_ = false;
    }
    // grid navigation: arrow keys, or the gamepad d-pad (W/A/S/D are letters here)
    const bool padUp = in.up && !in.key['W'] && !in.key[kVkUp], padDown = in.down && !in.key['S'] && !in.key[kVkDown];
    const bool padLeft = in.left && !in.key['A'] && !in.key[kVkLeft], padRight = in.right && !in.key['D'] && !in.key[kVkRight];
    const bool up = in.keyP[kVkUp] || (padUp && !prevUp_), down = in.keyP[kVkDown] || (padDown && !prevDown_);
    const bool left = in.keyP[kVkLeft] || (padLeft && !prevLeft_), right = in.keyP[kVkRight] || (padRight && !prevRight_);
    prevUp_ = padUp; prevDown_ = padDown; prevLeft_ = padLeft; prevRight_ = padRight;
    if (doneTicks_ >= 0) {
        if (--doneTicks_ <= 0) go(State::PackSelect);
        return;
    }
    if (in.keyP[kVkEscape]) { go(State::MainMenu); return; }
    // the grid has an extra top row (-1) = the two fields
    if (up) { if (gridY_ > -1) --gridY_; else field_ = 0; }
    if (down) {
        if (gridY_ == -1 && field_ == 0) field_ = 1;
        else if (gridY_ < kGridRowsN - 1) ++gridY_;
    }
    if (gridY_ == -1) gridY_ = 0;  // fields are chosen with up at the top row
    if (left) gridX_ = (gridX_ + rowLen(gridY_) - 1) % rowLen(gridY_);
    if (right) gridX_ = (gridX_ + 1) % rowLen(gridY_);
    gridX_ = std::min(gridX_, rowLen(gridY_) - 1);

    bool submit = in.keyP[kVkReturn];
    if (in.keyP[kVkF4]) showPass_ = !showPass_;  // show / hide the password
    // Mouse: click a box to type into it, the Show/Hide button toggles the password, the grid cells type.
    bool gridClick = false;
    if (in.lmbP) {
        if (inRect(in.mx, in.my, style::kFieldX - 3, style::kNameY - 2, style::kFieldW, 14)) field_ = 0;
        else if (inRect(in.mx, in.my, style::kFieldX - 3, style::kPassY - 2, style::kFieldW, 14)) field_ = 1;
        else if (inRect(in.mx, in.my, kEyeX, style::kPassY - 2, kEyeW, 14)) showPass_ = !showPass_;
    }
    for (int r = 0; r < kGridRowsN; ++r)
        for (int c = 0; c < rowLen(r); ++c) {
            float x = style::kGridX + style::kCellW * float(c), w = style::kCellW;
            if (r == kGridRowsN - 1 && c == 11) { x += 4; w = 26; }
            else if (r == kGridRowsN - 1 && c == 12) { x = style::kGridX + style::kCellW * 11.f + 36; w = 20; }
            if (!inRect(in.mx, in.my, x - 2, style::kGridY + style::kCellH * float(r) - 1, w, style::kCellH)) continue;
            if (mouseMoved_ || in.lmbP) { gridX_ = c; gridY_ = r; }
            if (in.lmbP) gridClick = true;
        }
    // Grid select: gamepad confirm or a mouse click (Space/Enter on the keyboard type/submit instead).
    if ((in.confirmP && !in.keyP[kVkSpace] && !in.keyP[kVkReturn]) || gridClick) {
        if (gridY_ == kGridRowsN - 1 && gridX_ == 11) { if (!f.empty()) f.pop_back(); }
        else if (gridY_ == kGridRowsN - 1 && gridX_ == 12) submit = true;
        else if (f.size() < maxLen) f += kGridRows[gridY_][gridX_];
        signError_ = false;
    }
    const bool autoSubmit = (flow_.stopAt == "pack" || flow_.stopAt == "newgame") && stateTicks_ == 30;
    if (submit || autoSubmit) {
        // Enter submits from either field; the password is optional for local accounts.
        const AccountResult r = accounts_->signUp(name_, pass_, account_);
        if (r != AccountResult::Ok) {
            signMsg_ = accountResultText(r);
            signError_ = true;
        } else {
            term_ = {"Account " + account_.name + " created.", "Connecting to the Account Clerk..."};
            termStart_ = ticks_;
            doneTicks_ = 40;
        }
    }
}

void App::updatePack(const Input& in) {
    const int n = int(packs_.size());
    if (doneTicks_ >= 0) {  // "Registered / NOW ENTERING..."
        if (--doneTicks_ <= 0) startNewGame();
        return;
    }
    const bool autoPick = flow_.stopAt == "newgame" && stateTicks_ == 30;
    bool ok = (stateTicks_ > 5 && (in.confirmP || in.keyP[kVkReturn])) || autoPick;
    if (confirm_) {  // "Register account? YES / NO"
        if ((in.left && !prevLeft_) || (in.right && !prevRight_) || (in.up && !prevUp_) || (in.down && !prevDown_))
            yes_ = !yes_;
        for (int b = 0; b < 2; ++b)  // mouse: YES / NO
            if (inRect(in.mx, in.my, style::kTermTextX + (b == 0 ? 10.f : 70.f), style::kConfirmY + 26, 56, 16)) {
                if (mouseMoved_) yes_ = b == 0;
                if (in.lmbP) { yes_ = b == 0; ok = true; }
            }
        if (in.keyP[kVkEscape] || in.rmbP) { confirm_ = false; return; }
        if (ok || (flow_.stopAt == "newgame" && stateTicks_ == 50)) {
            confirm_ = false;
            if (yes_ || flow_.stopAt == "newgame") { doneTicks_ = kDoneTicks; termStart_ = ticks_; }
        }
        return;
    }
    // Original layout: the A/B/C tabs on the left are chosen with up/down (left/right also work).
    if ((in.up && !prevUp_) || (in.left && !prevLeft_)) packSel_ = (packSel_ + n - 1) % n;
    if ((in.down && !prevDown_) || (in.right && !prevRight_)) packSel_ = (packSel_ + 1) % n;
    if (in.keyP[kVkEscape]) { go(State::MainMenu); return; }
    // Mouse: hover / click the A B C tabs to look at a pack; click the pack panel to choose it.
    for (int t = 0; t < n && t < 3; ++t)
        if (inRect(in.mx, in.my, style::kTabX, style::kTabY + style::kTabStep * float(t), style::kTabW, style::kTabH)) {
            if (mouseMoved_ || in.lmbP) packSel_ = t;
        }
    if (in.lmbP && stateTicks_ > 5 && inRect(in.mx, in.my, style::kPackNameX - 4, style::kPackPanelY, 262, 190)) ok = true;
    if (ok) {
        confirm_ = true;
        yes_ = true;
    }
}

void App::updateLogIn(const Input& in) {
    if (doneTicks_ < 0) {  // failed: back to the menu
        if (in.confirmP || in.keyP[kVkEscape] || in.keyP[kVkReturn] || in.lmbP) go(State::MainMenu);
        return;
    }
    if (--doneTicks_ <= 0) startContinue();
}

void App::updatePause(const Input& in) {
    if (pauseMsgTicks_ > 0) --pauseMsgTicks_;
    if (in.keyP[kVkEscape]) { go(State::Playing); return; }
    if (in.up && !prevUp_) pauseSel_ = (pauseSel_ + kPauseCount - 1) % kPauseCount;
    if (in.down && !prevDown_) pauseSel_ = (pauseSel_ + 1) % kPauseCount;
    bool ok = in.confirmP || in.keyP[kVkReturn];
    const int m = menuWindowItem(kPauseCount, 0, in.mx, in.my);
    if (m >= 0 && mouseMoved_) pauseSel_ = m;
    if (m >= 0 && in.lmbP) { pauseSel_ = m; ok = true; }
    if (in.rmbP) { go(State::Playing); return; }
    if (!ok) return;
    switch (pauseSel_) {
        case 0: go(State::Playing); break;
        case 1:
            pauseMsg_ = game_ && game_->save() ? "Game saved." : "Save failed!";
            pauseMsgTicks_ = 120;
            break;
        case 2:
            optFromPause_ = true;
            go(State::Options);
            break;
        case 3:
            if (game_) game_->save();
            game_.reset();
            menuSel_ = 0;
            go(State::Title);
            break;
        case 4:
            if (game_) game_->save();
            state_ = State::Quit;
            break;
    }
}

// ------------------------------------------------------------------ rendering

void App::drawTitleBg() {
    for (int g = 0; g < 3; ++g) titleBg_.draw(g, 0, 0, 0, style::kTitleBgTint);
    // Sparks running along the circuit traces: TTCOMMPK group 3 holds short frame sequences (positions baked
    // into the pieces). Sequences = runs of frame ids; played one after another, staggered.
    static const int kSeq[][2] = {{15, 23}, {24, 32}, {33, 40}, {41, 49}, {50, 65}, {66, 81}, {82, 97}};
    for (int s = 0; s < 7; ++s) {
        const int len = kSeq[s][1] - kSeq[s][0] + 1;
        const long long t = (ticks_ / 3 + s * 17) % (len + 30);  // run, then pause
        if (t < len) titleBg_.draw(3, kSeq[s][0] + int(t), 0, 0);
    }
}

void App::drawOnlineBg() {
    ui::fillRect(0, 0, kScreenW, kScreenH, style::kOnlineBg);
    for (int l = 0; l < 3; ++l) {
        const float a = float(ticks_) * style::kHexSpeed[l] * 0.01f + float(l) * 2.1f;
        const float dx = -82.f + 90.f * std::sin(a), dy = 86.f + 100.f * std::sin(a * 0.7f + float(l));
        hexBg_.draw(0, l, std::floor(dx), std::floor(dy), style::kHexTint[l]);
    }
}

void App::drawTerminal(const std::vector<std::string>& lines, bool typewriter) {
    online_.draw(0, 0x01, 0, style::kTerminalY);
    long long budget = typewriter ? (ticks_ - termStart_) / style::kTypeTicks : 1 << 30;
    float y = style::kTerminalY + style::kTermTextY;
    float endX = style::kTermTextX, endY = y;
    for (const auto& l : lines) {
        if (budget <= 0) break;
        const std::string shown = l.substr(0, size_t(std::min<long long>(budget, (long long)l.size())));
        budget -= (long long)l.size();
        const int w = ui::drawTextFit(style::kTermTextX, y, shown, style::kTermTextW, style::kTermText);
        endX = style::kTermTextX + float(w);
        endY = y;
        y += style::kTermLine;
    }
    if ((ticks_ / 16) % 2 == 0) ui::drawText(endX + 1, endY, "_", style::kTermText);
}

void App::render(int ww, int wh) {
    const bool overGame = state_ == State::Playing || state_ == State::Pause || (state_ == State::Options && optFromPause_);
    if (overGame && game_) {
        game_->render(ww, wh);
        if (state_ != State::Playing) {
            gfx::clearHudText();  // the game's HUD labels stay under the menu
            gfx::beginVirtual();
            if (state_ == State::Pause) renderPause();
            else {
                ui::fillRect(0, 0, kScreenW, kScreenH, 0x80000000u);
                renderOptionsWindow();
            }
        }
        return;
    }
    gfx::beginFrame(ww, wh);
    switch (state_) {
        case State::Title: renderTitle(); break;
        case State::MainMenu: renderMenu(); break;
        case State::Options: renderOptions(); break;
        case State::Slots: renderSlots(); break;
        case State::SignUp: renderSignUp(); break;
        case State::PackSelect: renderPack(); break;
        case State::LogIn: renderLogIn(); break;
        default: break;
    }
    // fade in after every screen change
    if (stateTicks_ < 16) ui::fillRect(0, 0, kScreenW, kScreenH, uint32_t(255 - stateTicks_ * 16) << 24);
}

void App::renderTitle() {
    drawTitleBg();
    logo_.draw(0, 0x03, style::kLogoX, style::kLogoY);  // glow (additive)
    logo_.draw(0, 0x00, style::kLogoX, style::kLogoY);
    logo_.draw(0, 0x01, style::kWorldX, style::kWorldY);
    logo_.draw(0, 0x02, style::kThreeX, style::kThreeY);
    if ((stateTicks_ / style::kBlinkTicks) % 2 == 0) logo_.draw(0, 0x08, style::kPlateX, style::kPlateY);
    logo_.draw(0, 0x06, style::kCopyX, style::kCopyY);
}

void App::renderMenu() {
    drawTitleBg();
    logo_.draw(0, 0x03, style::kLogoX, style::kLogoY);
    logo_.draw(0, 0x00, style::kLogoX, style::kLogoY);
    logo_.draw(0, 0x01, style::kWorldX, style::kWorldY);
    logo_.draw(0, 0x02, style::kThreeX, style::kThreeY);
    for (size_t i = 0; i < menuItems_.size(); ++i) {
        const float y = style::kPlateY + style::kPlateStep * float(i);
        const bool sel = int(i) == menuSel_;
        const uint32_t tint = sel ? 0xffffffffu : style::kPlateDim;
        const int item = menuItems_[i];
        if (item == kContinue) logo_.draw(0, 0x0A, style::kPlateX, y, tint);  // original "Continue" plate
        else {  // our entries: the original empty plate (part 11) + the original font
            logo_.drawPart(0, 11, style::kPlateX, y, tint);
            const std::string label = kMenuLabels[item];
            const int w = ui::measureText(label);
            ui::drawText(style::kPlateX + 82 - float(w) / 2, y + 2, label, sel ? style::kPlateText : 0xffa08080u);
        }
        if (sel) logo_.draw(0, 0x07, style::kPlateX, y - 1);  // selection frame
    }
    if (menuItems_.size() <= 2) logo_.draw(0, 0x06, style::kCopyX, style::kCopyY);
    if (!error_.empty()) ui::drawTextFit(4, 228, error_, 312, style::kErrorText);
}

void App::drawMenuWindow(const char* title, const std::vector<std::string>& items, int sel, float w) {
    if (w <= 0) w = style::kMenuW;
    const float h = 30 + style::kMenuLine * float(items.size());
    const float x = (kScreenW - w) / 2, y = (kScreenH - h) / 2;
    ui::drawWindow(x, y, w, h);
    ui::drawText(x + 14, y + 8, title, ui::kYellow);
    for (size_t i = 0; i < items.size(); ++i) {
        const float iy = y + 24 + style::kMenuLine * float(i);
        if (int(i) == sel) ui::drawCursor(x + 10, iy, int(ticks_));
        ui::drawText(x + 24, iy, items[i], int(i) == sel ? ui::kWhite : ui::kGrey);
    }
}

void App::renderSlots() {
    drawTitleBg();
    ui::fillRect(0, 0, kScreenW, kScreenH, 0x90000000u);
    const float x = (kScreenW - style::kSlotW) / 2;
    const char* title = slotsForNew_ ? "New Game - choose a save slot" : "Continue - choose a save";
    ui::drawText(x + 4, style::kSlotTop - 18, title, ui::kYellow);
    for (int i = 0; i < style::kSlotCount; ++i) {
        const SlotInfo& si = slots_[size_t(i)];
        const float y = style::kSlotTop + (style::kSlotH + style::kSlotGap) * float(i);
        const bool sel = i == slotSel_;
        const bool enabled = slotsForNew_ || si.used;
        ui::drawWindow(x, y, style::kSlotW, style::kSlotH);
        if (sel) ui::fillRect(x + 4, y + 4, style::kSlotW - 8, style::kSlotH - 8, style::kSlotSel);
        if (sel) ui::drawCursor(x + 8, y + 6, int(ticks_));
        const uint32_t main = enabled ? (sel ? ui::kWhite : ui::kGrey) : style::kSlotEmpty;
        ui::drawText(x + 22, y + 6, std::to_string(i + 1), ui::kYellow);
        if (!si.used) {
            ui::drawText(x + 40, y + 6, "- Empty -", style::kSlotEmpty);
            continue;
        }
        char t[32];
        std::snprintf(t, sizeof t, "%lld:%02lld", si.seconds / 3600, (si.seconds / 60) % 60);
        // line 1: account + play time, line 2: area, line 3: lead Digimon + level
        const int tw = ui::measureText(std::string("Time ") + t);
        ui::drawTextFit(x + 40, y + 6, si.account, style::kSlotW - 60 - float(tw), main);
        ui::drawText(x + style::kSlotW - 18 - float(tw), y + 6, std::string("Time ") + t, main);
        ui::drawTextFit(x + 40, y + 18, si.area, style::kSlotW - 50, main);
        ui::drawTextFit(x + 40, y + 30, si.lead, style::kSlotW - 50, sel ? style::kGreenText : main);
    }
    const float hy = style::kSlotTop + (style::kSlotH + style::kSlotGap) * float(style::kSlotCount) + 2;
    ui::drawTextFit(x + 4, hy, "Up/Down: choose  Confirm: OK  Esc: back", style::kSlotW - 8, style::kTermText);
    if (overwriteAsk_) {
        const float wy = 92;
        ui::fillRect(0, 0, kScreenW, kScreenH, 0x80000000u);
        ui::drawWindow(70, wy, 180, 46);
        ui::drawText(82, wy + 8, "Overwrite save slot " + std::to_string(slotSel_ + 1) + "?", ui::kWhite);
        ui::drawText(110, wy + 26, "YES", yes_ ? ui::kYellow : ui::kGrey);
        ui::drawText(180, wy + 26, "NO", !yes_ ? ui::kYellow : ui::kGrey);
        ui::drawCursor(yes_ ? 98.f : 168.f, wy + 26, int(ticks_));
    }
}

void App::renderOptions() {
    drawTitleBg();
    renderOptionsWindow();
}

void App::renderOptionsWindow() {
    drawMenuWindow("Options", optionLabels(), optSel_, kOptW);
    ui::drawTextFit(8, 226, "Up/Down choose  Left/Right or click: change  F11 fullscreen", 304, style::kTermText);
}

void App::renderPause() {
    ui::fillRect(0, 0, kScreenW, kScreenH, 0x80000000u);
    drawMenuWindow("Menu", std::vector<std::string>(kPauseItems, kPauseItems + kPauseCount), pauseSel_);
    if (pauseMsgTicks_ > 0) {
        const int w = ui::measureText(pauseMsg_);
        ui::drawText((kScreenW - float(w)) / 2, 200, pauseMsg_, ui::kYellow);
    }
}

void App::renderSignUp() {
    drawOnlineBg();
    drawTerminal(term_, true);
    online_.draw(0, 0x38, style::kLogoOnlineX, style::kLogoOnlineY, style::kLogoOnlineTint);
    // entry panel
    ui::fillRect(style::kPanelX, style::kPanelY, style::kPanelW, style::kPanelH, style::kPanelFill);
    stroke(style::kPanelX, style::kPanelY, style::kPanelW, style::kPanelH, style::kPanelEdge);
    const bool blink = (ticks_ / 16) % 2 == 0;
    auto fieldRow = [&](int idx, float y, const char* label, const std::string& value, bool secret) {
        const bool focus = field_ == idx && doneTicks_ < 0;
        ui::drawText(style::kFieldLabelX, y, label, focus ? style::kTermText : style::kTermDim);
        stroke(style::kFieldX - 3, y - 2, style::kFieldW, 14, focus ? style::kSelEdge : style::kPanelEdge);
        // The password is never drawn as text unless the player asked for it (Show button / F4): one '*' per
        // character, drawn as a small pixel asterisk (the original font has no '*' glyph).
        int w = 0;
        if (secret) {
            static const char* kStar[5] = {"x.x.x", ".xxx.", "xxxxx", ".xxx.", "x.x.x"};
            for (size_t i = 0; i < value.size(); ++i, w += 7)
                for (int r = 0; r < 5; ++r)
                    for (int c = 0; c < 5; ++c)
                        if (kStar[r][c] == 'x')
                            ui::fillRect(style::kFieldX + float(w + c), y + 3 + float(r), 1, 1, style::kFieldText);
        } else {
            w = ui::drawText(style::kFieldX, y, value, style::kFieldText);
        }
        if (focus && blink) ui::drawText(style::kFieldX + float(w) + 1, y, "_", style::kFieldText);
    };
    fieldRow(0, style::kNameY, "Account", name_, false);
    fieldRow(1, style::kPassY, "Password", pass_, !showPass_);
    stroke(kEyeX, style::kPassY - 2, kEyeW, 14, style::kPanelEdge);  // show / hide toggle
    ui::drawText(kEyeX + 3, style::kPassY, showPass_ ? "Hide" : "Show", style::kTermDim);
    for (int r = 0; r < kGridRowsN; ++r)
        for (int c = 0; c < rowLen(r); ++c) {
            const bool last = r == kGridRowsN - 1;
            float x = style::kGridX + style::kCellW * float(c);
            float w = style::kCellW;
            std::string label;
            if (last && c == 11) { label = "DEL"; x += 4; w = 26; }
            else if (last && c == 12) { label = "OK"; x = style::kGridX + style::kCellW * 11.f + 36; w = 20; }
            else label = std::string(1, kGridRows[r][c]);
            const float y = style::kGridY + style::kCellH * float(r);
            const bool sel = r == gridY_ && c == gridX_ && doneTicks_ < 0;
            if (sel) stroke(x - 2, y - 1, w, style::kCellH + 1, style::kSelEdge);
            ui::drawText(x + (label.size() > 1 ? 0.f : 5.f), y, label, sel ? style::kSelEdge : style::kTermText,
                         label.size() > 1 ? UiFont::White : UiFont::White);
        }
    if (signError_) ui::drawTextFit(style::kPanelX + 6, style::kPanelY + style::kPanelH + 2, signMsg_, 260, style::kErrorText);
    (void)kGridCols;
}

// Draws "@N" (original USNAMEET message N, read from the disc) or our own text ('/' = new line).
void App::drawMsg(float x, float y, const std::string& ref, uint32_t color) {
    if (!ref.empty() && ref[0] == '@') {
        const int i = std::atoi(ref.c_str() + 1);
        if (i >= 0 && i < int(regText_.size()) && !regText_[size_t(i)].empty())
            ui::drawEncoded(x, y, regText_[size_t(i)].data(), regText_[size_t(i)].size(), color);
        else
            ui::drawText(x, y, "?", color);
        return;
    }
    std::string t = ref;
    std::replace(t.begin(), t.end(), '/', '\n');
    ui::drawText(x, y, t, color);
}

void App::renderPack() {
    drawOnlineBg();
    // header with the 1-2-3 registration steps (step 2 = starter pack)
    online_.draw(0, 0x0c, style::kPackHeadX, style::kPackHeadY);
    // The piece always lights step 1 (the step light is drawn by the original code): move it to step 2.
    stroke(style::kPackHeadX + 200, style::kPackHeadY + 1, 16, 14, style::kPanelEdge);
    stroke(style::kPackHeadX + 228, style::kPackHeadY + 1, 16, 14, style::kSelEdge);
    drawMsg(style::kPackHeadX + 10, style::kPackHeadY + 13, "@6", style::kGreenText);
    for (int k = 0; k < 3; ++k)  // step numbers "1" "2" "3"
        drawMsg(style::kPackHeadX + 207 + 28.f * float(k), style::kPackHeadY + 2, "@" + std::to_string(3 + k),
                k == 1 ? ui::kYellow : style::kTermText);
    online_.draw(0, 0x07, style::kPackPanelX, style::kPackPanelY);  // tabs + panel with three diamonds
    const StarterPack& p = packs_[size_t(packSel_)];
    for (int t = 0; t < int(packs_.size()) && t < 3; ++t) {
        const float ty = style::kTabY + style::kTabStep * float(t);
        if (t == packSel_) ui::fillRect(style::kTabX, ty, style::kTabW, style::kTabH, style::kTabSel);
        drawMsg(style::kTabX + 10, ty + 3, "@" + std::to_string(7 + t), t == packSel_ ? ui::kWhite : style::kTermText);
    }
    drawMsg(style::kPackNameX, style::kPackNameY, p.name, style::kGreenText);
    for (size_t m = 0; m < p.members.size() && m < 3; ++m) {
        const StarterMember& d = p.members[m];
        const float my = style::kMemberY + style::kMemberStep * float(m);
        if (d.icon >= 0) {
            const int f = d.icon + int((ticks_ / 10 + static_cast<long long>(m) * 3) % d.iconFrames);
            online_.draw(0, f, style::kPortraitX, my + style::kPortraitDy);
        }
        ui::drawText(style::kPackTextX, my, d.name, style::kGreenText);
        drawMsg(style::kPackTextX, my + 12, d.desc, ui::kWhite);
    }
    drawMsg(style::kPackNameX, style::kSummaryY, p.desc, ui::kWhite);
    if (confirm_ || doneTicks_ >= 0) {
        ui::fillRect(0, 0, kScreenW, kScreenH, 0x90000000u);
        online_.draw(0, 0x01, 0, style::kConfirmY);
        if (confirm_) {
            drawMsg(style::kTermTextX, style::kConfirmY + 9, "@22", style::kTermText);
            drawMsg(style::kTermTextX + 24, style::kConfirmY + 28, "@23", yes_ ? style::kSelEdge : style::kTermDim);
            drawMsg(style::kTermTextX + 84, style::kConfirmY + 28, "@24", !yes_ ? style::kSelEdge : style::kTermDim);
            ui::drawCursor(style::kTermTextX + (yes_ ? 12.f : 72.f), style::kConfirmY + 28, int(ticks_));
        } else {
            drawMsg(style::kTermTextX, style::kConfirmY + 9, "@30", style::kTermText);
            if ((ticks_ / 12) % 2 == 0) drawMsg(style::kTermTextX, style::kConfirmY + 28, "@29", style::kGreenText);
        }
    }
}

void App::renderLogIn() {
    drawOnlineBg();
    drawTerminal(term_, true);
    online_.draw(0, 0x38, style::kLogoOnlineX, style::kLogoOnlineY, style::kLogoOnlineTint);
}

}  // namespace dw3
