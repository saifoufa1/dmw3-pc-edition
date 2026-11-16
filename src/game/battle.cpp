#include "game/battle.h"

#include <fstream>
#include <sstream>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace dw3 {

namespace {
// World units per 2D-era screen pixel. The battle used to run in 320x240 screen pixels with the
// default unit of 46 px per size 1.0; the 3D battle uses DW3 model units with unit = 2000, so
// every old pixel distance / speed is multiplied by this to keep the same feel.
constexpr float kPxW = 2000.f / 46.f;
constexpr float kHudTop = 50;           // move-name banner stays below the HP bars
// Heights the technique effect scales were tuned at (Rookie..Mega, 2D-era pixels).
constexpr float kFxTunedH[4] = {40, 58, 76, 96};
constexpr int kRevertHold = 60;        // hold digivolve this many ticks to go back to Rookie

// Attack button panel (virtual 320x240 coordinates).
constexpr float kBtnX = 6, kBtnY = 203, kBtnW = 100, kBtnH = 26, kBtnGap = 4;

// Clip slots shared by DW3 battle models (same index = same action for every Digimon).
enum : int {
    kClipIdle = 0, kClipMove = 1, kClipHurt = 2, kClipBreath = 3, kClipGuard = 4, kClipKO = 5,
    kClipRoar = 12, kClipPower = 13, kClipCharge = 15, kClipStrike = 16, kClipCombo2 = 28, kClipCombo3 = 34,
    kClipWin = 38, kClipWin2 = 44,
};

constexpr uint32_t kOrange = 0xff2890ffu, kRed = 0xff1e5affu, kWhite = 0xffffffffu, kGold = 0xff40d0ffu,
                   kBlue = 0xffffb050u, kViolet = 0xffff60c0u;

// The Agumon line: each form has its own model, its own animations and its own way of fighting.
const FormDef kAgumonLine[kMaxEvo + 1] = {
    {"Agumon", "M003AGUM",
     {{"Pepper Breath", AtkFx::Shot, kClipBreath, 6, 34, 10, 5, kOrange, "EFFECT/MEFT0328", 0.0055f},
      {"Claw Attack", AtkFx::Melee, kClipStrike, 0, 22, 8, 1.0f, kWhite},
      {"Spitfire Rush", AtkFx::Spread, kClipBreath, 16, 80, 6, 4, kRed, "EFFECT/MEFT0328", 0.0042f}}},
    {"Greymon", "M005GREY",
     {{"Nova Blast", AtkFx::Orb, kClipBreath, 8, 40, 12, 8, kOrange, "EFFECT/MEFT0331", 0.014f},
      {"Horn Strike", AtkFx::Dash, kClipStrike, 0, 30, 9, 1.0f, kWhite},
      {"Great Antler", AtkFx::Combo, kClipCombo3, 18, 90, 7, 1.0f, kGold}}},
    {"MetalGreymon", "M012MTGR",
     {{"Giga Destroyer", AtkFx::Spread, kClipBreath, 10, 46, 10, 6, kViolet, "EFFECT/MEFT0367", 0.0085f},
      {"Trident Arm", AtkFx::Dash, kClipStrike, 0, 30, 10, 1.1f, kWhite},
      {"Mega Claw", AtkFx::Combo, kClipCombo3, 20, 90, 8, 1.1f, kGold}}},
    {"WarGreymon", "M213WGRE",
     {{"Gaia Force", AtkFx::Orb, kClipBreath, 14, 60, 18, 18, kOrange, "EFFECT/MEFT0090", 0.06f},
      {"Dramon Killer", AtkFx::Dash, kClipStrike, 0, 26, 11, 1.2f, kWhite},
      {"Brave Tornado", AtkFx::Combo, kClipCombo3, 22, 90, 9, 1.2f, kBlue}}},
};
// Enemies drawn as 3D battle models. Keyed by the field sprite of the encounter.
// DAT/MODEL has no Black Agumon or Blue Agumon file (no name contains BAGM/SAGM and none of the
// 241 loadable models looks like them; the "2"/"9" files are recolours of other species), so they
// reuse Agumon's M003AGUM mesh + clips with a repainted texture. Digmon (P123DIGM) has no battle
// model in DAT/MODEL either -> it stays the field sprite (a billboard in the 3D arena).
struct EnemyModelDef {
    const char* sprite;
    const char* model;
    const char* effect;  // projectile effect model
    float effectScale;
    // Champion the enemy digivolves to below half HP (nullptr = none).
    const char* evoModel = nullptr;
    const char* evoName = nullptr;
    const char* evoEffect = nullptr;
    float evoEffectScale = 0;
};
const EnemyModelDef kEnemyModels[] = {
    {"P171BAGM", "M003AGUM:black", "EFFECT/MEFT0328", 0.0055f,  // Black Agumon -> BlackGreymon
     "M005GREY:black", "BlackGreymon", "EFFECT/MEFT0331", 0.014f},
    {"P167SAGM", "M003AGUM:blue", "EFFECT/MEFT0328", 0.0055f,   // Blue Agumon -> Greymon (blue)
     "M005GREY:blue", "Greymon (Blue)", "EFFECT/MEFT0331", 0.014f},
    {"P006AGUM", "M003AGUM", "EFFECT/MEFT0328", 0.0055f},        // Agumon
};

// Any other partner (sprite only): the original prototype moves.
const FormDef kGenericForm = {"", "",
                              {{"Blast", AtkFx::Shot, kClipBreath, 6, 34, 10, 5, kOrange},
                               {"Strike", AtkFx::Melee, kClipStrike, 0, 22, 7, 1.0f, kWhite},
                               {"Burst", AtkFx::Spread, kClipBreath, 16, 80, 7, 4.5f, kRed}}};

float len(float x, float y) { return std::sqrt(x * x + y * y); }
float len3(float x, float y, float z) { return std::sqrt(x * x + y * y + z * z); }
float frand(long long& seed) {  // tiny deterministic LCG
    seed = seed * 6364136223846793005LL + 1442695040888963407LL;
    return float((seed >> 33) & 0xffffff) / float(0xffffff);
}
long long g_seed = 12345;

float smooth(float a, float b, float x) {
    float t = std::clamp((x - a) / (b - a), 0.f, 1.f);
    return t * t * (3 - 2 * t);
}
float wrapAngle(float a) {
    while (a > 3.14159265f) a -= 6.2831853f;
    while (a < -3.14159265f) a += 6.2831853f;
    return a;
}
// Model yaw that faces a ground direction (models look along their local -z).
float faceYaw(float dx, float dz) { return std::atan2(-dx, -dz); }

bool clipValid(const Model& m, int c) { return c >= 0 && size_t(c) < m.clips.size() && m.clips[size_t(c)].valid(); }
int firstValid(const Model& m, std::initializer_list<int> cs) {
    for (int c : cs)
        if (clipValid(m, c)) return c;
    return kClipIdle;
}
}  // namespace

float Battle::Fighter::bodyH() const { return model ? height : 24.f * scale * kPxW; }
float Battle::bodyRadius(const Fighter& f) const { return f.model ? f.height * 0.3f : 10.f * f.scale * kPxW; }

// ---------------------------------------------------------------- arena
bool Battle::loadArenaFor(const std::string& stageId, std::string& err) {
    const int wanted = forceArena_ > 0 ? std::clamp(forceArena_, 1, kArenaCount) : arenaForStage(stageId);
    // The arena of this area, else the closest decoded one (same theme), else the sky island.
    for (int n : {wanted, arenaFallback(wanted), 1, 13}) {
        auto it = arenas_.find(n);
        if (it != arenas_.end()) { arena_ = it->second.get(); return true; }
        Bytes b;
        if (!readFile(models_.root() / "DAT" / "EFFECT" / (arenaFileName(n) + ".BIN"), b)) continue;
        auto a = std::make_unique<ArenaGpu>();
        std::string e;
        if (!loadArena(b, n, a->arena, e)) continue;
        uploadModel(a->arena.model, a->gpu);
        arena_ = a.get();
        arenas_[n] = std::move(a);
        return true;
    }
    err = "no battle arena could be loaded (DAT/EFFECT/MEFT10xx.BIN)";
    return false;
}

// Moves a fighter toward the arena centre until it stands on walkable floor.
void Battle::pullInside(Fighter& f) {
    if (!arena_) return;
    const float cx = arena_->arena.centerX, cz = arena_->arena.centerZ;
    for (int k = 0; k < 40 && !arena_->arena.walkable(f.x, f.z); ++k) {
        f.x = cx + (f.x - cx) * 0.85f;
        f.z = cz + (f.z - cz) * 0.85f;
    }
}

void Battle::settleOnFloor(Fighter& f, bool snap) {
    float y = arena_ ? arena_->arena.playY : 0.f;
    if (arena_) arena_->arena.floorAt(f.x, f.z, y);
    f.y = snap ? y : f.y + (y - f.y) * 0.35f;
}

void Battle::moveOnGround(Fighter& f, float dx, float dz) {
    // The whole body must fit: the feet and 8 points on the body's ground circle.
    const float r = bodyRadius(f) * 0.9f;
    auto ok = [&](float x, float z) {
        if (!arena_) return true;
        if (!arena_->arena.walkable(x, z)) return false;
        for (int k = 0; k < 8; ++k) {
            const float a = float(k) * 0.785398f;
            if (!arena_->arena.walkable(x + std::cos(a) * r, z + std::sin(a) * r)) return false;
        }
        return true;
    };
    if (ok(f.x + dx, f.z + dz)) { f.x += dx; f.z += dz; }
    else if (ok(f.x + dx, f.z)) f.x += dx;
    else if (ok(f.x, f.z + dz)) f.z += dz;
    else if (!ok(f.x, f.z)) {  // stuck outside (e.g. after a size change): walk back to the centre
        const float cx = arena_ ? arena_->arena.centerX : 0.f, cz = arena_ ? arena_->arena.centerZ : 0.f;
        const float d = std::max(1.f, len(f.x - cx, f.z - cz));
        f.x -= (f.x - cx) / d * std::min(d, 60.f);
        f.z -= (f.z - cz) / d * std::min(d, 60.f);
    }
}

