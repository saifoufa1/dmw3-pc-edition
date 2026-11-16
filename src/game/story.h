// Story / event system: chapter state, story flags, the original game's own flags (WSTAG talk tables),
// events triggered by talking, touching fighters, entering zones/stages and battle results, plus a
// small script interpreter. The chapter script is our own editable text file (data/story/*.story);
// it only *references* original dialogue by message id (see story_text.h). The format is described
// in the header comment of data/story/chapter1.story.
#pragma once
#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "formats/stage_overlay.h"
#include "game/common.h"
#include "game/dialogue.h"
#include "game/stage_data.h"
#include "game/story_text.h"

namespace dw3 {

struct StoryCond {
    enum Kind { Flag, NotFlag, Answer, Won, NotWon, GFlag } kind = Flag;
    std::string name;
    int value = 0;
    uint16_t g = 0;
};

// A story-controlled NPC: present on `stage` while every `when` condition holds.
struct StoryActor {
    std::string id, stage;
    NpcDef def;              // def.text = line ref or our own text (talk actors without a talk event)
    std::string enemySprite, enemyName;  // fight actors: the Digimon that battles (default = the actor)
    std::vector<StoryCond> when;
    int slot = -1;           // >= 0: the original NPC of this actor slot (sprite / position from data/npcs)
};

struct StoryCmd {
    std::string op;
    std::vector<std::string> a;
    std::vector<StoryCond> cond;  // for if
    int jump = -1;                // if: target when false; else: target to skip
    int line = 0;
};

enum class StoryTrigger { Start, Talk, Touch, Win, Lose, Enter, Stage };

struct StoryEvent {
    std::string name;
    StoryTrigger trig = StoryTrigger::Start;
    std::string arg;  // actor id / stage id
    int x = 0, y = 0, w = 0, h = 0;
    bool hasRect = false, repeat = false;
    std::vector<StoryCond> when;
    std::vector<StoryCmd> cmds;
};

struct StoryGate {  // exit from `stage` to `target` is closed while `when` holds
    std::string stage, target, msg;
    std::vector<StoryCond> when;
};

struct Starter {
    std::string sprite, name;
};

class Story {
public:
    Story(SpriteCache& sprites, Party& party) : sprites_(sprites), party_(party) {}
    bool load(const std::filesystem::path& file, TextDb* text, std::string& err);
    bool loaded() const { return loaded_; }
    void newGame();
    // PC Edition New Game: the opening (login, partner download, meeting Teddy/Kail) is skipped; applies the
    // script's `newgame flags|done|objective` lines instead of running the start event.
    void newGameSkipOpening();
    static std::filesystem::path defaultSavePath();
    static std::filesystem::path saveDir();               // %APPDATA%/DMW3PC unless overridden
    static void setSaveDir(const std::filesystem::path& dir);  // tests: --save-dir
    const std::string& account() const { return account_; }
    void setAccount(const std::string& a) { account_ = a; }
    bool saveGame(const std::filesystem::path& p, const std::string& stage, int x, int y) const;
    bool loadGame(const std::filesystem::path& p, std::string& stage, int& x, int& y);

    // ---- queries
    const std::string& startStage() const { return startStage_; }
    int startX() const { return startX_; }
    int startY() const { return startY_; }
    std::vector<const StoryActor*> actorsOn(const std::string& stage) const;
    bool hasActorAt(const std::string& stage, int x, int y) const;  // any story actor (any condition) on that spot
    bool hasActorSlot(const std::string& stage, int slot) const;  // a present story actor replaces that original NPC
    int revision() const { return revision_; }
    int talkBank(const std::string& stage) const;  // -1 = none
    bool exitBlocked(const std::string& stage, const std::string& target, std::string& msg) const;
    const std::string& objective() const { return objective_; }

    // ---- triggers (true = an event started)
    bool onTalk(const std::string& actorId);
    bool onTouch(const std::string& actorId);
    void onBattleEnd(const std::string& actorId, bool won);
    void onStage(const std::string& stage);
    void onEnter(const std::string& stage, float x, float y);
    bool runEvent(const std::string& name);  // debug / tests
    void abortEvents();                      // drop running/queued events (tests, load)

