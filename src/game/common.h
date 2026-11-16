// Shared game types: input, sprite sets, actors, party state, digivolution stages.
#pragma once
#include <algorithm>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "formats/sprite_pack.h"
#include "render/gl_renderer.h"

namespace dw3 {

// Logical input for one 60 Hz tick. "*P" = pressed this tick (edge).
struct Input {
    bool up = false, down = false, left = false, right = false, run = false;
    bool confirm = false, atk1 = false, atk2 = false, atk3 = false, guard = false, digivolve = false;
    bool confirmP = false, atk1P = false, atk2P = false, atk3P = false, digivolveP = false;
    bool nextStageP = false, prevStageP = false, partnerP = false, hudP = false, editorP = false, equipP = false;
    bool key[256] = {};   // raw keyboard (Win32 virtual-key codes), for the editor
    bool keyP[256] = {};
    float mx = -1, my = -1;  // mouse in virtual 320x240 screen coords
    bool lmb = false, rmb = false, lmbP = false, rmbP = false, lmbR = false;
    // 3D battle camera: gamepad right stick (-1..1, x = turn, y = tilt) and mouse wheel notches.
    float camTurn = 0, camTilt = 0;
    int wheel = 0;
};

// 8 facings in screen space. Stored sprites cover S..N; the W side is drawn mirrored.
enum Dir { S, SE, E, NE, N, NW, W, SW };
// Animation groups inside a sprite pack: slot = group*5 + stored direction.
enum class Move { Idle = 0, Walk = 1, Run = 2, Action = 3 };

Dir dirFromVector(float dx, float dy);
void dirVector(Dir d, float& x, float& y);  // unit-ish screen vector (iso-squashed)

struct SpriteSet {
    std::string name;
    SpritePack pack;
    std::vector<Texture> tex;
    ~SpriteSet();
};

// Loads FIELD/SPRT/<id>.BIN once and keeps textures alive.
class SpriteCache {
public:
    explicit SpriteCache(std::filesystem::path root = {}) : root_(std::move(root)) {}
    void setRoot(std::filesystem::path r) { root_ = std::move(r); }
    std::shared_ptr<SpriteSet> get(const std::string& id, std::string* err = nullptr);
    std::vector<std::string> listMovable() const;  // sprite ids with a full idle/walk/run set
private:
    std::filesystem::path root_;
    std::map<std::string, std::shared_ptr<SpriteSet>> cache_;
};

struct Actor {
    std::shared_ptr<SpriteSet> sprite;
    float x = 0, y = 0;  // feet position (map pixels in field, screen pixels in battle)
    Dir dir = S;
    Move move = Move::Idle;
    int anim = -1, key = 0, timer = 0;

    void tick();
    bool actionDone() const;  // current one-shot animation reached its last key
    // scale grows from the feet; tint multiplies colour; flash adds a white overlay.
    void draw(float camX, float camY, float scale = 1.f, uint32_t tint = 0xffffffffu, float flash = 0.f) const;
    bool mirrored() const;
};

// Digivolution stages: each one is bigger and stronger.
struct EvoStage {
    const char* name;
    float scale;     // sprite size multiplier
    float power;     // damage multiplier
    float hpBonus;   // added to max HP when reached
    uint32_t tint;
};
const EvoStage& evoStage(int level);  // 0 = Rookie .. 3 = Mega
constexpr int kMaxEvo = 3;

// A Digimon's own stats (they grow per level, see DigimonDb; training can raise them later).
struct DigiStats {
    int hp = 120, mp = 60, atk = 10, def = 8, spd = 10;
};

// One Digimon of the player's team (3 in a starter pack). Each has its OWN level, EXP and stats.
struct TeamMember {
    std::string sprite, name;
    int level = 1, exp = 0;  // EXP toward the next level
    DigiStats stats;
};

// Persistent player/partner state shared by field and battle. Everything the player owns lives here (plus the
// account), so it can be serialized as one block later (local save now, server later).
struct Party {
    std::string partnerId = "P006AGUM";   // the ACTIVE partner (mirrors team[active] when a team exists)
    std::string partnerName = "Agumon";
    std::vector<TeamMember> team;          // starter pack members; empty = single partner (old saves, tests)
    int active = 0;
    std::string pack;                      // starter pack letter ("A", "B", "C"), "" = none
    bool partnersOut = true;               // false = not downloaded yet (new game, lobby): no partner on the field
    int wins = 0;
    long long playSeconds = 0;             // total play time (save slot screen)
    int level = 1, exp = 0, bits = 0;  // money; level/exp = single-partner mode only (no team: tests, old saves)
    static int expToNext(int level) { return 30 + 20 * level; }

    TeamMember* lead() { return team.empty() ? nullptr : &team[size_t(std::clamp(active, 0, int(team.size()) - 1))]; }
    const TeamMember* lead() const { return const_cast<Party*>(this)->lead(); }

    // Makes team member i the active partner. False if out of range.
    bool setActive(int i) {
        if (i < 0 || i >= int(team.size())) return false;
        active = i;
        partnerId = team[size_t(i)].sprite;
        partnerName = team[size_t(i)].name;
        return true;
    }
};

}  // namespace dw3