bool Battle::toScreen(float x, float y, float z, float& sx, float& sy) const {
    float d;
    return cam_.project(x, y, z, sx, sy, d);
}

// ---------------------------------------------------------------- start
bool Battle::start(const BattleRequest& req, const std::string& stageId, std::string& err) {
    me_ = {};
    foe_ = {};
    shots_.clear();
    sparks_.clear();
    popups_.clear();
    seq_ = {};
    darken_ = 0;
    done_ = won_ = false;
    phase_ = Phase::Intro;
    phaseTimer_ = 70;
    banner_ = req.boss ? "BOSS BATTLE!" : "FIGHT!";
    screenFlash_ = 20;
    if (!loadArenaFor(stageId, err)) return false;

    me_.a.sprite = sprites_.get(party_.partnerId, &err);
    foe_.a.sprite = sprites_.get(req.enemySprite, &err);
    if (!me_.a.sprite || !foe_.a.sprite) return false;

    me_.name = party_.partnerName;
    me_.level = 1 + party_.wins;
    baseMaxHp_ = 120.f + 10.f * float(party_.wins);
    me_.maxHp = me_.hp = baseMaxHp_;
    me_.maxMp = me_.mp = 60;
    me_.a.dir = E;
    setEvo(0);

    foe_.name = req.enemyName;
    foe_.level = req.level;
    foe_.boss = req.boss;
    foe_.maxHp = foe_.hp = (50.f + req.level * 14.f) * (req.boss ? 2.4f : 1.f);
    foe_.power = (0.8f + req.level * 0.1f) * (req.boss ? 1.25f : 1.f);
    foe_.scale = req.boss ? 1.6f : 1.15f;
    foe_.a.dir = W;
    foe_.aiTimer = 50;
    foeEffect_ = nullptr;
    foeEffectScale_ = 0;
    foeEvoModel_ = foeEvoName_ = foeEvoEffect_ = foeBaseModel_ = nullptr;
    foeEvolved_ = false;
    for (const EnemyModelDef& d : kEnemyModels) {
        if (req.enemySprite != d.sprite) continue;
        foe_.model = models_.get(d.model);
        if (!foe_.model) break;  // no model on disk: field sprite
        // An enemy Rookie is exactly as tall as the partner Rookie; bosses are a bit bigger.
        foe_.height = foeHeight(0);
        foeBaseModel_ = d.model;
        foeFxBase_ = d.effectScale;
        foeEvoFxBase_ = d.evoEffectScale;
        foe_.scale = foe_.height / (35.f * kPxW);  // keeps the AI's reach/spacing in proportion
        foeEffect_ = d.effect;
        foeEffectScale_ = d.effectScale * foe_.height / kFxTunedH[0];
        foeEvoModel_ = d.evoModel;
        foeEvoName_ = d.evoName;
        foeEvoEffect_ = d.evoEffect;
        foeEvoEffectScale_ = d.evoEffectScale;
        setClip(foe_, kClipIdle, 0.6f, true);
        break;
    }

    // Partner on the left, enemy on the right and a little farther, facing each other across
    // the arena centre (the default camera looks along +z, so world x is screen right).
    auto place = [&](Fighter& f, float x, float z) {
        f.x = x; f.z = z;
        pullInside(f);
        settleOnFloor(f, true);
    };
    const float ax = arena_->arena.centerX, az = arena_->arena.centerZ;
    place(me_, ax - 2600.f, az - 500.f);
    place(foe_, ax + 2600.f, az + 700.f);
    me_.yaw = faceYaw(foe_.x - me_.x, foe_.z - me_.z);
    foe_.yaw = faceYaw(me_.x - foe_.x, me_.z - foe_.z);
    me_.moveX = 1; me_.moveZ = 0;

    camYaw_ = hasCamDebug_ ? camYawDebug_ * 3.14159265f / 180.f : 0.f;
    camPitch_ = 0.36f;
    camZoom_ = 1.f;
    lastMx_ = lastMy_ = -1;
    updateCamera(nullptr, true);
    camDist_ *= 1.6f;  // intro: the camera swoops in
    for (int& b : btnFx_) b = 0;
    moveName_.clear();
    moveNameT_ = 0;
    revertHold_ = 0;
    return true;
}

const FormDef* Battle::form() const {
    if (party_.partnerId == "P006AGUM") return &kAgumonLine[me_.evo];
    return &kGenericForm;
}

std::string Battle::formNameOf(int evo) const {
    if (party_.partnerId == "P006AGUM") return kAgumonLine[evo].name;
    return evo == 0 ? me_.name : me_.name + " (" + evoStage(evo).name + ")";
}
std::string Battle::formName() const { return formNameOf(me_.evo); }
std::string Battle::attackName(int slot) const { return form()->atk[slot].name; }

void Battle::setEvo(int evo) {
    const EvoStage& prev = evoStage(me_.evo);
    const EvoStage& next = evoStage(evo);
    me_.evo = evo;
    me_.scale = next.scale;
    me_.power = next.power;
    me_.maxHp = baseMaxHp_ + next.hpBonus;
    if (next.hpBonus > prev.hpBonus) me_.hp += next.hpBonus - prev.hpBonus;  // growing heals the difference
    me_.hp = std::min(me_.hp, me_.maxHp);
    me_.maxMp = 60.f + 20.f * evo;

    // The real model of this form (falls back to the tinted sprite when missing).
    const FormDef* f = form();
    me_.model = f->model[0] ? models_.get(f->model) : nullptr;
    me_.tint = me_.model ? 0xffffffffu : next.tint;
    me_.height = formHeight(evo);
    me_.clip = -1;
    me_.act = -1;
    if (me_.model) setClip(me_, kClipIdle, 0.6f, true);
}

void Battle::setClip(Fighter& f, int clip, float speed, bool restart) {
    f.animSpeed = speed;
    if (!f.model) return;
    if (clip == f.clip && !restart) return;
    f.clip = clip;
    f.anim.play(f.model->model, clip);
}

void Battle::burst(float x, float y, float z, uint32_t color, int n, float speed) {
    for (int i = 0; i < n; ++i) {
        const float ang = frand(g_seed) * 6.2832f, sp = speed * (0.4f + frand(g_seed)) * kPxW;
        const float up = (frand(g_seed) - 0.5f) * 1.4f;
        const int life = 14 + int(frand(g_seed) * 16);
        sparks_.push_back({x, y, z, std::cos(ang) * sp, up * sp - 0.6f * kPxW, std::sin(ang) * sp, life, life,
                           (2.f + frand(g_seed) * 3.f) * kPxW, color});
    }
}

void Battle::fireShot(const Fighter& from, const Fighter& to, float dmg, float speed, float r, float angOff, uint32_t color, int owner) {
    // Projectiles leave from the mouth (upper body, toward the target) and fly in 3D.
    const float mouthY = from.y - from.bodyH() * (from.model ? 0.62f : 0.5f);
    const float hx = to.x - from.x, hz = to.z - from.z;
    const float hl = std::max(1.f, len(hx, hz));
    const float ca = std::cos(angOff), sa = std::sin(angOff);
    const float dx = (hx * ca - hz * sa) / hl, dz = (hx * sa + hz * ca) / hl;  // spread turns around the vertical
    const float dy = (to.cy() - mouthY) / hl;
    const float l = std::max(1e-3f, len3(dx, dy, dz));
    const float reach = from.model ? from.height * 0.35f : 10.f * from.scale * kPxW;
    const float sp = speed * kPxW;
    shots_.push_back({from.x + dx / l * reach, mouthY, from.z + dz / l * reach, dx / l * sp, dy / l * sp, dz / l * sp,
                      dmg, r * kPxW, owner, 150, color});
}

// Effect models fly themselves along local -z (thousands of units). The game moves the shot,
// so nodes with such a travel track are pinned to the origin and the clip loops its in-flight part.
static bool isTravelTrack(const ModelTrack& t) {
    for (const ModelKey& k : t.pos)
        if (k.t < 30000 && (std::abs(k.x) >= 2500 || std::abs(k.y) >= 2500 || std::abs(k.z) >= 2500)) return true;
    return false;
}

void Battle::attachFx(Shot& s, const char* effect, float effectScale) {
    if (!effect) return;
    s.fx = models_.get(effect);
    if (!s.fx) return;
    const Model& m = s.fx->model;
    // launch = key before the flight starts (the original charges in place until then: the shot
    // already left the mouth here, so skip that part); arrive = frame where the flight ends.
    int arrive = 0, launch = 1;
    for (const ModelNode& n : m.nodes) {
        const ModelTrack& t = m.tracks[size_t(n.track)];
        if (!isTravelTrack(t)) continue;
        for (size_t i = 0; i < t.pos.size(); ++i) {
            const ModelKey& k = t.pos[i];
            if (k.t < 30000 && (std::abs(k.z) >= 2500 || std::abs(k.x) >= 2500)) {
                arrive = std::max(arrive, int(k.t));
                if (i > 0) launch = std::max(launch, int(t.pos[i - 1].t));
                break;
            }
        }
    }
    if (arrive <= 2) arrive = (!m.clips.empty() && m.clips[0].valid()) ? std::max(4, m.clips[0].length - 1) : 40;
    launch = std::min(launch, arrive - 2);
    s.fxScale = effectScale;  // world units per effect-model unit
    s.fxT = float(launch);
    s.fxLoopTo = float(arrive - 1);
    s.fxLoopFrom = std::max(float(launch), (float(launch) + s.fxLoopTo) * 0.5f);
}

