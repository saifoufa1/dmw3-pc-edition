// Real-time battle (Digimon World 1 spirit, direct control): the partner moves freely,
// has 3 attack buttons + guard, and digivolves when the DV gauge is full. Each stage is bigger.
#pragma once
#include <string>
#include <vector>

#include "game/common.h"
#include "game/field.h"

namespace dw3 {

class Battle {
public:
    Battle(SpriteCache& sprites, Party& party) : sprites_(sprites), party_(party) {}
    bool start(const BattleRequest& req, const Texture* backdrop, float bgCamX, float bgCamY, std::string& err);
    void update(const Input& in);
    void render();
    void renderHud(int windowW, int windowH);
    bool finished() const { return done_; }
    bool won() const { return won_; }

private:
    struct Fighter {
        Actor a;
        std::string name;
        int level = 1;
        float hp = 1, maxHp = 1, mp = 0, maxMp = 0, power = 1;
        int evo = 0, evoTimer = 0;
        float dv = 0;  // digivolve gauge 0..100
        int flash = 0, invuln = 0, stun = 0;
        float kbx = 0, kby = 0;
        int act = -1, actTimer = 0;  // current attack slot and its remaining ticks
        bool actHit = false;
        int cd[3] = {0, 0, 0};
        bool guarding = false, boss = false;
        int aiTimer = 60, dashTimer = 0;
        float scale = 1.f, deathFade = 1.f;
        uint32_t tint = 0xffffffffu;
        float cx() const { return a.x; }
        float cy() const { return a.y - 12.f * scale; }
    };
    struct Shot { float x, y, vx, vy, dmg, r; int owner, life; uint32_t color; };
    struct Spark { float x, y, vx, vy; int life, max; float r; uint32_t color; };
    struct Popup { float x, y; std::string text; int life; uint32_t color; };

    void updatePlayer(const Input& in);
    void updateEnemy();
    void startAttack(Fighter& f, int slot, int duration);
    void resolvePlayerAttack();
    void hit(Fighter& target, Fighter& attacker, float dmg, float fromX, float fromY);
    void fireShot(const Fighter& from, const Fighter& to, float dmg, float speed, float r, float angleOffset, uint32_t color, int owner);
    void burst(float x, float y, uint32_t color, int n, float speed);
    void setEvo(int evo);
    std::string attackName(int slot) const;
    std::string formName() const;
    void clampArena(Fighter& f);

    SpriteCache& sprites_;
    Party& party_;
    const Texture* backdrop_ = nullptr;
    float bgX_ = 0, bgY_ = 0;
    Fighter me_, foe_;
    std::vector<Shot> shots_;
    std::vector<Spark> sparks_;
    std::vector<Popup> popups_;
    enum class Phase { Intro, Fight, Ending } phase_ = Phase::Intro;
    int phaseTimer_ = 0, shake_ = 0, screenFlash_ = 0;
    long long ticks_ = 0;
    bool done_ = false, won_ = false;
    float baseMaxHp_ = 120;
    std::string banner_;
};

}  // namespace dw3
