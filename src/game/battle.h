// Real-time battle (Digimon World 1 spirit, direct control) in the original DW3 3D arenas.
//
// Each field area fights in its own 3D battle arena (DAT/EFFECT/MEFT10xx, formats/arena.h),
// drawn whole with a perspective camera that orbits the fight (Q/E, right mouse drag or the
// right stick turn it; wheel / PgUp / PgDn zoom). Fighters stand on the arena floor and move
// freely on the ground plane (WASD relative to the camera); projectiles and effects fly in 3D.
//
// The partner has 3 attack buttons + guard, and digivolves when the DV gauge is full. A
// digivolved partner keeps its form for the rest of the battle (hold the digivolve button to go
// back to Rookie). It is drawn with the original DW3 3D battle model of its current form
// (Agumon -> Greymon -> MetalGreymon -> WarGreymon), using that form's own animation clips.
// Digivolution plays a full sequence: charge, white silhouette, morph to the new size, flash,
// the new form's roar. Enemies use their own battle model too when one exists (kEnemyModels in
// battle.cpp), falling back to the field sprite (drawn as a billboard).
#pragma once
#include <algorithm>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "formats/arena.h"
#include "formats/model.h"
#include "game/common.h"
#include "game/field.h"
#include "render/camera3d.h"
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
    float effectScale = 0.f;   // effect model units -> 2D-era screen pixels (x kPxW = world scale)
};

struct FormDef {
    const char* name;
    const char* model;  // DAT/MODEL prefix
    FormAttack atk[3];
};

// Battle sizes, edited live with the size dashboard (F3 in battle) and saved to data/battle_sizes.cfg.
// Form sizes are relative units (Rookie, Champion, Ultimate, Mega); unit = world units per 1.0
// (DW3 model units: Agumon's model is ~1200 tall, so unit 2000 x 0.6 = its original size).
struct BattleSizes {
    float form[4] = {0.6f, 1.5f, 2.1f, 2.6f};
    float enemy = 1.f;  // enemy form size x this
    float boss = 1.3f;  // bosses x this on top
    float unit = 2000.f;
};

class Battle {
public:
    Battle(SpriteCache& sprites, ModelCache& models, Party& party) : sprites_(sprites), models_(models), party_(party) {}
    // stageId = the field stage the encounter started on ("S200"): picks the 3D arena.
    bool start(const BattleRequest& req, const std::string& stageId, std::string& err);
    void update(const Input& in);
    void render();
    void renderHud(int windowW, int windowH);
    bool finished() const { return done_; }
    bool won() const { return won_; }

    // Testing helpers.
    void debugFillDv() { me_.dv = 100.f; }
    void debugSetForm(int evo) { setEvo(evo); }
    void debugArena(int number) { forceArena_ = number; }  // --arena N (1..55), before start()
    void debugCamera(float yawDeg) { camYawDebug_ = yawDeg; hasCamDebug_ = true; }  // --cam-yaw DEG
    int arenaNumber() const { return arena_ ? arena_->arena.number : 0; }
    // Size dashboard.
    void setSizeFile(std::filesystem::path p);
    void openSizePanel() { devTools_ = sizePanel_ = true; }
    void enableDevTools() { devTools_ = true; }  // F3 size dashboard; never on for players

private:
    struct Fighter {
        Actor a;            // sprite animation state; a.x/a.y = projected screen position
        float x = 0, z = 0; // ground position (world units, PS1 axes)
        float y = 0;        // floor height under the feet (world y, down is +)
        std::string name;
        int level = 1;
        float hp = 1, maxHp = 1, mp = 0, maxMp = 0, power = 1;
        int evo = 0;
        float dv = 0;  // digivolve gauge 0..100
        int flash = 0, invuln = 0, stun = 0;
        float kbx = 0, kbz = 0;
        int act = -1, actTimer = 0, actTotal = 0;  // current attack slot, remaining / total ticks
        std::vector<int> hitTicks;                 // ticks (from attack start) where the attack lands
        int hitsDone = 0;
        bool actHit = false;
        int cd[3] = {0, 0, 0};
        bool guarding = false, boss = false;
        int aiTimer = 60, dashTimer = 0;
        float scale = 1.f, deathFade = 1.f;
        uint32_t tint = 0xffffffffu;
        float moveX = 0, moveZ = 1;  // last movement direction on the ground
        // 3D model (null = sprite)
        std::shared_ptr<GameModel> model;
        ClipPlayer anim;
        int clip = -1;
        float animSpeed = 1.f;
        float yaw = 0;
        float height = 1200.f;  // world height of the current form (models)
        float bodyH() const;
        float cy() const { return y - bodyH() * 0.5f; }  // centre height (world y, up is -)
    };
    struct Shot {
        float x, y, z, vx, vy, vz, dmg, r;
        int owner, life;
        uint32_t color;
        // Original effect model (optional): played in place, oriented along the flight.
        std::shared_ptr<GameModel> fx;
        float fxScale = 0, fxT = 1, fxLoopFrom = 1, fxLoopTo = 1;
    };
    struct Spark { float x, y, z, vx, vy, vz; int life, max; float r; uint32_t color; };
    struct Popup { float x, y, z, rise; std::string text; int life; uint32_t color; };
    // Digivolution / de-digivolution sequence.
    struct EvoSeq {
        bool active = false, up = true;
        int t = 0, len = 0, from = 0, to = 0;
        float fromH = 0, toH = 0;
        bool swapped = false;
        float flash = 0;          // white overlay on the partner
        bool silhouette = false;  // pure white shape (mid-transformation)
        bool foe = false;         // the enemy is the one digivolving
        std::string fromName, toName;
    };
    struct ArenaGpu {
        Arena arena;
        ModelGpu gpu;
        ~ArenaGpu() { destroyModel(gpu); }
    };

