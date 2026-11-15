// Top-level game: owns the field and battle modes and the transitions between them.
#pragma once
#include <filesystem>
#include <string>

#include "game/battle.h"
#include "game/field.h"

namespace dw3 {

struct DebugOptions {
    bool startBattle = false;   // jump straight into a fight (testing)
    std::string battleSprite = "P171BAGM";
};

class Game {
public:
    Game() : field_(sprites_, party_), battle_(sprites_, party_) {}
    bool init(const std::filesystem::path& dataRoot, const std::filesystem::path& stageDir,
              const std::string& startStage, const DebugOptions& dbg, std::string& err);
    void update(const Input& in);
    void render(int windowW, int windowH);

private:
    enum class Mode { Field, ToBattle, Battle, ToField };
    SpriteCache sprites_;
    Party party_;
    Field field_;
    Battle battle_;
    Mode mode_ = Mode::Field;
    int trans_ = 0;
    BattleRequest pending_;
    std::string error_;
};

}  // namespace dw3
