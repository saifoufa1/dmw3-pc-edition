// Top-level game: owns the field and battle modes and the transitions between them.
#pragma once
#include <filesystem>
#include <string>

#include "game/battle.h"
#include "game/field.h"
#include "game/story.h"
#include "game/story_text.h"
#include "render/model_renderer.h"

namespace dw3 {

struct DebugOptions {
    bool startBattle = false;   // jump straight into a fight (testing)
    std::string battleSprite = "P171BAGM";
    bool fullDv = false;        // start battles with a full digivolve gauge
    int startForm = 0;          // start battles already digivolved (0 = Rookie .. 3 = Mega)
    bool sizePanel = false;     // open the battle size dashboard (F3) at once
    bool devTools = false;      // --dev: developer tools (F3 size dashboard); off for players
    int arena = 0;              // --arena N: fight in battle arena N (1..55) instead of the area's own
    float camYaw = -999.f;      // --cam-yaw DEG: battle camera start angle
    // story (testing)
    bool stageGiven = false;    // --stage was passed: start there instead of the story start / save
    bool newGame = false;       // --new-game: ignore the save file
    bool noStory = false;       // --no-story: plain sandbox field (no chapter script)
    std::vector<std::string> storyFlags;  // --story-flags a,b,c: set before play (skips the intro)
    std::string storyEvent;     // --story-event NAME: run this event at once
    int posX = -1, posY = -1;   // --pos X Y: place the player
};

// How the PC flow (title menu) starts a play session.
struct FlowStart {
    bool newGame = true;           // false = Continue (load the save)
    std::string account;           // logged-in Digimon Online account
    std::string pack;              // New Game: starter pack letter
    std::vector<TeamMember> team;  // New Game: the pack's Digimon (first = active)
    std::string stage;             // New Game: lobby stage (MAGAMI Online Center)
    int x = -1, y = -1;            // New Game: spawn (-1 = the stage's decoded spawn)
};

class Game {
public:
    Game() : field_(sprites_, party_), battle_(sprites_, models_, party_), story_(sprites_, party_) {}
    bool init(const std::filesystem::path& dataRoot, const std::filesystem::path& stageDir,
              const std::string& startStage, const DebugOptions& dbg, std::string& err);
    // PC flow: New Game (opening skipped, party from the starter pack, spawn in the lobby) or Continue.
    bool initFlow(const std::filesystem::path& dataRoot, const std::filesystem::path& stageDir, const FlowStart& fs,
                  const DebugOptions& dbg, std::string& err);
    bool save();                   // writes the save (false if no story / failed)
    bool inField() const { return mode_ == Mode::Field; }
    bool storyBusy() const { return story_.busy(); }
    void update(const Input& in);
    void render(int windowW, int windowH);

private:
    enum class Mode { Field, ToBattle, Battle, ToField };
    SpriteCache sprites_;
    ModelCache models_;
    Party party_;
    Field field_;
    Battle battle_;
    TextDb text_;
    Story story_;
    Mode mode_ = Mode::Field;
    int trans_ = 0;
    BattleRequest pending_;
    std::string error_;
    DebugOptions dbg_;
};

}  // namespace dw3