void Battle::drawShotFx(const Shot& s) {
    const Model& m = s.fx->model;
    Pose pose;
    samplePose(m, s.fxT, pose);
    for (size_t i = 0; i < m.nodes.size(); ++i)
        if (isTravelTrack(m.tracks[size_t(m.nodes[i].track)])) pose[i].px = pose[i].py = pose[i].pz = 0;
    WorldPlace p;
    p.x = s.x;
    p.y = s.y;
    p.z = s.z;
    p.scale = s.fxScale;
    p.yaw = faceYaw(s.vx, s.vz);  // the effect's forward (-z) along the flight
    drawModelWorld(m, s.fx->gpu, pose, cam_, p);
}

void Battle::startAttack(Fighter& f, int slot, int duration) {
    f.act = slot;
    f.actTimer = duration;
    f.actTotal = duration;
    f.actHit = false;
    f.hitsDone = 0;
    f.a.move = Move::Action;
    if (&f == &foe_ && f.model) {
        // The enemy's own clip, timed so its strike frame lands when the hit resolves
        // (updateEnemy: 18 ticks of wind-up).
        const Model& m = f.model->model;
        const int clip = slot == 0 ? firstValid(m, {kClipBreath, kClipStrike}) : firstValid(m, {kClipStrike, kClipCombo2, kClipBreath});
        float hitFrame = 18.f;
        if (clipValid(m, clip))
            for (const ClipSeg& sg : m.clips[size_t(clip)].segs)
                if (!sg.blend) { hitFrame = float(sg.start) + float(sg.len) * 0.55f; break; }
        setClip(f, clip, std::clamp(hitFrame / 18.f, 0.5f, 2.5f), true);
    }
}

void Battle::startPlayerAttack(int slot) {
    Fighter& f = me_;
    const FormAttack& atk = form()->atk[slot];
    f.hitTicks.clear();
    int total = 20;
    if (f.model && clipValid(f.model->model, atk.clip)) {
        // Timing comes from the form's own animation: one hit per played segment.
        const ModelClip& c = f.model->model.clips[size_t(atk.clip)];
        const float speed = atk.fx == AtkFx::Combo ? 1.3f : 1.1f;
        total = int(std::ceil(float(c.length) / speed));
        int firstFrom = -1;
        for (const ClipSeg& s : c.segs) {
            if (s.blend) continue;
            if (firstFrom < 0) firstFrom = s.from;
            else if (s.from < firstFrom) continue;  // jumps back on the timeline = return to stance
            f.hitTicks.push_back(int((float(s.start) + float(s.len) * 0.55f) / speed));
        }
        const bool multi = atk.fx == AtkFx::Combo || atk.fx == AtkFx::Spread;
        if (!multi && f.hitTicks.size() > 1) f.hitTicks.resize(1);
        if (atk.fx == AtkFx::Spread && f.hitTicks.size() == 1)
            for (int k = 1; k < 3; ++k) f.hitTicks.push_back(f.hitTicks[0] + k * 5);
        setClip(f, atk.clip, speed, true);
    } else {
        f.hitTicks = atk.fx == AtkFx::Spread ? std::vector<int>{8} : std::vector<int>{6};
        total = atk.fx == AtkFx::Spread ? 26 : 20;
    }
    if (f.hitTicks.empty()) f.hitTicks.push_back(total / 2);
    startAttack(f, slot, total);
}

void Battle::hit(Fighter& t, Fighter& attacker, float dmg, float fromX, float fromZ) {
    if (t.invuln > 0 || t.hp <= 0) return;
    if (t.guarding) {
        dmg *= 0.25f;
        burst(t.x, t.cy(), t.z, rgba(120, 200, 255), 6, 1.5f);
    }
    dmg = std::max(1.f, std::round(dmg));
    t.hp = std::max(0.f, t.hp - dmg);
    t.flash = 8;
    t.invuln = 10;
    t.stun = t.guarding ? 4 : 12;
    const float dx = t.x - fromX, dz = t.z - fromZ, l = std::max(1.f, len(dx, dz));
    const float kb = (t.guarding ? 1.2f : 3.0f) * kPxW;
    t.kbx = dx / l * kb;
    t.kbz = dz / l * kb;
    if (t.model && !t.guarding && t.act < 0) setClip(t, kClipHurt, 1.2f, true);
    shake_ = std::max(shake_, foe_.boss ? 8 : 5);
    burst(t.x, t.cy(), t.z, rgba(255, 220, 120), 10, 2.2f);
    char buf[16];
    std::snprintf(buf, sizeof buf, "%d", int(dmg));
    popups_.push_back({t.x, t.y - t.bodyH(), t.z, 6.f, buf, 50, &t == &me_ ? 0xff5050ffu : 0xffffffffu});
    // Digivolve gauge: fills from dealing and taking damage.
    if (&attacker == &me_) me_.dv = std::min(100.f, me_.dv + dmg * 1.1f);
    else me_.dv = std::min(100.f, me_.dv + dmg * 1.5f);
}

void Battle::resolvePlayerAttack() {
    Fighter& f = me_;
    const FormAttack& atk = form()->atk[f.act];
    const float pw = f.power;
    const int elapsed = f.actTotal - f.actTimer;
    const float dx = foe_.x - f.x, dz = foe_.z - f.z, d = std::max(1.f, len(dx, dz));
    const float reach = (f.model ? 10.f * kPxW + f.height * 0.42f : 24.f * f.scale * kPxW) * atk.size;
    const float foeR = bodyRadius(foe_);
    const float fxScale = atk.effectScale * me_.height / kFxTunedH[std::clamp(me_.evo, 0, 3)];
    const float sizeK = std::sqrt(f.model ? f.height / (40.f * kPxW) : f.scale);

    // Lunges: close the distance before the strike lands.
    const int nextHit = f.hitsDone < int(f.hitTicks.size()) ? f.hitTicks[size_t(f.hitsDone)] : -1;
    if ((atk.fx == AtkFx::Dash || atk.fx == AtkFx::Combo) && nextHit >= 0 && elapsed < nextHit && d > reach * 0.7f) {
        const float sp = (atk.fx == AtkFx::Dash ? 3.4f : 1.8f) * kPxW;
        moveOnGround(f, dx / d * sp, dz / d * sp);
        f.yaw = faceYaw(dx, dz);
    }

    while (f.hitsDone < int(f.hitTicks.size()) && elapsed >= f.hitTicks[size_t(f.hitsDone)]) {
        const int k = f.hitsDone++;
        switch (atk.fx) {
            case AtkFx::Shot:
                fireShot(f, foe_, atk.dmg * pw, 3.8f, atk.size * sizeK, 0, atk.color, 0);
                attachFx(shots_.back(), atk.effect, fxScale);
                break;
            case AtkFx::Orb:
                fireShot(f, foe_, atk.dmg * pw, 2.8f, atk.size, 0, atk.color, 0);
                attachFx(shots_.back(), atk.effect, fxScale);
                burst(f.x, f.cy() - f.bodyH() * 0.15f, f.z, atk.color, 12, 2.f);
                break;
            case AtkFx::Spread: {
                const int n = int(f.hitTicks.size());
                const float off = n > 1 ? (float(k) - (n - 1) / 2.f) * 0.2f : 0.f;
                fireShot(f, foe_, atk.dmg * pw, 3.4f, atk.size * sizeK, off, atk.color, 0);
                attachFx(shots_.back(), atk.effect, fxScale);
                break;
            }
            case AtkFx::Melee:
            case AtkFx::Dash:
            case AtkFx::Combo: {
                const float dist = len3(foe_.x - f.x, (foe_.cy() - f.cy()) * 0.5f, foe_.z - f.z);
                burst(f.x + dx / d * reach * 0.6f, f.cy(), f.z + dz / d * reach * 0.6f, atk.color, 6, 1.6f);
                if (dist < reach + foeR) hit(foe_, f, atk.dmg * pw, f.x, f.z);
                break;
            }
        }
    }
}

void Battle::startEvoSeq(int to) {
    seq_ = {};
    seq_.active = true;
    seq_.up = to > me_.evo;
    seq_.from = me_.evo;
    seq_.to = to;
    seq_.len = seq_.up ? 200 : 110;
    seq_.fromH = me_.height;
    seq_.toH = formHeight(to);
    seq_.fromName = formNameOf(seq_.from);
    seq_.toName = formNameOf(to);
    me_.act = -1;
    me_.guarding = false;
    me_.stun = 0;
    me_.kbx = me_.kbz = 0;
    shots_.clear();
    if (seq_.up) me_.dv = 0;
    revertHold_ = -1;  // a held button must be released before it can de-digivolve
    moveNameT_ = 0;
}

// The enemy digivolves once, the first time its HP falls below half.
void Battle::startFoeEvo() {
    foeEvolved_ = true;
    seq_ = {};
    seq_.active = true;
    seq_.foe = true;
    seq_.up = true;
    seq_.len = 200;
    seq_.fromH = foe_.height;
    seq_.toH = foeHeight(1);
    seq_.fromName = foe_.name;
    seq_.toName = foeEvoName_;
    foe_.act = -1;
    foe_.dashTimer = 0;
    foe_.stun = 0;
    foe_.kbx = foe_.kbz = 0;
    me_.act = -1;
    me_.guarding = false;
    shots_.clear();
}

void Battle::applyFoeEvo() {
    auto m = models_.get(foeEvoModel_);
    if (!m) return;
    foe_.model = m;
    foe_.name = foeEvoName_;
    foe_.clip = -1;
    // Stronger and partly healed, like the partner's own digivolution.
    const float bonus = foe_.maxHp * 0.5f;
    foe_.maxHp += bonus;
    foe_.hp = std::min(foe_.maxHp, foe_.hp + bonus);
    foe_.power *= 1.35f;
    foe_.scale = seq_.toH / (35.f * kPxW);
    foeEffect_ = foeEvoEffect_;
    foeEffectScale_ = foeEvoEffectScale_ * seq_.toH / kFxTunedH[1];
    foeEvolved_ = true;
    setClip(foe_, kClipIdle, 0.6f, true);
}

