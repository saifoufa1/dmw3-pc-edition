// Field mode: walk the tamer + partner around a stage with walls, exits, NPCs and items.
// F2 opens the stage editor (paint walls, draw exits, place NPCs/items, set spawn).
#pragma once
#include <deque>
#include <filesystem>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "formats/stage_pack.h"
#include "game/common.h"
#include "game/stage_data.h"

namespace dw3 {

struct BattleRequest {
    std::string enemySprite, enemyName;
    int level = 1;
    bool boss = false;
};

class Field {
public:
    Field(SpriteCache& sprites, Party& party) : sprites_(sprites), party_(party) {}
    ~Field();
    bool init(const std::filesystem::path& dataRoot, const std::filesystem::path& stageDir,
              const std::string& startStage, std::string& err);
    void update(const Input& in);
    void render();
    void renderHud(int windowW, int windowH);

    std::optional<BattleRequest> takeBattleRequest();
    void endBattle(bool won);

    // For the battle scene backdrop.
    const Texture& stageTexture() const { return stageTex_; }
    float camX() const { return camX_; }
    float camY() const { return camY_; }

private:
    struct NpcRt {
        NpcDef def;
        Actor actor;
        bool defeated = false;
    };
    enum class Tool { Walls, Exits, Npcs, Items, Spawn };

    bool loadStage(size_t index, std::string& err, int arriveX = -1, int arriveY = -1);
    bool walkable(float x, float y) const;
    void placeParty(float x, float y);
    void findAutoSpawn(float& x, float& y) const;
    void buildNpcs();
    void updatePartner(float speed);
    void updateCamera();
    void updateEditor(const Input& in);
    void renderEditor();
    std::string stageFile() const;
    std::string npcKey(size_t i) const;

    SpriteCache& sprites_;
    Party& party_;
    std::filesystem::path root_, stageDir_;
    std::vector<std::string> stageIds_;
    size_t stageIndex_ = 0;
    StageMap stage_;
    StageData data_;
    Texture stageTex_;

    Actor player_, partner_;
    std::vector<NpcRt> npcs_;
    std::deque<std::pair<float, float>> trail_;
    std::set<std::string> defeated_, pickedUp_;
    std::optional<BattleRequest> battle_;
    int battleNpc_ = -1;
    int touchCooldown_ = 0;

    // dialogue
    int talkNpc_ = -1;
    std::vector<std::string> talkPages_;
    size_t talkPage_ = 0;

    // stage transition
    int fade_ = 0;          // 0..20, >0 means fading
    bool fadingOut_ = false;
    ExitZone pendingExit_;

    // editor
    bool editor_ = false;
    Tool tool_ = Tool::Walls;
    int brush_ = 1;
    size_t editTarget_ = 0;
    std::vector<std::string> npcSprites_;
    size_t npcSprite_ = 0;
    NpcKind npcKind_ = NpcKind::Fight;
    bool dragging_ = false;
    float dragX_ = 0, dragY_ = 0;
    float mapMx_ = -1, mapMy_ = -1;

    float camX_ = 0, camY_ = 0;
    bool hud_ = true;
    std::string status_;
    int statusTimer_ = 0;
    long long ticks_ = 0;
};

}  // namespace dw3
