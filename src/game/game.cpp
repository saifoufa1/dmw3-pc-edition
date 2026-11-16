#include "game/game.h"

#include <algorithm>
#include <cmath>

#include "game/digimon_db.h"

namespace dw3 {

namespace {
constexpr int kTransTicks = 36;

// data/digivolve_paths.cfg: per-species stats, growth and digivolve path (shown in the HUD when broken).
void loadDigimonData(const std::filesystem::path& stageDir, std::string& error) {
    std::string e;
    if (!digimonDb().load(stageDir.parent_path() / "digivolve_paths.cfg", e)) error = e;
}
}

bool Game::init(const std::filesystem::path& dataRoot, const std::filesystem::path& stageDir,
                const std::string& startStage, const DebugOptions& dbg, std::string& err) {
    models_.setRoot(dataRoot);
    dbg_ = dbg;
    loadDigimonData(stageDir, error_);
    // Story: our chapter script (data/story) + the original text from the player's disc data.
    text_.setRoot(dataRoot);
    std::string stage = startStage;
    int sx = -1, sy = -1;
    if (!dbg.noStory) {
        std::string serr;
        if (story_.load(stageDir.parent_path() / "story" / "chapter1.story", &text_, serr)) {
            field_.attachStory(&story_);
            const bool skip = !dbg.storyFlags.empty() || !dbg.storyEvent.empty();
            if (!dbg.newGame && !skip && std::filesystem::exists(Story::defaultSavePath()) &&
                story_.loadGame(Story::defaultSavePath(), stage, sx, sy)) {
                if (dbg.stageGiven) { stage = startStage; sx = sy = -1; }
            } else {
                if (skip) { story_.abortEvents(); story_.setAutosave(false); }
                for (const auto& f : dbg.storyFlags) story_.setFlag(f, true);
                if (!dbg.stageGiven) { stage = story_.startStage(); sx = story_.startX(); sy = story_.startY(); }
            }
        } else {
            error_ = serr;  // shown in the HUD; the field still runs without a story
        }
    }
    if (!field_.init(dataRoot, stageDir, stage, err)) return false;
    if (dbg.posX >= 0) field_.warpTo(field_.stageId(), dbg.posX, dbg.posY);
    else if (sx >= 0 && field_.stageId() == stage) field_.warpTo(stage, sx, sy);
    field_.reloadPartner();
    if (!dbg.storyEvent.empty()) story_.runEvent(dbg.storyEvent);
    battle_.setSizeFile(stageDir.parent_path() / "battle_sizes.cfg");  // data/battle_sizes.cfg
    if (dbg.devTools || dbg.sizePanel) battle_.enableDevTools();
    if (dbg.startBattle) {
        const bool boss = dbg.battleSprite != "P171BAGM";
        BattleRequest r{dbg.battleSprite, boss ? "Digmon" : "Black Agumon", boss ? 8 : 3, boss};
        if (dbg.arena > 0) battle_.debugArena(dbg.arena);
        if (dbg.camYaw > -900.f) battle_.debugCamera(dbg.camYaw);
        if (!battle_.start(r, field_.stageId(), err)) return false;
        if (dbg.startForm > 0) battle_.debugSetForm(std::min(dbg.startForm, kMaxEvo));
        if (dbg.fullDv) battle_.debugFillDv();
        if (dbg.sizePanel) battle_.openSizePanel();
        mode_ = Mode::Battle;
    }
    return true;
}

bool Game::initFlow(const std::filesystem::path& dataRoot, const std::filesystem::path& stageDir, const FlowStart& fs,
                    const DebugOptions& dbg, std::string& err) {
    models_.setRoot(dataRoot);
    dbg_ = dbg;
    loadDigimonData(stageDir, error_);
    text_.setRoot(dataRoot);
    std::string stage = fs.stage.empty() ? "S210" : fs.stage;
    int sx = fs.x, sy = fs.y;
    std::string serr;
    if (!story_.load(stageDir.parent_path() / "story" / "chapter1.story", &text_, serr)) {
        err = serr;
        return false;
    }
    field_.attachStory(&story_);
    if (fs.newGame) {
        story_.newGameSkipOpening();
        story_.setAccount(fs.account);
        party_.team.clear();
        for (const auto& m : fs.team) party_.team.push_back(digimonDb().make(m.sprite, m.name));  // Lv 1, own stats
        party_.pack = fs.pack;
        party_.partnersOut = false;  // downloaded at the Tamer Service counter (story event "download")
        if (!party_.setActive(0)) { err = "starter pack has no Digimon"; return false; }
    } else {
        if (!story_.loadGame(Story::defaultSavePath(), stage, sx, sy)) {
            err = "cannot load " + Story::defaultSavePath().string();
            return false;
        }
        if (story_.account().empty()) story_.setAccount(fs.account);
    }
    if (!fs.account.empty()) text_.setPlayerName(fs.account);  // the tamer's name in the original text
    field_.setDevTools(dbg.devTools);
    if (!field_.init(dataRoot, stageDir, stage, err)) return false;
    if (sx >= 0) field_.warpTo(stage, sx, sy);
    if (dbg.flowX >= 0) field_.warpTo(field_.stageId(), dbg.flowX, dbg.flowY);
    field_.reloadPartner();
    if (!dbg.flowEvent.empty()) story_.runEvent(dbg.flowEvent);
    battle_.setSizeFile(stageDir.parent_path() / "battle_sizes.cfg");
    if (dbg.devTools) battle_.enableDevTools();
    if (fs.newGame) save();  // the account now has a save: Continue appears in the title menu
    return true;
}

bool Game::save() {
    if (!story_.loaded()) return false;
    field_.saveStory();
    return std::filesystem::exists(Story::defaultSavePath());
}

void Game::update(const Input& in) {
    if (++playFrames_ >= 60) { playFrames_ = 0; ++party_.playSeconds; }  // 60 Hz ticks -> play time
    switch (mode_) {
        case Mode::Field:
            if (in.keyP[0x74] && story_.loaded() && !story_.busy()) field_.saveStory();  // F5
            if (in.keyP[0x78] && story_.loaded() && !story_.busy()) {                    // F9
                std::string st;
                int x = -1, y = -1;
                if (story_.loadGame(Story::defaultSavePath(), st, x, y)) { field_.warpTo(st, x, y); field_.reloadPartner(); }
            }
            field_.update(in);
            if (auto req = field_.takeBattleRequest()) {
                pending_ = *req;
                mode_ = Mode::ToBattle;
                trans_ = 0;
            }
            break;
        case Mode::ToBattle:
            if (++trans_ >= kTransTicks) {
                if (battle_.start(pending_, field_.stageId(), error_)) {
                    mode_ = Mode::Battle;
                    if (dbg_.fullDv) battle_.debugFillDv();
                }
                else { field_.endBattle(true); mode_ = Mode::Field; }
            }
            break;
        case Mode::Battle:
            battle_.update(in);
            if (battle_.finished()) {
                field_.endBattle(battle_.won());
                mode_ = Mode::ToField;
                trans_ = kTransTicks;
            }
            break;
        case Mode::ToField:
            if (--trans_ <= 0) mode_ = Mode::Field;
            break;
    }
}

void Game::render(int ww, int wh) {
    gfx::beginFrame(ww, wh);
    const bool inBattle = mode_ == Mode::Battle;
    if (inBattle) battle_.render();
    else field_.render();

    if (mode_ == Mode::ToBattle) {
        // encounter flash: white strobes, then bars close in
        float t = float(trans_) / kTransTicks;
        if (trans_ < 12 && (trans_ / 3) % 2 == 0) gfx::fillRect(0, 0, kScreenW, kScreenH, 0xb0ffffffu);
        float h = kScreenH / 2.f * std::max(0.f, (t - 0.3f) / 0.7f);
        gfx::fillRect(0, 0, kScreenW, h, 0xff000000u);
        gfx::fillRect(0, kScreenH - h, kScreenW, h, 0xff000000u);
    } else if (mode_ == Mode::ToField) {
        gfx::fillRect(0, 0, kScreenW, kScreenH, (uint32_t(255.f * trans_ / kTransTicks) << 24));
    }

    gfx::beginHud(ww, wh);
    if (inBattle) battle_.renderHud(ww, wh);
    else if (mode_ == Mode::Field) field_.renderHud(ww, wh);
    if (!error_.empty() && mode_ == Mode::Field) gfx::drawText(10, wh - 30, error_.c_str(), 0xff6060ffu);
}

}  // namespace dw3