void Battle::updateEvoSeq() {
    EvoSeq& s = seq_;
    Fighter& f = s.foe ? foe_ : me_;
    const Fighter& other = s.foe ? me_ : foe_;
    ++s.t;
    const float t = float(s.t);
    const float A = s.up ? 60.f : 24.f;   // charge ends
    const float B = s.up ? 110.f : 54.f;  // white silhouette ends -> flash
    const float C = s.up ? 150.f : 84.f;  // reveal ends
    const float toFoe = faceYaw(other.x - f.x, other.z - f.z);

    if (t < A) {  // gathering power: glow, rising sparks, turning white
        darken_ = std::min(s.up ? 0.6f : 0.35f, darken_ + 0.02f);
        if (f.model) setClip(f, firstValid(f.model->model, {kClipCharge, kClipPower, kClipIdle}), 0.8f);
        s.flash = smooth(A * 0.35f, A, t);
        f.yaw += 0.02f + 0.3f * smooth(A * 0.4f, A, t);
        if (s.t % 2 == 0) {
            const float ang = frand(g_seed) * 6.2832f, r = frand(g_seed) * f.height * 0.45f;
            sparks_.push_back({f.x + std::cos(ang) * r, f.y - frand(g_seed) * 6.f * kPxW, f.z + std::sin(ang) * r, 0,
                               (-2.2f - frand(g_seed) * 1.5f) * kPxW, 0, 34, 34, (2.f + frand(g_seed) * 2.f) * kPxW,
                               s.up ? rgba(255, 230, 140) : rgba(200, 220, 255)});
        }
    } else if (t < B) {  // pure white shape spinning, growing (or shrinking) to the new size
        s.silhouette = true;
        s.flash = 1.f;
        f.yaw += 0.33f;
        const float k = smooth(A + 5.f, B - 5.f, t);
        if (!s.swapped && t >= (A + B) * 0.5f) {
            s.swapped = true;
            if (s.foe) applyFoeEvo();
            else {
                setEvo(s.to);  // new model, new size, new attacks
                if (!s.up) me_.dv = 0;
            }
        }
        f.height = s.fromH + (s.toH - s.fromH) * k;
        if (s.t % 3 == 0) burst(f.x, f.cy(), f.z, rgba(255, 255, 255), 2, 2.5f);
    } else {
        if (s.t == int(B)) {
            s.silhouette = false;
            screenFlash_ = 30;
            shake_ = 16;
            burst(f.x, f.cy(), f.z, s.up ? rgba(255, 230, 120) : rgba(200, 220, 255), 60, 3.5f);
            f.height = s.toH;
            if (f.model) setClip(f, s.up ? firstValid(f.model->model, {kClipRoar, kClipPower, kClipBreath}) : kClipIdle, 0.9f, true);
        }
        s.flash = 1.f - smooth(B, B + (C - B) * 0.7f, t);
        // spin slows down and settles facing the enemy
        const float settle = smooth(B, C, t);
        f.yaw = wrapAngle(f.yaw);
        f.yaw += (0.33f * (1.f - settle)) + wrapAngle(toFoe - f.yaw) * 0.12f * settle;
        if (t > C) darken_ = std::max(0.f, darken_ - 0.03f);
    }
    if (f.model) f.anim.update(f.model->model, f.animSpeed);

    if (s.t >= s.len) {
        s = {};
        darken_ = 0;
        if (f.model) setClip(f, kClipIdle, 0.6f, true);
    }
}

void Battle::updatePlayer(const Input& in) {
    Fighter& f = me_;
    for (int& c : f.cd) c = std::max(0, c - 1);
    f.mp = std::min(f.maxMp, f.mp + 0.07f);
    if (f.stun > 0) { --f.stun; return; }

    // A digivolved form stays for the whole battle. Going back to Rookie is the player's
    // choice: hold the digivolve button for a second (when it cannot digivolve further).
    const bool canDigivolve = f.dv >= 100.f && f.evo < kMaxEvo;
    if (!in.digivolve) revertHold_ = 0;
    else if (revertHold_ >= 0 && f.evo > 0 && !canDigivolve && f.act < 0 && ++revertHold_ >= kRevertHold) {
        startEvoSeq(0);
        return;
    }

    // digivolve (each stage is a different, bigger model)
    if (in.digivolveP && f.dv >= 100.f && f.evo < kMaxEvo && f.act < 0) {
        startEvoSeq(f.evo + 1);
        return;
    }

    if (f.act >= 0) {
        resolvePlayerAttack();
        if (--f.actTimer <= 0) { f.act = -1; f.a.move = Move::Idle; f.clip = -1; }
        return;
    }

    f.guarding = in.guard;
    if (f.guarding) { f.a.move = Move::Idle; return; }

    const bool want[3] = {in.atk1P, in.atk2P, in.atk3P};
    for (int s = 0; s < 3; ++s) {
        const FormAttack& atk = form()->atk[s];
        if (!want[s]) continue;
        if (f.cd[s] > 0) { btnFx_[s] = -14; continue; }  // still recharging: the button blinks red
        if (f.mp < atk.mp) {
            btnFx_[s] = -14;
            popups_.push_back({f.x, f.y - f.bodyH(), f.z, 8.f, "No MP", 40, 0xffa0a0a0u});
            continue;
        }
        btnFx_[s] = 26;  // the pressed button lights up
        moveName_ = atk.name;
        moveNameT_ = 75;
        f.mp -= atk.mp;
        f.cd[s] = atk.cooldown;
        f.yaw = faceYaw(foe_.x - f.x, foe_.z - f.z);  // auto-face target
        startPlayerAttack(s);
        return;
    }

    // Move relative to the camera: up = away from the camera, right = screen right.
    const float ix = float(in.right) - float(in.left), iy = float(in.down) - float(in.up);
    if (ix != 0 || iy != 0) {
        float fx, fz, rx, rz;
        cam_.groundAxes(fx, fz, rx, rz);
        float mx = ix * rx - iy * fx, mz = ix * rz - iy * fz;
        const float l = std::max(1e-3f, len(mx, mz));
        mx /= l;
        mz /= l;
        const float sp = (in.run ? 2.4f : 1.6f) * kPxW;
        moveOnGround(f, mx * sp, mz * sp);
        f.moveX = mx;
        f.moveZ = mz;
        f.a.dir = dirFromVector(ix, iy);
        f.a.move = in.run ? Move::Run : Move::Walk;
    } else {
        f.a.move = Move::Idle;
    }
}

void Battle::updateEnemy() {
    Fighter& e = foe_;
    for (int& c : e.cd) c = std::max(0, c - 1);
    if (e.hp <= 0) return;
    if (e.stun > 0) { --e.stun; return; }

    const float dx = me_.x - e.x, dz = me_.z - e.z, d = std::max(1.f, len(dx, dz));
    if (e.act >= 0) {
        const int total = e.act == 1 ? 26 : 30;
        const int fireAt = total - 18;  // wind-up = telegraph
        if (e.actTimer == fireAt) {
            if (e.act == 1) {
                if (len3(me_.x - e.x, (me_.cy() - e.cy()) * 0.5f, me_.z - e.z) < 30.f * kPxW * e.scale + me_.bodyH() * 0.2f)
                    hit(me_, e, 9.f * e.power, e.x, e.z);
                burst(e.x + dx / d * 14.f * kPxW, e.cy(), e.z + dz / d * 14.f * kPxW, rgba(255, 120, 120), 8, 2.f);
            } else {
                const int n = e.boss ? 3 : 1;
                for (int i = 0; i < n; ++i) {
                    fireShot(e, me_, 8.f * e.power, e.boss ? 2.9f : 2.5f, e.boss ? 6.f : 5.f, (i - (n - 1) / 2.f) * 0.3f,
                             e.boss ? rgba(200, 60, 255) : rgba(120, 80, 255), 1);
                    attachFx(shots_.back(), foeEffect_, foeEffectScale_);
                }
            }
        }
        if (--e.actTimer <= 0) { e.act = -1; e.a.move = Move::Idle; }
        return;
    }

    if (e.dashTimer > 0) {  // charge at the player
        --e.dashTimer;
        moveOnGround(e, dx / d * 2.6f * kPxW, dz / d * 2.6f * kPxW);
        e.moveX = dx / d;
        e.moveZ = dz / d;
        e.a.move = Move::Run;
        if (d < 20.f * kPxW * e.scale + me_.bodyH() * 0.2f && me_.invuln == 0) { hit(me_, e, 7.f * e.power, e.x, e.z); e.dashTimer = 0; }
        return;
    }

    if (--e.aiTimer <= 0) {
        const float r = frand(g_seed);
        if (d < 36.f * kPxW * e.scale + me_.bodyH() * 0.2f) startAttack(e, 1, 26);
        else if (r < 0.6f) startAttack(e, 0, 30);
        else e.dashTimer = 40;
        e.aiTimer = e.boss ? 45 + int(frand(g_seed) * 35) : 70 + int(frand(g_seed) * 45);
        return;
    }

    // keep a fighting distance (bigger partner forms push it out), circling the partner a little
    const float want = (e.boss ? 75.f : 60.f) * kPxW + me_.bodyH() * 0.3f;
    float mvx = 0, mvz = 0;
    if (d > want + 10 * kPxW) { mvx = dx / d; mvz = dz / d; }
    else if (d < want - 15 * kPxW) { mvx = -dx / d; mvz = -dz / d; }
    const float strafe = std::sin(float(ticks_) * 0.03f) * 0.5f;
    mvx += -dz / d * strafe;
    mvz += dx / d * strafe;
    if (len(mvx, mvz) > 0.2f) {
        moveOnGround(e, mvx * 1.1f * kPxW, mvz * 1.1f * kPxW);
        e.moveX = mvx;
        e.moveZ = mvz;
        e.a.move = Move::Walk;
    } else {
        e.a.move = Move::Idle;
    }
}

