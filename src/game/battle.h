// Real-time battle (Digimon World 1 spirit, direct control): the partner moves freely,
// has 3 attack buttons + guard, and digivolves when the DV gauge is full.
//
// The partner is drawn with the original DW3 3D battle model of its current form
// (Agumon -> Greymon -> MetalGreymon -> WarGreymon), using that form's own animation clips,
// so every form moves and attacks differently. Digivolution plays a full sequence:
// charge, white silhouette, morph to the new size, flash, the new form's roar.
#pragma once
#include <memory>
#include <string>
#include <vector>

#include "formats/model.h"
#include "game/common.h"
#include "game/field.h"
#include "render/model_renderer.h"

namespace dw3 {

// How an attack hurts: projectiles, melee, a lunge, or several strikes (one per animation segment).
enum class AtkFx { Shot, Spread, Orb, Melee, Dash, Combo };

struct FormAttack {
    const char* name;
    AtkFx fx;
    int clip;        // model clip played for this attack (the form's own animation)
    float mp;
    int cooldown;    // ticks
    float dmg;       // per hit
    float size;      // projectile radius / melee reach factor
    uint32_t color;
    const char* effect = nullptr;  // original effect model drawn as the projectile (DAT/EFFECT/MEFT####)
    float effectScale = 0.f;   // effect model units -> screen pixels
};

struct FormDef {
    const char* name;
    const char* model;  // DAT/MODEL prefix
    FormAttack atk[3];
};

class Battle {
public:
    Battle(SpriteCache& sprites, ModelCache& models, Party& party) : sprites_(sprites), models_(models), party_(party) {}
    bool start(const BattleRequest& req, const Texture* backdrop, float bgCamX, float bgCamY, std::string& err);
    void update(const Input& in);
    void render();
    void renderHud(int windowW, int windowH);
    bool finished() const { return done_; }
    bool won() const { return won_; }

    // Testing helpers.
    void debugFillDv() { me_.dv = 100.f; }
    void debugSetForm(int evo) { setEvo(evo); }

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
        int act = -1, actTimer = 0, actTotal = 0;  // current attack slot, remaining / total ticks
        std::vector<int> hitTicks;                 // ticks (from attack start) where the attack lands
        int hitsDone = 0;
        bool actHit = false;
        int cd[3] = {0, 0, 0};
        bool guarding = false, boss = false;
        int aiTimer = 60, dashTimer = 0;
        float scale = 1.f, deathFade = 1.f;
        uint32_t tint = 0xffffffffu;
        // 3D model (null = sprite)
        std::shared_ptr<GameModel> model;
        ClipPlayer anim;
        int clip = -1;
        float animSpeed = 1.f;
        float yaw = -1.2f;
        float heightPx = 24.f;  // on-screen height of the current form
        float bodyH() const { return model ? heightPx : 24.f * scale; }
        float cx() const { return a.x; }
        float cy() const { return a.y - bodyH() * 0.5f; }
    };
    struct Shot {
        float x, y, vx, vy, dmg, r;
        int owner, life;
        uint32_t color;
        // Original effect model (optional): played in place, oriented along the flight.
        std::shared_ptr<GameModel> fx;
        float fxScale = 0, fxT = 1, fxLoopFrom = 1, fxLoopTo = 1;
    };
    struct Spark { float x, y, vx, vy; int life, max; float r; uint32_t color; };
    struct Popup { float x, y; std::string text; int life; uint32_t color; };
    // Digivolution / de-digivolution sequence.
    struct EvoSeq {
        bool active = false, up = true;
        int t = 0, len = 0, from = 0, to = 0;
        float fromH = 0, toH = 0;
        bool swapped = false;
        float flash = 0;          // white overlay on the partner
        bool silhouette = false;  // pure white shape (mid-transformation)
    };

    void updatePlayer(const Input& in);
    void updateEnemy();
    void updateEvoSeq();
    void startEvoSeq(int to);
    void startPlayerAttack(int slot);
    void resolvePlayerAttack();
    void startAttack(Fighter& f, int slot, int duration);
    void hit(Fighter& target, Fighter& attacker, float dmg, float fromX, float fromY);
    void fireShot(const Fighter& from, const Fighter& to, float dmg, float speed, float r, float angleOffset, uint32_t color, int owner);
    void burst(float x, float y, uint32_t color, int n, float speed);
    void attachFx(Shot& s, const FormAttack& atk);
    void drawShotFx(const Shot& s, float camX, float camY);
    void setEvo(int evo);
    void setClip(Fighter& f, int clip, float speed, bool restart = false);
    void animateModel(Fighter& f);
    void drawFighter(Fighter& f, float camX, float camY);
    const FormDef* form() const;
    std::string attackName(int slot) const;
    std::string formName() const;
    std::string formNameOf(int evo) const;
    void clampArena(Fighter& f);

    SpriteCache& sprites_;
    ModelCache& models_;
    Party& party_;
    const Texture* backdrop_ = nullptr;
    float bgX_ = 0, bgY_ = 0;
    Fighter me_, foe_;
    std::vector<Shot> shots_;
    std::vector<Spark> sparks_;
    std::vector<Popup> popups_;
    EvoSeq seq_;
    float darken_ = 0;  // extra screen dim during digivolution
    enum class Phase { Intro, Fight, Ending } phase_ = Phase::Intro;
    int phaseTimer_ = 0, shake_ = 0, screenFlash_ = 0;
    long long ticks_ = 0;
    bool done_ = false, won_ = false;
    float baseMaxHp_ = 120;
    std::string banner_;
};

}  // namespace dw3
