// Shared game types: input, sprite sets, actors, party state, digivolution stages.
#pragma once
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

// Persistent player/partner state shared by field and battle.
struct Party {
    std::string partnerId = "P006AGUM";
    std::string partnerName = "Agumon";
    int wins = 0;
};

}  // namespace dw3
