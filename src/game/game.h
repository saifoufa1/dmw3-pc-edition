// Top-level game: owns the field and battle modes and the transitions between them.
#pragma once
#include <filesystem>
#include <string>

#include "game/battle.h"
#include "game/field.h"
#include "render/model_renderer.h"

namespace dw3 {

struct DebugOptions {
    bool startBattle = false;   // jump straight into a fight (testing)
    std::string battleSprite = "P171BAGM";
    bool fullDv = false;        // start battles with a full digivolve gauge
    int startForm = 0;          // start battles already digivolved (0 = Rookie .. 3 = Mega)
};

class Game {
public:
    Game() : field_(sprites_, party_), battle_(sprites_, models_, party_) {}
    bool init(const std::filesystem::path& dataRoot, const std::filesystem::path& stageDir,
              const std::string& startStage, const DebugOptions& dbg, std::string& err);
    void update(const Input& in);
    void render(int windowW, int windowH);

private:
    enum class Mode { Field, ToBattle, Battle, ToField };
    SpriteCache sprites_;
    ModelCache models_;
    Party party_;
    Field field_;
    Battle battle_;
    Mode mode_ = Mode::Field;
    int trans_ = 0;
    BattleRequest pending_;
    std::string error_;
    DebugOptions dbg_;
};

}  // namespace dw3
