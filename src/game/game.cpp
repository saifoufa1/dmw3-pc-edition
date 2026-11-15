#include "game/game.h"

#include <cmath>

namespace dw3 {

namespace {
constexpr int kTransTicks = 36;
}

bool Game::init(const std::filesystem::path& dataRoot, const std::filesystem::path& stageDir,
                const std::string& startStage, const DebugOptions& dbg, std::string& err) {
    if (!field_.init(dataRoot, stageDir, startStage, err)) return false;
    if (dbg.startBattle) {
        const bool boss = dbg.battleSprite != "P171BAGM";
        BattleRequest r{dbg.battleSprite, boss ? "Digmon" : "Black Agumon", boss ? 8 : 3, boss};
        if (!battle_.start(r, &field_.stageTexture(), field_.camX(), field_.camY(), err)) return false;
        mode_ = Mode::Battle;
    }
    return true;
}

void Game::update(const Input& in) {
    switch (mode_) {
        case Mode::Field:
            field_.update(in);
            if (auto req = field_.takeBattleRequest()) {
                pending_ = *req;
                mode_ = Mode::ToBattle;
                trans_ = 0;
            }
            break;
        case Mode::ToBattle:
            if (++trans_ >= kTransTicks) {
                if (battle_.start(pending_, &field_.stageTexture(), field_.camX(), field_.camY(), error_)) mode_ = Mode::Battle;
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
}

}  // namespace dw3