// Picks the model clip from what the fighter is doing and turns it toward where it looks.
void Battle::animateModel(Fighter& f) {
    const Fighter& other = &f == &me_ ? foe_ : me_;
    // Sprite facing (8 directions) from the screen-space direction it looks at.
    {
        const bool walking = f.act < 0 && (f.a.move == Move::Walk || f.a.move == Move::Run);
        const float lx = walking ? f.moveX : other.x - f.x, lz = walking ? f.moveZ : other.z - f.z;
        float fx, fz, rx, rz;
        cam_.groundAxes(fx, fz, rx, rz);
        f.a.dir = dirFromVector(lx * rx + lz * rz, -(lx * fx + lz * fz));
    }
    if (!f.model) return;
    const Model& m = f.model->model;
    if (f.act < 0) {
        if (phase_ == Phase::Ending && f.hp > 0 && won_ == (&f == &me_)) setClip(f, firstValid(m, {kClipWin, kClipWin2, kClipIdle}), 0.8f);
        else if (f.hp <= 0) setClip(f, firstValid(m, {kClipKO, kClipHurt}), 0.8f);
        else if (f.clip == kClipHurt && !f.anim.done()) {}  // let the flinch finish
        else if (f.guarding) setClip(f, firstValid(m, {kClipGuard, kClipIdle}), 0.8f);
        else if (f.a.move == Move::Walk || f.a.move == Move::Run) setClip(f, kClipMove, f.a.move == Move::Run ? 1.5f : 1.0f);
        else setClip(f, kClipIdle, 0.6f);
    }
    f.anim.update(m, f.animSpeed);

    float target;
    if (f.act < 0 && (f.a.move == Move::Walk || f.a.move == Move::Run)) target = faceYaw(f.moveX, f.moveZ);
    else target = faceYaw(other.x - f.x, other.z - f.z);
    f.yaw = wrapAngle(f.yaw + wrapAngle(target - f.yaw) * 0.18f);
}

// Orbit camera: follows the midpoint of the two fighters and backs off to keep both in view.
// Q/E, right mouse drag (horizontal) or the right stick turn it; drag vertical / stick tilt it;
// wheel or PgUp/PgDn zoom; Home resets the view.
void Battle::updateCamera(const Input* in, bool snap) {
    if (in) {
        float turn = in->camTurn * 0.045f;
        if (in->key['Q']) turn -= 0.035f;
        if (in->key['E'] && !sizePanel_) turn += 0.035f;
        camPitch_ += in->camTilt * 0.02f;
        if (in->rmb && lastMx_ >= 0) {
            turn += (in->mx - lastMx_) * 0.02f;
            camPitch_ += (in->my - lastMy_) * 0.01f;
        }
        lastMx_ = in->rmb ? in->mx : -1;
        lastMy_ = in->rmb ? in->my : -1;
        camYaw_ = wrapAngle(camYaw_ + turn);
        if (in->wheel) camZoom_ *= std::pow(0.9f, float(in->wheel));
        if (in->key[0x21]) camZoom_ *= 0.985f;  // PgUp: closer
        if (in->key[0x22]) camZoom_ *= 1.015f;  // PgDn: farther
        if (in->keyP[0x24]) { camZoom_ = 1.f; camPitch_ = 0.36f; }  // Home
        camPitch_ = std::clamp(camPitch_, 0.08f, 1.15f);
        camZoom_ = std::clamp(camZoom_, 0.45f, 2.2f);
    }
    const float maxH = std::max(me_.bodyH(), foe_.bodyH());
    const float tx = (me_.x + foe_.x) * 0.5f, tz = (me_.z + foe_.z) * 0.5f;
    const float ty = (me_.y + foe_.y) * 0.5f - maxH * 0.42f;
    const float sep = len(me_.x - foe_.x, me_.z - foe_.z);
    float want = (sep * 0.95f + maxH * 1.7f + 2600.f) * camZoom_;
    want = std::clamp(want, 2400.f, 30000.f);
    const float k = snap ? 1.f : 0.08f;
    camT_[0] += (tx - camT_[0]) * k;
    camT_[1] += (ty - camT_[1]) * k;
    camT_[2] += (tz - camT_[2]) * k;
    camDist_ += (want - camDist_) * (snap ? 1.f : 0.06f);
    cam_.target[0] = camT_[0];
    cam_.target[1] = camT_[1];
    cam_.target[2] = camT_[2];
    if (shake_ > 0) {  // screen shake = a small jitter of the look-at point
        cam_.target[0] += float((ticks_ * 7) % 5 - 2) * shake_ * 6.f;
        cam_.target[1] += float((ticks_ * 11) % 5 - 2) * shake_ * 5.f;
    }
    cam_.yaw = camYaw_;
    cam_.pitch = camPitch_;
    cam_.dist = camDist_;
    cam_.update();
}

void Battle::update(const Input& in) {
    ++ticks_;
    if (shake_ > 0) --shake_;
    if (screenFlash_ > 0) --screenFlash_;
    for (int& b : btnFx_) b += b > 0 ? -1 : b < 0 ? 1 : 0;
    if (moveNameT_ > 0) --moveNameT_;
    if (devTools_ && in.keyP[0x72]) sizePanel_ = !sizePanel_;  // F3: size dashboard (developer builds/runs only)
    if (sizeMsgT_ > 0) --sizeMsgT_;
    if (sizePanel_ && !seq_.active) {  // the fight pauses; the models keep breathing
        if (!sizeLayout_) {  // stand both fighters side by side in front of the camera
            sizeLayout_ = true;
            float fx, fz, rx, rz;
            cam_.groundAxes(fx, fz, rx, rz);
            const float gap = 1400.f + (me_.bodyH() + foe_.bodyH()) * 0.3f;
            me_.x = camT_[0] - rx * gap; me_.z = camT_[2] - rz * gap;
            foe_.x = camT_[0] + rx * gap; foe_.z = camT_[2] + rz * gap;
            for (Fighter* f : {&me_, &foe_}) {
                pullInside(*f);
                settleOnFloor(*f, true);
            }
            shots_.clear();
            me_.act = foe_.act = -1;
            foe_.dashTimer = 0;
        }
        if (phase_ == Phase::Intro) phase_ = Phase::Fight;
        updateSizePanel(in);
        animateModel(me_);
        animateModel(foe_);
        updateCamera(&in, false);
        return;
    }
    sizeLayout_ = false;

    if (seq_.active) {
        // The world holds its breath while the partner digivolves.
        updateEvoSeq();
        foe_.a.tick();
    } else {
        for (Fighter* f : {&me_, &foe_}) {
            if (f->flash > 0) --f->flash;
            if (f->invuln > 0) --f->invuln;
            if (std::fabs(f->kbx) + std::fabs(f->kbz) > 1.f) moveOnGround(*f, f->kbx, f->kbz);
            f->kbx *= 0.82f;
            f->kbz *= 0.82f;
        }

        if (phase_ == Phase::Intro) {
            if (--phaseTimer_ <= 0) phase_ = Phase::Fight;
        } else if (phase_ == Phase::Fight) {
            updatePlayer(in);
            updateEnemy();
            if (foeEvoModel_ && !foeEvolved_ && foe_.hp > 0 && foe_.hp < foe_.maxHp * 0.5f && !seq_.active) startFoeEvo();
            if (foe_.hp <= 0 || me_.hp <= 0) {
                won_ = foe_.hp <= 0;
                phase_ = Phase::Ending;
                phaseTimer_ = 160;
                banner_ = won_ ? "VICTORY!" : "DEFEATED...";
                if (won_) burst(foe_.x, foe_.cy(), foe_.z, rgba(255, 255, 200), 60, 3.f);
                shots_.clear();
                me_.act = foe_.act = -1;
                if (!won_ && me_.evo > 0) startEvoSeq(0);  // knocked out: falls back to Rookie
            }
        } else {
            if (won_) foe_.deathFade = std::max(0.f, foe_.deathFade - 0.02f);
            else if (!me_.model) me_.deathFade = std::max(0.f, me_.deathFade - 0.02f);
            me_.a.move = foe_.a.move = Move::Idle;
            if (--phaseTimer_ <= 0 || (phaseTimer_ < 100 && in.confirmP)) done_ = true;
        }

        // projectiles (3D)
        for (auto& s : shots_) {
            s.x += s.vx;
            s.y += s.vy;
            s.z += s.vz;
            --s.life;
            if (s.fx && (s.fxT += 1.f) > s.fxLoopTo) s.fxT = s.fxLoopFrom;  // keep the in-flight frames looping
            if (frand(g_seed) < (s.fx ? 0.2f : 0.5f))
                sparks_.push_back({s.x, s.y, s.z, -s.vx * 0.2f, -s.vy * 0.2f, -s.vz * 0.2f, 10, 10, s.r * 0.6f, s.color});
            Fighter& target = s.owner == 0 ? foe_ : me_;
            Fighter& source = s.owner == 0 ? me_ : foe_;
            const float tr = target.model ? target.height * 0.3f : 9.f * target.scale * kPxW;
            if (len3(s.x - target.x, (s.y - target.cy()) * 0.8f, s.z - target.z) < s.r + tr) {
                hit(target, source, s.dmg, s.x - s.vx * 4, s.z - s.vz * 4);
                s.life = 0;
            }
            const float floorY = arena_ ? arena_->arena.playY : 0.f;
            const float ax = arena_ ? arena_->arena.centerX : 0.f, az = arena_ ? arena_->arena.centerZ : 0.f;
            if (len(s.x - ax, s.z - az) > 40000.f || s.y > floorY + 600.f) s.life = 0;  // left the arena / hit the ground
        }
        shots_.erase(std::remove_if(shots_.begin(), shots_.end(), [](const Shot& s) { return s.life <= 0; }), shots_.end());
        animateModel(me_);
        animateModel(foe_);
        me_.a.tick();
        foe_.a.tick();
    }
    settleOnFloor(me_, false);
    settleOnFloor(foe_, false);
    updateCamera(&in, false);

    for (auto& p : sparks_) { p.x += p.vx; p.y += p.vy; p.z += p.vz; p.vy += 0.05f * kPxW; --p.life; }
    sparks_.erase(std::remove_if(sparks_.begin(), sparks_.end(), [](const Spark& p) { return p.life <= 0; }), sparks_.end());
    for (auto& p : popups_) { p.rise += 0.5f; --p.life; }
    popups_.erase(std::remove_if(popups_.begin(), popups_.end(), [](const Popup& p) { return p.life <= 0; }), popups_.end());
}