    void updatePlayer(const Input& in);
    void updateEnemy();
    void updateEvoSeq();
    void updateCamera(const Input* in, bool snap);
    void startEvoSeq(int to);
    void startFoeEvo();
    float formHeight(int evo) const { return sizes_.unit * sizes_.form[std::clamp(evo, 0, 3)]; }
    float foeHeight(int evo) const { return formHeight(evo) * sizes_.enemy * (foe_.boss ? sizes_.boss : 1.f); }
    bool loadSizes();
    bool saveSizes() const;
    void applySizes();
    void setFoeForm(bool evolved);
    void updateSizePanel(const Input& in);
    void renderSizePanel();
    void applyFoeEvo();
    void startPlayerAttack(int slot);
    void resolvePlayerAttack();
    void startAttack(Fighter& f, int slot, int duration);
    void hit(Fighter& target, Fighter& attacker, float dmg, float fromX, float fromZ);
    void fireShot(const Fighter& from, const Fighter& to, float dmg, float speed, float r, float angleOffset, uint32_t color, int owner);
    void burst(float x, float y, float z, uint32_t color, int n, float speed);
    void attachFx(Shot& s, const char* effect, float effectScale);
    void drawShotFx(const Shot& s);
    void setEvo(int evo);
    void setClip(Fighter& f, int clip, float speed, bool restart = false);
    void animateModel(Fighter& f);
    void drawFighter3D(Fighter& f);
    void drawFighterSprite(Fighter& f);
    void glow3D(float x, float y, float z, float radius, uint32_t color) const;
    const FormDef* form() const;
    std::string attackName(int slot) const;
    std::string formName() const;
    std::string formNameOf(int evo) const;
    // Moves a fighter on the ground, sliding along the arena's walls; keeps it on the floor.
    void moveOnGround(Fighter& f, float dx, float dz);
    void settleOnFloor(Fighter& f, bool snap);
    void pullInside(Fighter& f);
    float bodyRadius(const Fighter& f) const;
    bool loadArenaFor(const std::string& stageId, std::string& err);
    bool toScreen(float x, float y, float z, float& sx, float& sy) const;

    SpriteCache& sprites_;
    ModelCache& models_;
    Party& party_;
    std::map<int, std::unique_ptr<ArenaGpu>> arenas_;
    ArenaGpu* arena_ = nullptr;
    int forceArena_ = 0;
    Camera3D cam_;
    float camYaw_ = 0, camPitch_ = 0.36f, camZoom_ = 1.f, camDist_ = 9000.f;
    float camT_[3] = {0, 0, 0};
    float camYawDebug_ = 0;
    bool hasCamDebug_ = false;
    float lastMx_ = -1, lastMy_ = -1;
    Fighter me_, foe_;
    std::vector<Shot> shots_;
    std::vector<Spark> sparks_;
    std::vector<Popup> popups_;
    EvoSeq seq_;
    // Enemy digivolution (e.g. Black Agumon -> BlackGreymon below half HP).
    const char* foeEvoModel_ = nullptr;
    const char* foeEvoName_ = nullptr;
    const char* foeEvoEffect_ = nullptr;
    float foeEvoEffectScale_ = 0;
    bool foeEvolved_ = false;
    const char* foeBaseModel_ = nullptr;
    float foeFxBase_ = 0, foeEvoFxBase_ = 0;
    // Size dashboard.
    BattleSizes sizes_;
    std::filesystem::path sizeFile_;
    bool devTools_ = false, sizePanel_ = false, sizeLayout_ = false;
    int sizeSel_ = 0;
    std::string sizeMsg_;
    int sizeMsgT_ = 0;
    float darken_ = 0;  // extra screen dim during digivolution
    enum class Phase { Intro, Fight, Ending } phase_ = Phase::Intro;
    int phaseTimer_ = 0, shake_ = 0, screenFlash_ = 0;
    long long ticks_ = 0;
    bool done_ = false, won_ = false;
    float baseMaxHp_ = 120;
    std::string banner_;
    // Enemy projectile look (its own technique effect when it has a model).
    const char* foeEffect_ = nullptr;
    float foeEffectScale_ = 0;
    // HUD feedback: attack button glow (ticks left, per slot; negative = refused), move-name
    // banner over the partner, and the hold-to-de-digivolve timer.
    int btnFx_[3] = {0, 0, 0};
    std::string moveName_;
    int moveNameT_ = 0;
    int revertHold_ = 0;
};

}  // namespace dw3