    // ---- dialogue outside events
    void say(const std::string& speaker, const std::vector<std::string>& pages);
    bool sayRef(const std::string& refOrText, const std::string& fbSpeaker, const std::string& fbText);
    // Original NPC talk from the WSTAG talk table of a slot (evaluates show/talk conditions on gflags).
    bool sayOriginal(const std::string& stage, const StageOverlay& ov, int slot, const std::string& fbSpeaker);

    // ---- flags
    bool flag(const std::string& f) const { return flags_.count(f) > 0; }
    void setFlag(const std::string& f, bool on);
    uint16_t gflag(uint32_t key) const;
    void setGflag(uint32_t key, uint16_t v);
    static uint32_t gkey(const std::string& stage, int slot, uint16_t flag);  // flags < 0x100 are per actor

    // ---- runtime
    bool busy() const;
    void update(const Input& in);
    void render();                      // world pass (blackout, partner previews)
    void renderHud(int ww, int wh);     // HUD pass (dialogue, title cards, objective)

    // ---- requests consumed by the field
    std::optional<std::string> takeFight() { auto r = fight_; fight_.reset(); return r; }
    struct Warp { std::string stage; int x = -1, y = -1; };
    std::optional<Warp> takeWarp() { auto r = warp_; warp_.reset(); return r; }
    bool takePartnerChanged() { bool r = partnerChanged_; partnerChanged_ = false; return r; }
    struct Place { int x = 0, y = 0; };
    std::optional<Place> takePlace() { auto r = place_; place_.reset(); return r; }  // `place X Y`
    // `beam`: the field plays the partner download (beams on the floor) and calls beamDone() when finished.
    bool takeBeam() { bool r = beamReq_; beamReq_ = false; return r; }
    void beamDone() { if (wait_ == Wait::Beam) wait_ = Wait::None; }
    bool takeSaveRequest() { bool r = saveReq_ && autosave_; saveReq_ = false; return r; }
    void setAutosave(bool on) { autosave_ = on; }  // off for test runs (--story-flags / --story-event)
    const StoryActor* actor(const std::string& id) const;

private:
    enum class Wait { None, Dialogue, Title, Partner, Fight, Ticks, Beam };
    bool eval(const std::vector<StoryCond>& c, const std::string& stage = "", int slot = -1) const;
    bool start(StoryTrigger t, const std::string& arg, const std::string& stage = "", float x = 0, float y = 0);
    void begin(size_t eventIndex);
    void step();
    bool message(const std::string& tok, Message& m);  // ref or quoted own text
    void error(const std::string& s);

    SpriteCache& sprites_;
    Party& party_;
    TextDb* text_ = nullptr;
    Dialogue dlg_;
    bool loaded_ = false;
    std::string file_;

    // script
    std::string startStage_ = "S200";
    int startX_ = -1, startY_ = -1;
    std::vector<StoryActor> actors_;
    std::vector<StoryEvent> events_;
    std::vector<StoryGate> gates_;
    std::vector<Starter> starters_;
    std::map<std::string, int> talkBanks_;
    std::map<uint32_t, uint16_t> initG_;
    std::map<std::string, std::string> packBlock_;  // `packblock A 3 ...`: "$" in message refs -> the pack's block
    std::string chapterInit_;

    // state (saved)
    std::set<std::string> flags_, done_;
    std::map<uint32_t, uint16_t> g_;
    std::string objective_, chapter_;
    std::string account_;  // Digimon Online account the save belongs to
    std::vector<std::string> skipFlags_, skipDone_;
    std::string skipObjective_;

    // runner
    std::vector<size_t> queue_;
    int cur_ = -1;
    size_t pc_ = 0;
    Wait wait_ = Wait::None;
    int waitTicks_ = 0;
    int answer_ = 0;
    bool lastWon_ = false;
    bool blackout_ = false;
    std::string title_;
    int partnerSel_ = 0;
    bool prevL_ = false, prevR_ = false;
    long long ticks_ = 0;
    int revision_ = 0;
    std::vector<std::shared_ptr<SpriteSet>> starterSprites_;
    std::vector<Actor> starterActors_;
    std::optional<std::string> fight_;
    std::optional<Warp> warp_;
    std::optional<Place> place_;
    bool beamReq_ = false;
    bool partnerChanged_ = false, saveReq_ = false, autosave_ = true;
    std::string errorText_;
    int errorTimer_ = 0;
};

}  // namespace dw3