// ---------------------------------------------------------------- rendering
void Battle::glow3D(float x, float y, float z, float radius, uint32_t color) const {
    float sx, sy, d;
    if (!cam_.project(x, y, z, sx, sy, d)) return;
    gfx::drawGlow(sx, sy, std::max(0.6f, radius * cam_.scaleAt(d)), color);
}

void Battle::drawFighter3D(Fighter& f) {
    if (f.deathFade <= 0.f || !f.model) return;
    const bool isMe = &f == &me_;
    uint32_t tint = f.tint;
    if (!isMe && f.act >= 0 && f.actTimer > (f.act == 1 ? 8 : 12))  // telegraph wind-up in red
        tint = (ticks_ / 3) % 2 ? rgba(255, 120, 120) : 0xffffffffu;
    drawGroundShadow3D(f.x, f.y, f.z, f.height * 0.32f, 0.45f * f.deathFade);
    WorldPlace p;
    p.x = f.x;
    p.y = f.y;
    p.z = f.z;
    p.yaw = f.yaw;
    p.scale = f.height / std::max(1.f, f.model->model.height);
    p.tint = (tint & 0x00ffffffu) | (uint32_t(255 * f.deathFade) << 24);
    p.flash = f.flash > 0 ? f.flash / 8.f : 0.f;
    if (seq_.active && seq_.foe != isMe) {
        p.flash = seq_.flash;
        p.silhouette = seq_.silhouette;
        if (p.silhouette) p.tint = 0xffffffffu;
    }
    drawModelWorld(f.model->model, f.model->gpu, f.anim.pose(), cam_, p);
}

// Sprite fighters (no battle model on disk) are drawn as camera-facing billboards.
void Battle::drawFighterSprite(Fighter& f) {
    if (f.deathFade <= 0.f || f.model) return;
    float sx, sy, d;
    if (!cam_.project(f.x, f.y, f.z, sx, sy, d)) return;
    uint32_t tint = f.tint;
    if (&f == &foe_ && f.act >= 0 && f.actTimer > (f.act == 1 ? 8 : 12)) tint = (ticks_ / 3) % 2 ? rgba(255, 120, 120) : 0xffffffffu;
    tint = (tint & 0x00ffffffu) | (uint32_t(255 * f.deathFade) << 24);
    f.a.x = sx;
    f.a.y = sy;
    f.a.draw(0, 0, f.scale * kPxW * cam_.scaleAt(d), tint, f.flash > 0 ? f.flash / 8.f : 0.f);
}

