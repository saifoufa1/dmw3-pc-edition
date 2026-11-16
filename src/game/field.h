// Field mode: walk the tamer + partner around a stage with walls, exits and NPCs.
// F2 opens the stage editor (paint walls, draw exits, place NPCs, set spawn).
#pragma once
#include <deque>
#include <map>
#include <filesystem>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "formats/field_stage.h"
#include "formats/stage_overlay.h"
#include "formats/stage_pack.h"
#include "formats/stage_tables.h"
#include "game/common.h"
#include "game/stage_data.h"
#include "game/story.h"

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

    // Story glue: the story decides events, story NPCs, gates and objectives (may be null).
    void attachStory(Story* s) { story_ = s; }
    // Developer tools (F2 editor, Q/E stage browser, debug HUD line). Off for players (PC flow without --dev).
    void setDevTools(bool on) { dev_ = on; }
    const std::string& stageId() const { return stageIds_[stageIndex_]; }
    int playerX() const { return int(player_.x); }
    int playerY() const { return int(player_.y); }
    bool warpTo(const std::string& stage, int x, int y);  // x < 0 = stage spawn
    void reloadPartner();
    void saveStory();
    // Test: walks through every door of these stages both ways with the game's own door code; one line per door
    // ("PASS S210 zone 2 -> S225 -> back at zone 2"), returns the number of failures.
    int doorSelfTest(const std::vector<std::string>& stages, std::vector<std::string>& report);

    // For the battle scene backdrop.
    const Texture& stageTexture() const { return stageTex_; }
    float camX() const { return camX_; }
    float camY() const { return camY_; }

private:
    struct NpcRt {
        NpcDef def;
        Actor actor;
        bool defeated = false;
        std::string storyId;  // story actor id ("" = from the stage file)
        int origSlot = -1;    // matching original WSTAG event actor slot (original talk lines)
        bool hidden = false;  // original actor without a sprite: an invisible blocker (e.g. in front of a counter)
    };
    enum class Tool { Walls, Exits, Npcs, Spawn };
    // A scene object from the WSTAG overlay drawn every frame (animated or additive glow).
    struct SceneAnim {
        int x = 0, y = 0;
        std::vector<int> pieces;  // piece index per animation frame
        int delay = 8, timer = 0;
        size_t cur = 0;
        bool additive = false;
        int depthY = 0;  // > 0: foreground piece, drawn over actors whose feet are above this line
        int link = 0;    // door piece of an area link (0x64..): slides open when the player uses the door
    };
    // Door / exit sequence: door opens -> tile wipe -> (new stage) area card -> fade in.
    struct DoorSeq {
        int phase = -1;  // -1 idle, 0 door opens, 1 wipe out, 2 area card, 3 card out
        int t = 0;
        DoorRecord door;
        std::string city, area;  // area card lines (data/areas.cfg)
    };
    // A teleport port (data/ports.cfg, ours): opens with a light effect when the player steps on it / confirms.
    struct Port {
        std::string stage;
        int x = 0, y = 0, r = 14;
        std::string target;  // "" = effect only
        int tx = -1, ty = -1;
    };
    struct PieceTex {
        Texture tex;
        int ox = 0, oy = 0;
        bool tried = false;
    };

    bool loadStage(size_t index, std::string& err, int arriveX = -1, int arriveY = -1);
    bool walkable(float x, float y) const;
    bool flagsHold(const std::vector<FlagCond>& l, int slot = 0) const;  // original game flags (story) hold
    int doorAt(float x, float y) const;   // index into doors_ of the usable warp under (x, y), -1 = none
    int playerDoor() const;               // door the player stands in or faces from right next to it
    void enterDoor(const DoorRecord& d);  // the transition sequence
    bool arriveThrough(const DoorRecord& d, std::string& err);  // load the target stage at the door's arrival
    void updateDoorSeq(const Input& in);
    void renderDoorSeq();
    void loadAreaNames();
    void loadPorts();
    int portAt(float x, float y) const;
    void placeParty(float x, float y);
    void findAutoSpawn(float& x, float& y) const;
    void loadOriginalStage(const std::string& id);  // MASK/TMPK + WSTAG overlay -> pieces, walk map
    void applyOriginalWalls();
    const PieceTex* pieceTex(int piece);
    void clearPieceTex();
    void buildNpcs();
    void startTalk(size_t npc);
    void handleStoryRequests();
    void updatePartner(float speed);
    void updateBeam();          // partner download: beams on the floor, the team appears one by one
    bool partnersVisible() const { return party_.partnersOut || beamT_ >= 0; }
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
    FieldStage fstage_;                 // original walk/trigger maps + scene pieces
    StageOverlay overlay_;              // original WSTAG tables (spawn, objects, event actors)
    std::vector<SceneAnim> anims_;
    std::vector<PieceTex> pieceTex_;

    Actor player_, partner_;
    std::vector<Actor> followers_;  // the other team members, walking behind the active partner
    int beamT_ = -1;                // >= 0: partner download running (ticks)
    std::vector<NpcRt> npcs_;
    std::deque<std::pair<float, float>> trail_;
    std::set<std::string> defeated_;
    std::optional<BattleRequest> battle_;
    int battleNpc_ = -1;
    int touchCooldown_ = 0;

    Story* story_ = nullptr;
    int storyRev_ = -1;
    std::string storyBattle_;  // story actor id of the running fight

    // dialogue (fallback when no story is attached)
    int talkNpc_ = -1;
    std::vector<std::string> talkPages_;
    size_t talkPage_ = 0;

    // stage transition
    std::vector<NpcRecord> origNpcs_;   // data/npcs/S###.npc (extracted original event actors)
    std::vector<DoorRecord> doors_;     // data/doors/S###.door + hand exits of stages without one
    bool origDoors_ = false;            // doors_ come from the extracted table (trigger zones), not from .stage exits
    DoorSeq seq_;
    // The original door marker: DAT/FIELD/FIELDCOM.BIN cell group 0, pieces 35-38 (red/white/blue spark right of
    // the head, origin +4,-36 from the feet), loaded once.
    struct MarkFrame { Texture tex; int ox = 0, oy = 0; };
    std::vector<MarkFrame> doorMark_;
    void loadDoorMark();
    std::string pendingStage_;          // story "on stage" trigger held back until the area card is gone
    std::map<std::string, std::string> areaNames_;
    std::vector<Port> ports_;
    int portIdx_ = -1, portT_ = -1;      // port effect running
    int lastPort_ = -1;                  // the port the player stands on (steps on -> opens once)

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
    bool dev_ = true;  // legacy test modes keep the tools; the PC flow turns them off unless --dev
    std::string status_;
    int statusTimer_ = 0;
    long long ticks_ = 0;
};

}  // namespace dw3