void Battle::render() {
    // ---- 3D: arena, fighters, technique models ----
    cam_.begin();
    if (arena_) {
        WorldPlace ap;
        ap.shade = false;
        ap.passes = 1;
        const uint8_t dk = uint8_t(255 * (1.f - darken_));
        ap.tint = rgba(dk, dk, uint8_t(std::min(255, dk + 8)));
        drawModelWorld(arena_->arena.model, arena_->gpu, arena_->arena.pose, cam_, ap);
    }
    drawFighter3D(me_);
    drawFighter3D(foe_);
    for (const auto& s : shots_)
        if (s.fx) drawShotFx(s);
    if (arena_) {  // the arena's semi-transparent parts (water, light, glass) last
        WorldPlace ap;
        ap.shade = false;
        ap.passes = 2;
        drawModelWorld(arena_->arena.model, arena_->gpu, arena_->arena.pose, cam_, ap);
    }
    Camera3D::end();

    // ---- 2D overlays projected from the world ----
    // the bottom of the screen darker so the button panel reads
    for (int i = 0; i < 8; ++i) {
        const float k = float(8 - i) / 8.f;
        gfx::fillRect(0, kScreenH - float(i + 1) * 5.f, kScreenW, 5.f, rgba(0, 0, 0, uint8_t(110 * k * k)));
    }
    // digivolution light pillar around whoever digivolves
    if (seq_.active) {
        const Fighter& sf = seq_.foe ? foe_ : me_;
        const float B = seq_.up ? 110.f : 54.f;
        const float k = std::min(smooth(0, 30, float(seq_.t)), 1.f - smooth(B, B + 50, float(seq_.t)));
        const uint32_t c = seq_.up ? rgba(255, 210, 90, uint8_t(70 * k)) : rgba(150, 190, 255, uint8_t(60 * k));
        for (int i = 0; i < 12; ++i)
            glow3D(sf.x, sf.y - float(i) * 20.f * kPxW, sf.z,
                   22.f * kPxW + sf.bodyH() * 0.25f + 3.f * kPxW * std::sin(float(ticks_) * 0.3f + float(i)), c);
    }
    // sprite fighters, far one first
    {
        float d0 = 0, d1 = 0, sx, sy;
        cam_.project(me_.x, me_.y, me_.z, sx, sy, d0);
        cam_.project(foe_.x, foe_.y, foe_.z, sx, sy, d1);
        Fighter* order[2] = {&me_, &foe_};
        if (d0 < d1) std::swap(order[0], order[1]);
        for (Fighter* f : order) drawFighterSprite(*f);
    }
    for (const Fighter* f : {&me_, &foe_})
        if (f->guarding) glow3D(f->x, f->cy(), f->z, f->bodyH() * 0.8f, rgba(100, 180, 255, 110));
    if (sizePanel_) {  // measuring lines: feet and head of each fighter
        for (const Fighter* f : {&me_, &foe_}) {
            const uint32_t c = f == &me_ ? rgba(80, 220, 255, 200) : rgba(255, 120, 90, 200);
            float fx, fy, hx, hy;
            if (!toScreen(f->x, f->y, f->z, fx, fy) || !toScreen(f->x, f->y - f->bodyH(), f->z, hx, hy)) continue;
            const float w = std::max(24.f, (fy - hy) * 0.7f);
            gfx::fillRect(fx - w * 0.5f, fy, w, 1, c);
            gfx::fillRect(fx - w * 0.5f, hy, w, 1, c);
            gfx::fillRect(fx - w * 0.5f, hy, 1, fy - hy, c);
        }
    }
    for (const auto& s : shots_) {
        if (s.fx) {  // the original technique model, with a soft halo
            glow3D(s.x, s.y, s.z, s.r * 2.0f, (s.color & 0x00ffffffu) | 0x50000000u);
            continue;
        }
        glow3D(s.x, s.y, s.z, s.r * 2.6f, (s.color & 0x00ffffffu) | 0xb0000000u);
        glow3D(s.x, s.y, s.z, s.r * 1.1f, rgba(255, 255, 230, 230));
    }
    for (const auto& p : sparks_)
        glow3D(p.x, p.y, p.z, p.r, (p.color & 0x00ffffffu) | (uint32_t(220 * p.life / p.max) << 24));

    // ---- bars (virtual coords) ----
    auto bar = [](float x, float y, float w, float h, float frac, uint32_t col) {
        gfx::fillRect(x - 1, y - 1, w + 2, h + 2, 0xc0000000u);
        gfx::fillRect(x, y, w * std::clamp(frac, 0.f, 1.f), h, col);
    };
    gfx::fillRect(6, 6, 132, 40, 0xa0100810u);
    bar(10, 22, 124, 5, me_.hp / me_.maxHp, me_.hp / me_.maxHp < 0.3f ? 0xff3030e0u : 0xff40d040u);
    bar(10, 30, 124, 3, me_.mp / me_.maxMp, 0xffe0a040u);
    bool ready = me_.dv >= 100.f && me_.evo < kMaxEvo;
    uint32_t dvCol = ready ? ((ticks_ / 8) % 2 ? 0xff40ffffu : 0xff00a0ffu) : 0xff00c0ffu;
    bar(10, 37, 124, 4, me_.evo >= kMaxEvo ? 1.f : me_.dv / 100.f, dvCol);
    gfx::fillRect(184, 6, 130, 22, 0xa0100810u);
    bar(188, 20, 122, 5, foe_.hp / foe_.maxHp, foe_.boss ? 0xff3030ffu : 0xff3060f0u);

    if (screenFlash_ > 0) gfx::fillRect(0, 0, kScreenW, kScreenH, rgba(255, 255, 255, uint8_t(std::min(255, 9 * screenFlash_))));
}
void Battle::renderHud(int ww, int wh) {
    int x, y;
    char buf[160];
    gfx::toWindow(10, 8, x, y);
    std::snprintf(buf, sizeof buf, "%s  Lv%d  %s", formName().c_str(), me_.level, evoStage(me_.evo).name);
    gfx::drawText(x, y + 10, buf, 0xffffffffu);
    gfx::toWindow(10, 48, x, y);
    bool ready = me_.dv >= 100.f && me_.evo < kMaxEvo;
    std::snprintf(buf, sizeof buf, "HP %d/%d  MP %d   %s", int(me_.hp), int(me_.maxHp), int(me_.mp),
                  ready ? "DIGIVOLVE READY [U]" : me_.evo >= kMaxEvo ? "MAX FORM" : "");
    gfx::drawText(x, y + 12, buf, ready ? 0xff40ffffu : 0xffffffffu);
    gfx::toWindow(188, 8, x, y);
    std::snprintf(buf, sizeof buf, "%s%s  Lv%d", foe_.boss ? "BOSS " : "", foe_.name.c_str(), foe_.level);
    gfx::drawText(x, y + 10, buf, foe_.boss ? 0xff4040ffu : 0xffffffffu);

    // ---- attack button panel: key, gamepad button, move name, MP cost, cooldown ----
    static const char* kKeys[3] = {"J", "K", "L"};
    static const char* kPad[3] = {"X", "B", "Y"};
    static const uint32_t kPadCol[3] = {rgba(70, 150, 255), rgba(255, 80, 70), rgba(255, 205, 40)};
    const int S = std::max(1, gfx::pixelScale());
    const bool fighting = phase_ == Phase::Fight && !seq_.active;
    for (int s = 0; s < 3 && !sizePanel_; ++s) {
        const FormAttack& atk = form()->atk[s];
        const bool cooling = me_.cd[s] > 0, noMp = me_.mp < atk.mp;
        const bool can = fighting && !cooling && !noMp && me_.act < 0;
        const bool lit = btnFx_[s] > 0 || me_.act == s;
        const bool refused = btnFx_[s] < 0;
        int x0, y0, x1, y1;
        const float bx = kBtnX + float(s) * (kBtnW + kBtnGap);
        gfx::toWindow(bx, kBtnY, x0, y0);
        gfx::toWindow(bx + kBtnW, kBtnY + kBtnH, x1, y1);
        const int w = x1 - x0, h = y1 - y0;
        const float pulse = 0.5f + 0.5f * std::sin(float(ticks_) * 0.5f);
        // body
        gfx::fillRect(float(x0), float(y0), float(w), float(h), can || lit ? rgba(14, 20, 40, 225) : rgba(26, 26, 30, 215));
        if (lit) gfx::fillRect(float(x0), float(y0), float(w), float(h), (kPadCol[s] & 0x00ffffffu) | (uint32_t(70 + 70 * pulse) << 24));
        // cooldown: a dark curtain over the part still recharging + a refill bar
        if (cooling) {
            const float k = float(me_.cd[s]) / float(std::max(1, atk.cooldown));
            gfx::fillRect(float(x1) - float(w) * k, float(y0), float(w) * k, float(h), rgba(0, 0, 0, 120));
            gfx::fillRect(float(x0), float(y1 - 2 * S), float(w) * (1.f - k), float(2 * S), (kPadCol[s] & 0x00ffffffu) | 0xc0000000u);
        }
        // border: bright when pressed, red blink when refused
        const uint32_t border = lit ? rgba(255, 255, 255) : refused && (btnFx_[s] / 3) % 2 ? rgba(255, 60, 60)
                                : can ? ((kPadCol[s] & 0x00ffffffu) | 0xc0000000u) : rgba(90, 90, 90, 200);
        const int bw = lit || refused ? 2 * S : S;
        gfx::fillRect(float(x0), float(y0), float(w), float(bw), border);
        gfx::fillRect(float(x0), float(y1 - bw), float(w), float(bw), border);
        gfx::fillRect(float(x0), float(y0), float(bw), float(h), border);
        gfx::fillRect(float(x1 - bw), float(y0), float(bw), float(h), border);
        // key badge: keyboard key on top, gamepad button below
        const int pad = 3 * S, bs = h - 2 * pad;
        const uint32_t badge = can || lit ? kPadCol[s] : rgba(80, 80, 80);
        gfx::fillRect(float(x0 + pad), float(y0 + pad), float(bs), float(bs), (badge & 0x00ffffffu) | 0xe0000000u);
        const int line1 = y0 + h / 2 - 2 * S, line2 = y0 + h - pad - 2 * S;
        gfx::drawTextCentered(x0 + pad + bs / 2, line1, kKeys[s], can || lit ? rgba(10, 10, 20) : rgba(170, 170, 170));
        std::snprintf(buf, sizeof buf, "(%s)", kPad[s]);
        gfx::drawTextCentered(x0 + pad + bs / 2, line2, buf, can || lit ? rgba(10, 10, 20) : rgba(150, 150, 150));
        // move name + cost
        const int tx = x0 + pad * 2 + bs;
        gfx::drawText(tx, line1, atk.name, can || lit ? 0xffffffffu : rgba(140, 140, 140));
        if (atk.mp > 0) std::snprintf(buf, sizeof buf, "%d MP", int(atk.mp));
        else std::snprintf(buf, sizeof buf, "free");
        gfx::drawText(tx, line2, buf, noMp ? rgba(255, 90, 90) : can || lit ? rgba(150, 220, 255) : rgba(130, 130, 130));
        if (cooling) {
            std::snprintf(buf, sizeof buf, "%.1fs", float(me_.cd[s]) / 60.f);
            gfx::drawText(x1 - pad - int(std::strlen(buf)) * gfx::kCharW, line2, buf, rgba(200, 200, 200));
        }
    }
    if (!sizePanel_) {
        int hx, hy;
        gfx::toWindow(160, kBtnY + kBtnH + 3, hx, hy);
        gfx::drawTextCentered(hx, hy + 11, me_.evo > 0 ? "WASD move  Space guard  U digivolve (hold: Rookie)  Q/E right-drag wheel: camera"
                                                     : "WASD move (Shift run)  Space/LB guard  U/RB digivolve  Q/E right-drag wheel: camera",
                              0xffb0e0ffu);
    }

    // the move just used, as a short banner over the partner
    if (moveNameT_ > 0 && !moveName_.empty()) {
        float hx = 160, hy = kHudTop;
        toScreen(me_.x, me_.y - me_.bodyH(), me_.z, hx, hy);
        gfx::toWindow(std::clamp(hx, 0.f, float(kScreenW)), std::clamp(hy - 14.f, kHudTop - 4.f, float(kScreenH) - 60.f), x, y);
        const int tw = int(moveName_.size()) * gfx::kCharW + 16;
        const uint8_t a = uint8_t(std::min(255, moveNameT_ * 12));
        const int bx = std::clamp(x - tw / 2, 4, ww - tw - 4);
        gfx::fillRect(float(bx), float(y - 15), float(tw), 20.f, rgba(0, 0, 0, uint8_t(a * 0.7f)));
        gfx::fillRect(float(bx), float(y + 3), float(tw), 2.f, rgba(255, 200, 60, a));
        gfx::drawText(bx + 8, y, moveName_.c_str(), rgba(255, 230, 120, a));
    }
    // holding U to go back to Rookie
    float fsx = 0, fsy = 0;
    if (revertHold_ > 0 && !seq_.active && toScreen(me_.x, me_.y, me_.z, fsx, fsy)) {
        gfx::toWindow(fsx - 20, fsy + 6, x, y);
        int x2, y2;
        gfx::toWindow(fsx + 20, fsy + 9, x2, y2);
        gfx::fillRect(float(x), float(y), float(x2 - x), float(y2 - y), rgba(0, 0, 0, 180));
        gfx::fillRect(float(x), float(y), float(x2 - x) * float(revertHold_) / kRevertHold, float(y2 - y), rgba(200, 220, 255));
        gfx::drawTextCentered((x + x2) / 2, y2 + 14, "back to Rookie...", rgba(200, 220, 255));
    }

    for (const auto& p : popups_) {
        float px, py;
        if (!toScreen(p.x, p.y, p.z, px, py)) continue;
        gfx::toWindow(px, py - p.rise, x, y);
        gfx::drawTextCentered(x, y, p.text.c_str(), (p.color & 0x00ffffffu) | (uint32_t(std::min(255, p.life * 8)) << 24));
    }
    if (seq_.active) {
        const int B = seq_.up ? 110 : 54;
        gfx::toWindow(160, 64, x, y);
        if (seq_.t > 12 && seq_.t < B) {
            std::snprintf(buf, sizeof buf, seq_.up ? "%s digivolve to..." : "%s de-digivolve to...", seq_.fromName.c_str());
            gfx::drawTextCentered(x, y, buf, seq_.up ? 0xff60e0ffu : 0xffffd0a0u);
        } else if (seq_.t >= B) {
            std::snprintf(buf, sizeof buf, "%s!", seq_.toName.c_str());
            gfx::drawTextCentered(x, y, buf, seq_.foe ? 0xff6060ffu : seq_.up ? 0xff40ffffu : 0xffffffffu);
            if (seq_.up && !seq_.foe) {
                gfx::toWindow(160, 74, x, y);
                const FormDef* f = form();
                std::snprintf(buf, sizeof buf, "New moves: %s / %s / %s", f->atk[0].name, f->atk[1].name, f->atk[2].name);
                gfx::drawTextCentered(x, y + 6, buf, 0xffffffffu);
            }
        }
    }
    if (phase_ != Phase::Fight) {
        gfx::toWindow(160, 100, x, y);
        gfx::drawTextCentered(x, y, banner_.c_str(), won_ || phase_ == Phase::Intro ? 0xff40ffffu : 0xff4040ffu);
        if (phase_ == Phase::Ending && phaseTimer_ < 100) gfx::drawTextCentered(x, y + 24, "[Space] continue", 0xffffffffu);
    }
    if (sizePanel_) renderSizePanel();
    (void)ww;
    (void)wh;
}

// ---------------------------------------------------------------- size dashboard
namespace {
const char* kSizeKeys[7] = {"rookie", "champion", "ultimate", "mega", "enemy", "boss", "unit"};
constexpr float kPanelX = 190, kPanelY = 44, kPanelW = 126, kRowH = 13;
constexpr int kSizeRows = 7;
}  // namespace

void Battle::setSizeFile(std::filesystem::path p) {
    sizeFile_ = std::move(p);
    loadSizes();
}

bool Battle::loadSizes() {
    std::ifstream f(sizeFile_);
    if (!f) return false;
    std::string line;
    while (std::getline(f, line)) {
        std::istringstream ss(line);
        std::string k;
        float v;
        if (!(ss >> k >> v) || k[0] == '#') continue;
        for (int i = 0; i < kSizeRows; ++i) {
            if (k != kSizeKeys[i]) continue;
            if (i < 4) sizes_.form[i] = v;
            else if (i == 4) sizes_.enemy = v;
            else if (i == 5) sizes_.boss = v;
            else sizes_.unit = v < 200.f ? BattleSizes{}.unit : v;  // < 200 = old file in screen pixels
        }
    }
    return true;
}

bool Battle::saveSizes() const {
    std::ofstream f(sizeFile_);
    if (!f) return false;
    f << "# DMW3 PC Edition battle sizes - edit live in battle with F3 (size dashboard), or by hand.\n"
         "# rookie..mega: form sizes in units (Agumon, Greymon, MetalGreymon, WarGreymon)\n"
         "# enemy: enemy size x this   boss: bosses x this on top   unit: world units per 1.0\n"
         "# (3D arena, DW3 model units: unit 2000 x rookie 0.6 = Agumon's original model height)\n";
    for (int i = 0; i < 4; ++i) f << kSizeKeys[i] << ' ' << sizes_.form[i] << '\n';
    f << "enemy " << sizes_.enemy << "\nboss " << sizes_.boss << "\nunit " << sizes_.unit << '\n';
    return bool(f);
}

void Battle::applySizes() {
    me_.height = formHeight(me_.evo);
    if (foe_.model) {
        foe_.height = foeHeight(foeEvolved_ ? 1 : 0);
        foe_.scale = foe_.height / (35.f * kPxW);
        foeEffectScale_ = (foeEvolved_ ? foeEvoFxBase_ : foeFxBase_) * foe_.height / kFxTunedH[foeEvolved_ ? 1 : 0];
    }
    settleOnFloor(me_, true);
    settleOnFloor(foe_, true);
}

// Enemy preview: its Rookie or its digivolved form, without the sequence.
void Battle::setFoeForm(bool evolved) {
    if (evolved && foeEvoModel_ && !foeEvolved_) {
        seq_.toH = foeHeight(1);
        applyFoeEvo();
    } else if (!evolved && foeEvolved_ && foeBaseModel_) {
        if (auto m = models_.get(foeBaseModel_)) {
            foe_.model = m;
            foe_.clip = -1;
            foeEvolved_ = false;
            foeEffect_ = "EFFECT/MEFT0328";
            setClip(foe_, kClipIdle, 0.6f, true);
        }
    }
    seq_ = {};
    applySizes();
}

void Battle::updateSizePanel(const Input& in) {
    float* vals[kSizeRows] = {&sizes_.form[0], &sizes_.form[1], &sizes_.form[2], &sizes_.form[3], &sizes_.enemy, &sizes_.boss, &sizes_.unit};
    const bool shift = in.key[0x10];
    int delta = 0, row = -1;
    if (in.keyP[0x26]) sizeSel_ = (sizeSel_ + kSizeRows - 1) % kSizeRows;  // Up
    if (in.keyP[0x28]) sizeSel_ = (sizeSel_ + 1) % kSizeRows;              // Down
    if (in.keyP[0x25]) { delta = -1; row = sizeSel_; }                      // Left
    if (in.keyP[0x27]) { delta = 1; row = sizeSel_; }                       // Right
    // mouse: [-] / [+] on each row, form chips and buttons at the bottom
    const float top = kPanelY + 16;
    if (in.lmbP && in.mx >= kPanelX && in.mx < kPanelX + kPanelW) {
        const int r = int((in.my - top) / kRowH);
        if (in.my >= top && r >= 0 && r < kSizeRows) {
            sizeSel_ = r;
            if (in.mx >= kPanelX + 74 && in.mx < kPanelX + 86) { delta = -1; row = r; }
            if (in.mx >= kPanelX + 112 && in.mx < kPanelX + 124) { delta = 1; row = r; }
        }
        const float fy = top + kSizeRows * kRowH + 6;
        if (in.my >= fy && in.my < fy + 12) {
            const int b = int((in.mx - kPanelX - 3) / 24);
            if (b >= 0 && b < 4) setEvo(b);
            if (b == 4) setFoeForm(!foeEvolved_);
        }
        const float by = top + kSizeRows * kRowH + 24;
        if (in.my >= by && in.my < by + 12) {
            const int b = int((in.mx - kPanelX - 3) / 40);
            if (b == 0) { sizeMsg_ = saveSizes() ? "Saved " + sizeFile_.filename().string() : "Save failed"; sizeMsgT_ = 120; }
            if (b == 1) { sizeMsg_ = loadSizes() ? "Reloaded" : "No file yet"; sizeMsgT_ = 90; applySizes(); }
            if (b == 2) { sizes_ = BattleSizes{}; sizeMsg_ = "Defaults"; sizeMsgT_ = 90; applySizes(); }
        }
    }
    if (row >= 0) {
        const float step = row == 6 ? (shift ? 250.f : 50.f) : (shift ? 0.25f : 0.05f);
        *vals[row] = std::max(row == 6 ? 200.f : 0.05f, *vals[row] + step * float(delta));
        *vals[row] = std::round(*vals[row] * 1000.f) / 1000.f;
        applySizes();
    }
    // keyboard shortcuts: 1-4 preview partner form, E enemy form, Ctrl+S save, R reload
    for (int i = 0; i < 4; ++i)
        if (in.keyP['1' + i]) setEvo(i);
    if (in.keyP['E']) setFoeForm(!foeEvolved_);
    if (in.key[0x11] && in.keyP['S']) { sizeMsg_ = saveSizes() ? "Saved " + sizeFile_.filename().string() : "Save failed"; sizeMsgT_ = 120; }
    if (in.keyP['R'] && !in.key[0x11]) { sizeMsg_ = loadSizes() ? "Reloaded" : "No file yet"; sizeMsgT_ = 90; applySizes(); }
}

void Battle::renderSizePanel() {
    char buf[96];
    int x0, y0, x1, y1;
    auto box = [&](float vx, float vy, float vw, float vh, uint32_t c) {
        gfx::toWindow(vx, vy, x0, y0);
        gfx::toWindow(vx + vw, vy + vh, x1, y1);
        gfx::fillRect(float(x0), float(y0), float(x1 - x0), float(y1 - y0), c);
    };
    auto text = [&](float vx, float vy, const char* s, uint32_t c) {
        int x, y;
        gfx::toWindow(vx, vy, x, y);
        gfx::drawText(x, y + 11, s, c);
    };
    auto ctext = [&](float vx, float vy, const char* s, uint32_t c) {
        int x, y;
        gfx::toWindow(vx, vy, x, y);
        gfx::drawTextCentered(x, y + 11, s, c);
    };
    const float top = kPanelY + 16;
    const float panelH = 16 + kSizeRows * kRowH + 44;
    box(kPanelX, kPanelY, kPanelW, panelH, rgba(8, 12, 26, 225));
    box(kPanelX, kPanelY, kPanelW, 13, rgba(40, 90, 170, 240));
    text(kPanelX + 4, kPanelY + 3, "SIZE DASHBOARD  (F3)", 0xffffffffu);
    const float vals[kSizeRows] = {sizes_.form[0], sizes_.form[1], sizes_.form[2], sizes_.form[3], sizes_.enemy, sizes_.boss, sizes_.unit};
    for (int r = 0; r < kSizeRows; ++r) {
        const float y = top + r * kRowH;
        const bool sel = r == sizeSel_;
        const bool current = r < 4 && r == me_.evo;
        if (sel) box(kPanelX + 1, y - 1, kPanelW - 2, kRowH, rgba(60, 110, 200, 110));
        std::string label = r < 4 ? formNameOf(r) : r == 4 ? "Enemy x" : r == 5 ? "Boss x" : "Unit wu";
        if (label.size() > 12) label = label.substr(0, 12);
        text(kPanelX + 4, y + 2, label.c_str(), current ? 0xff40ffffu : 0xffe0e0e0u);
        box(kPanelX + 74, y + 1, 12, kRowH - 3, rgba(90, 40, 40, 230));
        ctext(kPanelX + 80, y + 2, "-", 0xffffffffu);
        std::snprintf(buf, sizeof buf, r == 6 ? "%.0f" : "%.2f", vals[r]);
        ctext(kPanelX + 99, y + 2, buf, 0xffffffffu);
        box(kPanelX + 112, y + 1, 12, kRowH - 3, rgba(40, 90, 40, 230));
        ctext(kPanelX + 118, y + 2, "+", 0xffffffffu);
    }
    const float fy = top + kSizeRows * kRowH + 6;
    static const char* kChip[5] = {"1", "2", "3", "4", "E"};
    for (int b = 0; b < 5; ++b) {
        const bool on = b < 4 ? me_.evo == b : foeEvolved_;
        box(kPanelX + 3 + b * 24, fy, 22, 12, on ? rgba(230, 190, 40, 240) : rgba(50, 50, 70, 230));
        ctext(kPanelX + 14 + b * 24, fy + 2, kChip[b], on ? rgba(10, 10, 20) : 0xffffffffu);
    }
    const float by = top + kSizeRows * kRowH + 24;
    static const char* kBtn[3] = {"Save", "Reload", "Reset"};
    for (int b = 0; b < 3; ++b) {
        box(kPanelX + 3 + b * 40, by, 38, 12, rgba(40, 70, 120, 240));
        ctext(kPanelX + 22 + b * 40, by + 2, kBtn[b], 0xffffffffu);
    }
    std::snprintf(buf, sizeof buf, "You %.0f  Enemy %.0f wu", me_.bodyH(), foe_.bodyH());
    box(kPanelX, kPanelY + panelH, kPanelW, sizeMsgT_ > 0 ? 40 : 31, rgba(8, 12, 26, 190));
    text(kPanelX + 3, kPanelY + panelH + 3, buf, 0xffb0e0ffu);
    text(kPanelX + 3, kPanelY + panelH + 12, "Arrows/click change, Shift x5", 0xffa0a0a0u);
    text(kPanelX + 3, kPanelY + panelH + 21, "1-4 form  E enemy  Ctrl+S", 0xffa0a0a0u);
    if (sizeMsgT_ > 0) text(kPanelX + 3, kPanelY + panelH + 30, sizeMsg_.c_str(), 0xff40ffffu);
}

}  // namespace dw3
