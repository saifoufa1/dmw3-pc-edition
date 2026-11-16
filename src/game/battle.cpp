#include "game/battle.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace dw3 {

namespace {
// Invisible arena bounds in screen space (feet positions). The bottom stays above the attack
// button panel; the top keeps the fighter's head below the HP bars (see clampArena).
constexpr float kArenaL = 22, kArenaR = 298, kArenaT = 112, kArenaB = 198;
constexpr float kHudTop = 50;           // fighters' heads stay below this line
constexpr float kRookieHeightPx = 37;  // on-screen height of a Rookie model; forms grow by EvoStage::scale
// Heights the technique effect scales were tuned at (Rookie..Mega); effects scale with the form.
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
// model in DAT/MODEL either -> it stays the field sprite.
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
// Model yaw that faces a screen direction (0 = toward the camera, -pi/2 = screen right).
float yawFor(float dx, float dy) { return std::atan2(-dx, dy * 1.6f); }

bool clipValid(const Model& m, int c) { return c >= 0 && size_t(c) < m.clips.size() && m.clips[size_t(c)].valid(); }
int firstValid(const Model& m, std::initializer_list<int> cs) {
    for (int c : cs)
        if (clipValid(m, c)) return c;
    return kClipIdle;
}
}  // namespace

bool Battle::start(const BattleRequest& req, const Texture* backdrop, float bgX, float bgY, std::string& err) {
    backdrop_ = backdrop;
    bgX_ = bgX;
    bgY_ = bgY;
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

    me_.a.sprite = sprites_.get(party_.partnerId, &err);
    foe_.a.sprite = sprites_.get(req.enemySprite, &err);
    if (!me_.a.sprite || !foe_.a.sprite) return false;

    me_.name = party_.partnerName;
    me_.level = 1 + party_.wins;
    baseMaxHp_ = 120.f + 10.f * float(party_.wins);
    me_.maxHp = me_.hp = baseMaxHp_;
    me_.maxMp = me_.mp = 60;
    me_.a.x = 80; me_.a.y = 190; me_.a.dir = E;
    me_.yaw = yawFor(1, 0.3f);
    setEvo(0);

    foe_.name = req.enemyName;
    foe_.level = req.level;
    foe_.boss = req.boss;
    foe_.maxHp = foe_.hp = (50.f + req.level * 14.f) * (req.boss ? 2.4f : 1.f);
    foe_.power = (0.8f + req.level * 0.1f) * (req.boss ? 1.25f : 1.f);
    foe_.scale = req.boss ? 1.6f : 1.15f;
    foe_.a.x = 235; foe_.a.y = 165; foe_.a.dir = W;
    foe_.aiTimer = 50;
    foeEffect_ = nullptr;
    foeEffectScale_ = 0;
    foeEvoModel_ = foeEvoName_ = foeEvoEffect_ = nullptr;
    foeEvolved_ = false;
    for (const EnemyModelDef& d : kEnemyModels) {
        if (req.enemySprite != d.sprite) continue;
        foe_.model = models_.get(d.model);
        if (!foe_.model) break;  // no model on disk: field sprite
        // An enemy Rookie is exactly as tall as the partner Rookie; bosses are a bit bigger.
        foe_.heightPx = kRookieHeightPx * (req.boss ? 1.3f : 1.f);
        foe_.scale = foe_.heightPx / 35.f;  // keeps the AI's reach/spacing in proportion
        foe_.yaw = yawFor(-1, -0.3f);
        foeEffect_ = d.effect;
        foeEffectScale_ = d.effectScale * foe_.heightPx / kFxTunedH[0];
        foeEvoModel_ = d.evoModel;
        foeEvoName_ = d.evoName;
        foeEvoEffect_ = d.evoEffect;
        foeEvoEffectScale_ = d.evoEffectScale;
        setClip(foe_, kClipIdle, 0.6f, true);
        break;
    }
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
    me_.heightPx = kRookieHeightPx * next.scale;
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

void Battle::clampArena(Fighter& f) {
    f.a.x = std::clamp(f.a.x, kArenaL, kArenaR);
    const float top = std::min(kArenaB, std::max(kArenaT, kHudTop + f.bodyH() * 0.95f));
    f.a.y = std::clamp(f.a.y, top, kArenaB);
}

void Battle::burst(float x, float y, uint32_t color, int n, float speed) {
    for (int i = 0; i < n; ++i) {
        float ang = frand(g_seed) * 6.2832f, sp = speed * (0.4f + frand(g_seed));
        int life = 14 + int(frand(g_seed) * 16);
        sparks_.push_back({x, y, std::cos(ang) * sp, std::sin(ang) * sp - 0.6f, life, life, 2.f + frand(g_seed) * 3.f, color});
    }
}

void Battle::fireShot(const Fighter& from, const Fighter& to, float dmg, float speed, float r, float angOff, uint32_t color, int owner) {
    // Projectiles leave from the mouth (upper body, toward the target).
    const float mouthY = from.a.y - from.bodyH() * (from.model ? 0.62f : 0.5f);
    float dx = to.cx() - from.cx(), dy = to.cy() - mouthY;
    float a = std::atan2(dy, dx) + angOff;
    const float reach = from.model ? from.heightPx * 0.35f : 10.f * from.scale;
    float fx = from.cx() + std::cos(a) * reach, fy = mouthY + std::sin(a) * reach * 0.6f;
    shots_.push_back({fx, fy, std::cos(a) * speed, std::sin(a) * speed, dmg, r, owner, 150, color});
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
    s.fxScale = effectScale;
    s.fxT = float(launch);
    s.fxLoopTo = float(arrive - 1);
    s.fxLoopFrom = std::max(float(launch), (float(launch) + s.fxLoopTo) * 0.5f);
}

void Battle::drawShotFx(const Shot& s, float camX, float camY) {
    const Model& m = s.fx->model;
    Pose pose;
    samplePose(m, s.fxT, pose);
    for (size_t i = 0; i < m.nodes.size(); ++i)
        if (isTravelTrack(m.tracks[size_t(m.nodes[i].track)])) pose[i].px = pose[i].py = pose[i].pz = 0;
    ModelView v;
    v.x = s.x - camX;
    v.y = s.y - camY;
    v.pxPerUnit = s.fxScale;
    v.yaw = std::atan2(-s.vx, s.vy * 1.6f);  // the effect's forward (-z) along the flight
    v.pitch = 0.32f;
    drawModel(m, s.fx->gpu, pose, v);
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

void Battle::hit(Fighter& t, Fighter& attacker, float dmg, float fromX, float fromY) {
    if (t.invuln > 0 || t.hp <= 0) return;
    if (t.guarding) {
        dmg *= 0.25f;
        burst(t.cx(), t.cy(), rgba(120, 200, 255), 6, 1.5f);
    }
    dmg = std::max(1.f, std::round(dmg));
    t.hp = std::max(0.f, t.hp - dmg);
    t.flash = 8;
    t.invuln = 10;
    t.stun = t.guarding ? 4 : 12;
    float dx = t.a.x - fromX, dy = t.a.y - fromY, l = std::max(1.f, len(dx, dy));
    float kb = t.guarding ? 1.2f : 3.0f;
    t.kbx = dx / l * kb;
    t.kby = dy / l * kb * 0.6f;
    if (t.model && !t.guarding && t.act < 0) setClip(t, kClipHurt, 1.2f, true);
    shake_ = std::max(shake_, foe_.boss ? 8 : 5);
    burst(t.cx(), t.cy(), rgba(255, 220, 120), 10, 2.2f);
    char buf[16];
    std::snprintf(buf, sizeof buf, "%d", int(dmg));
    popups_.push_back({t.cx(), t.a.y - t.bodyH() - 6.f, buf, 50, &t == &me_ ? 0xff5050ffu : 0xffffffffu});
    // Digivolve gauge: fills from dealing and taking damage.
    if (&attacker == &me_) me_.dv = std::min(100.f, me_.dv + dmg * 1.1f);
    else me_.dv = std::min(100.f, me_.dv + dmg * 1.5f);
}

void Battle::resolvePlayerAttack() {
    Fighter& f = me_;
    const FormAttack& atk = form()->atk[f.act];
    const float pw = f.power;
    const int elapsed = f.actTotal - f.actTimer;
    const float dx = foe_.a.x - f.a.x, dy = foe_.a.y - f.a.y, d = std::max(1.f, len(dx, dy));
    const float reach = (f.model ? 10.f + f.heightPx * 0.42f : 24.f * f.scale) * atk.size;
    const float foeR = bodyRadius(foe_);

    // Lunges: close the distance before the strike lands.
    const int nextHit = f.hitsDone < int(f.hitTicks.size()) ? f.hitTicks[size_t(f.hitsDone)] : -1;
    if ((atk.fx == AtkFx::Dash || atk.fx == AtkFx::Combo) && nextHit >= 0 && elapsed < nextHit && d > reach * 0.7f) {
        const float sp = atk.fx == AtkFx::Dash ? 3.4f : 1.8f;
        f.a.x += dx / d * sp;
        f.a.y += dy / d * sp * 0.75f;
        f.yaw = yawFor(dx, dy);
    }

    while (f.hitsDone < int(f.hitTicks.size()) && elapsed >= f.hitTicks[size_t(f.hitsDone)]) {
        const int k = f.hitsDone++;
        switch (atk.fx) {
            case AtkFx::Shot:
                fireShot(f, foe_, atk.dmg * pw, 3.8f, atk.size * std::sqrt(f.model ? f.heightPx / 40.f : f.scale), 0, atk.color, 0);
                attachFx(shots_.back(), atk.effect, atk.effectScale * me_.heightPx / kFxTunedH[std::clamp(me_.evo, 0, 3)]);
                break;
            case AtkFx::Orb:
                fireShot(f, foe_, atk.dmg * pw, 2.8f, atk.size, 0, atk.color, 0);
                attachFx(shots_.back(), atk.effect, atk.effectScale * me_.heightPx / kFxTunedH[std::clamp(me_.evo, 0, 3)]);
                burst(f.cx(), f.cy() - f.bodyH() * 0.15f, atk.color, 12, 2.f);
                break;
            case AtkFx::Spread: {
                const int n = int(f.hitTicks.size());
                const float off = n > 1 ? (float(k) - (n - 1) / 2.f) * 0.2f : 0.f;
                fireShot(f, foe_, atk.dmg * pw, 3.4f, atk.size * std::sqrt(f.model ? f.heightPx / 40.f : f.scale), off, atk.color, 0);
                attachFx(shots_.back(), atk.effect, atk.effectScale * me_.heightPx / kFxTunedH[std::clamp(me_.evo, 0, 3)]);
                break;
            }
            case AtkFx::Melee:
            case AtkFx::Dash:
            case AtkFx::Combo: {
                float dist = len(foe_.cx() - f.cx(), (foe_.cy() - f.cy()) * 1.4f);
                float sx = f.a.x + dx / d * reach * 0.6f, sy = f.cy() + dy / d * 6.f;
                burst(sx, sy, atk.color, 6, 1.6f);
                if (dist < reach + foeR) hit(foe_, f, atk.dmg * pw, f.a.x, f.a.y);
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
    seq_.fromH = me_.heightPx;
    seq_.toH = kRookieHeightPx * evoStage(to).scale;
    seq_.fromName = formNameOf(seq_.from);
    seq_.toName = formNameOf(to);
    me_.act = -1;
    me_.guarding = false;
    me_.stun = 0;
    me_.kbx = me_.kby = 0;
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
    seq_.fromH = foe_.heightPx;
    seq_.toH = kRookieHeightPx * evoStage(1).scale * (foe_.boss ? 1.3f : 1.f);
    seq_.fromName = foe_.name;
    seq_.toName = foeEvoName_;
    foe_.act = -1;
    foe_.dashTimer = 0;
    foe_.stun = 0;
    foe_.kbx = foe_.kby = 0;
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
    foe_.scale = seq_.toH / 35.f;
    foeEffect_ = foeEvoEffect_;
    foeEffectScale_ = foeEvoEffectScale_ * seq_.toH / kFxTunedH[1];
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
    const float toFoe = yawFor(other.a.x - f.a.x, other.a.y - f.a.y);

    if (t < A) {  // gathering power: glow, rising sparks, turning white
        darken_ = std::min(s.up ? 0.6f : 0.35f, darken_ + 0.02f);
        if (f.model) setClip(f, firstValid(f.model->model, {kClipCharge, kClipPower, kClipIdle}), 0.8f);
        s.flash = smooth(A * 0.35f, A, t);
        f.yaw += 0.02f + 0.3f * smooth(A * 0.4f, A, t);
        if (s.t % 2 == 0) {
            float px = f.a.x + (frand(g_seed) - 0.5f) * f.heightPx * 0.9f;
            sparks_.push_back({px, f.a.y - frand(g_seed) * 6.f, 0, -2.2f - frand(g_seed) * 1.5f, 34, 34,
                               2.f + frand(g_seed) * 2.f, s.up ? rgba(255, 230, 140) : rgba(200, 220, 255)});
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
        f.heightPx = s.fromH + (s.toH - s.fromH) * k;
        if (s.t % 3 == 0) burst(f.cx(), f.cy(), rgba(255, 255, 255), 2, 2.5f);
    } else {
        if (s.t == int(B)) {
            s.silhouette = false;
            screenFlash_ = 30;
            shake_ = 16;
            burst(f.cx(), f.cy(), s.up ? rgba(255, 230, 120) : rgba(200, 220, 255), 60, 3.5f);
            f.heightPx = s.toH;
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
            popups_.push_back({f.cx(), f.a.y - f.bodyH() - 8.f, "No MP", 40, 0xffa0a0a0u});
            continue;
        }
        btnFx_[s] = 26;  // the pressed button lights up
        moveName_ = atk.name;
        moveNameT_ = 75;
        f.mp -= atk.mp;
        f.cd[s] = atk.cooldown;
        f.a.dir = dirFromVector(foe_.a.x - f.a.x, foe_.a.y - f.a.y);  // auto-face target
        f.yaw = yawFor(foe_.a.x - f.a.x, foe_.a.y - f.a.y);
        startPlayerAttack(s);
        return;
    }

    float ix = float(in.right) - float(in.left), iy = float(in.down) - float(in.up);
    if (ix != 0 || iy != 0) {
        float l = len(ix, iy);
        float sp = (in.run ? 2.4f : 1.6f);
        f.a.x += ix / l * sp;
        f.a.y += iy / l * sp * 0.75f;
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

    const float dx = me_.a.x - e.a.x, dy = me_.a.y - e.a.y, d = len(dx, dy);
    if (e.act >= 0) {
        const int total = e.act == 1 ? 26 : 30;
        const int fireAt = total - 18;  // wind-up = telegraph
        if (e.actTimer == fireAt) {
            if (e.act == 1) {
                if (len(me_.cx() - e.cx(), (me_.cy() - e.cy()) * 1.4f) < 30.f * e.scale + me_.bodyH() * 0.2f)
                    hit(me_, e, 9.f * e.power, e.a.x, e.a.y);
                burst(e.cx() + dx / std::max(1.f, d) * 14.f, e.cy(), rgba(255, 120, 120), 8, 2.f);
            } else {
                int n = e.boss ? 3 : 1;
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
        e.a.x += dx / std::max(1.f, d) * 2.6f;
        e.a.y += dy / std::max(1.f, d) * 2.0f;
        e.a.dir = dirFromVector(dx, dy);
        e.a.move = Move::Run;
        if (d < 20.f * e.scale + me_.bodyH() * 0.2f && me_.invuln == 0) { hit(me_, e, 7.f * e.power, e.a.x, e.a.y); e.dashTimer = 0; }
        return;
    }

    if (--e.aiTimer <= 0) {
        e.a.dir = dirFromVector(dx, dy);
        float r = frand(g_seed);
        if (d < 36.f * e.scale + me_.bodyH() * 0.2f) startAttack(e, 1, 26);
        else if (r < 0.6f) startAttack(e, 0, 30);
        else e.dashTimer = 40;
        e.aiTimer = e.boss ? 45 + int(frand(g_seed) * 35) : 70 + int(frand(g_seed) * 45);
        return;
    }

    // keep a fighting distance (bigger partner forms push it out), strafing a little
    const float want = (e.boss ? 75.f : 60.f) + me_.bodyH() * 0.3f;
    float mvx = 0, mvy = 0;
    if (d > want + 10) { mvx = dx / d; mvy = dy / d; }
    else if (d < want - 15) { mvx = -dx / d; mvy = -dy / d; }
    mvy += std::sin(ticks_ * 0.03f) * 0.5f;
    if (len(mvx, mvy) > 0.2f) {
        e.a.x += mvx * 1.1f;
        e.a.y += mvy * 0.8f;
        e.a.dir = dirFromVector(dx, dy);
        e.a.move = Move::Walk;
    } else {
        e.a.dir = dirFromVector(dx, dy);
        e.a.move = Move::Idle;
    }
}

// Picks the model clip from what the fighter is doing and turns it toward where it looks.
void Battle::animateModel(Fighter& f) {
    if (!f.model) return;
    const Model& m = f.model->model;
    const Fighter& other = &f == &me_ ? foe_ : me_;
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
    if (f.act < 0 && (f.a.move == Move::Walk || f.a.move == Move::Run)) {
        float vx, vy;
        dirVector(f.a.dir, vx, vy);
        target = yawFor(vx, vy);
    } else {
        target = yawFor(other.a.x - f.a.x, other.a.y - f.a.y);
    }
    f.yaw = wrapAngle(f.yaw + wrapAngle(target - f.yaw) * 0.18f);
}

void Battle::update(const Input& in) {
    ++ticks_;
    if (shake_ > 0) --shake_;
    if (screenFlash_ > 0) --screenFlash_;
    for (int& b : btnFx_) b += b > 0 ? -1 : b < 0 ? 1 : 0;
    if (moveNameT_ > 0) --moveNameT_;

    if (seq_.active) {
        // The world holds its breath while the partner digivolves.
        updateEvoSeq();
        foe_.a.tick();
    } else {
        for (Fighter* f : {&me_, &foe_}) {
            if (f->flash > 0) --f->flash;
            if (f->invuln > 0) --f->invuln;
            f->a.x += f->kbx;
            f->a.y += f->kby;
            f->kbx *= 0.82f;
            f->kby *= 0.82f;
            clampArena(*f);
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
                if (won_) burst(foe_.cx(), foe_.cy(), rgba(255, 255, 200), 60, 3.f);
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

        // projectiles
        for (auto& s : shots_) {
            s.x += s.vx;
            s.y += s.vy;
            --s.life;
            if (s.fx && (s.fxT += 1.f) > s.fxLoopTo) s.fxT = s.fxLoopFrom;  // keep the in-flight frames looping
            if (frand(g_seed) < (s.fx ? 0.2f : 0.5f)) sparks_.push_back({s.x, s.y, -s.vx * 0.2f, -s.vy * 0.2f, 10, 10, s.r * 0.6f, s.color});
            Fighter& target = s.owner == 0 ? foe_ : me_;
            Fighter& source = s.owner == 0 ? me_ : foe_;
            const float tr = target.model ? target.heightPx * 0.3f : 9.f * target.scale;
            if (len(s.x - target.cx(), (s.y - target.cy()) * 1.3f) < s.r + tr) {
                hit(target, source, s.dmg, s.x - s.vx * 4, s.y - s.vy * 4);
                s.life = 0;
            }
            if (s.x < -20 || s.x > kScreenW + 20 || s.y < 20 || s.y > kScreenH + 20) s.life = 0;
        }
        shots_.erase(std::remove_if(shots_.begin(), shots_.end(), [](const Shot& s) { return s.life <= 0; }), shots_.end());
        animateModel(me_);
        animateModel(foe_);
        me_.a.tick();
        foe_.a.tick();
    }

    for (auto& p : sparks_) { p.x += p.vx; p.y += p.vy; p.vy += 0.05f; --p.life; }
    sparks_.erase(std::remove_if(sparks_.begin(), sparks_.end(), [](const Spark& p) { return p.life <= 0; }), sparks_.end());
    for (auto& p : popups_) { p.y -= 0.5f; --p.life; }
    popups_.erase(std::remove_if(popups_.begin(), popups_.end(), [](const Popup& p) { return p.life <= 0; }), popups_.end());
}

void Battle::drawFighter(Fighter& f, float camX, float camY) {
    if (f.deathFade <= 0.f) return;
    const bool isMe = &f == &me_;
    uint32_t tint = f.tint;
    if (!isMe && f.act >= 0 && f.actTimer > (f.act == 1 ? 8 : 12))  // telegraph wind-up in red
        tint = (ticks_ / 3) % 2 ? rgba(255, 120, 120) : 0xffffffffu;

    if (f.model) {
        drawGroundShadow(f.a.x - camX, f.a.y - camY, f.heightPx * 0.32f, f.heightPx * 0.1f, 0.45f * f.deathFade);
        ModelView v;
        v.x = f.a.x - camX;
        v.y = f.a.y - camY;
        v.pxPerUnit = f.heightPx / std::max(1.f, f.model->model.height);
        v.yaw = f.yaw;
        v.tint = (tint & 0x00ffffffu) | (uint32_t(255 * f.deathFade) << 24);
        v.flash = f.flash > 0 ? f.flash / 8.f : 0.f;
        if (seq_.active && seq_.foe != isMe) {
            v.flash = seq_.flash;
            v.silhouette = seq_.silhouette;
            if (v.silhouette) v.tint = 0xffffffffu;
        }
        drawModel(f.model->model, f.model->gpu, f.anim.pose(), v);
    } else {
        tint = (tint & 0x00ffffffu) | (uint32_t(255 * f.deathFade) << 24);
        f.a.draw(camX, camY, f.scale, tint, f.flash > 0 ? f.flash / 8.f : 0.f);
    }
    if (f.guarding) gfx::drawGlow(f.cx() - camX, f.cy() - camY, f.bodyH() * 0.8f, rgba(100, 180, 255, 110));
}

void Battle::render() {
    const float sx = shake_ > 0 ? float((ticks_ * 7) % 5 - 2) * (shake_ / 6.f) : 0.f;
    const float sy = shake_ > 0 ? float((ticks_ * 11) % 5 - 2) * (shake_ / 8.f) : 0.f;
    const float camX = -sx, camY = -sy;
    // The fight happens right on the field map where the encounter started: the same view of
    // the stage, only a light vignette (darker at the bottom so the button panel reads).
    if (backdrop_) gfx::drawTexture(*backdrop_, -bgX_ + sx, -bgY_ + sy);
    for (int i = 0; i < 8; ++i) {
        const float k = float(8 - i) / 8.f;
        gfx::fillRect(0, float(i) * 3.f, kScreenW, 3.f, rgba(0, 0, 0, uint8_t(60 * k * k)));
        gfx::fillRect(0, kScreenH - float(i + 1) * 5.f, kScreenW, 5.f, rgba(0, 0, 0, uint8_t(110 * k * k)));
        gfx::fillRect(float(i) * 3.f, 0, 3.f, kScreenH, rgba(0, 0, 0, uint8_t(45 * k * k)));
        gfx::fillRect(kScreenW - float(i + 1) * 3.f, 0, 3.f, kScreenH, rgba(0, 0, 0, uint8_t(45 * k * k)));
    }
    if (darken_ > 0) gfx::fillRect(0, 0, kScreenW, kScreenH, rgba(0, 0, 8, uint8_t(255 * darken_)));

    // digivolution light pillar behind whoever digivolves
    if (seq_.active) {
        const Fighter& sf = seq_.foe ? foe_ : me_;
        const float B = seq_.up ? 110.f : 54.f;
        const float k = std::min(smooth(0, 30, float(seq_.t)), 1.f - smooth(B, B + 50, float(seq_.t)));
        const uint32_t c = seq_.up ? rgba(255, 210, 90, uint8_t(70 * k)) : rgba(150, 190, 255, uint8_t(60 * k));
        for (int i = 0; i < 12; ++i)
            gfx::drawGlow(sf.a.x - camX, sf.a.y - camY - i * 20.f, 22.f + sf.heightPx * 0.25f + 3.f * std::sin(ticks_ * 0.3f + i), c);
    }

    Fighter* order[2] = {&me_, &foe_};
    if (foe_.a.y < me_.a.y) std::swap(order[0], order[1]);
    for (Fighter* f : order) drawFighter(*f, camX, camY);
    if (me_.act >= 0 && !me_.model && form()->atk[me_.act].fx == AtkFx::Melee && me_.actTimer > 8) {  // claw streak (sprite)
        float fx, fy;
        dirVector(me_.a.dir, fx, fy);
        gfx::drawGlow(me_.cx() + fx * 18 * me_.scale - camX, me_.cy() + fy * 8 - camY, 10 * me_.scale, rgba(255, 255, 255, 140));
    }
    for (const auto& s : shots_) {
        if (s.fx) {  // the original technique model
            gfx::drawGlow(s.x - camX, s.y - camY, s.r * 2.0f, (s.color & 0x00ffffffu) | 0x50000000u);
            drawShotFx(s, camX, camY);
            continue;
        }
        gfx::drawGlow(s.x - camX, s.y - camY, s.r * 2.6f, (s.color & 0x00ffffffu) | 0xb0000000u);
        gfx::drawGlow(s.x - camX, s.y - camY, s.r * 1.1f, rgba(255, 255, 230, 230));
    }
    for (const auto& p : sparks_)
        gfx::drawGlow(p.x - camX, p.y - camY, p.r, (p.color & 0x00ffffffu) | (uint32_t(220 * p.life / p.max) << 24));

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
    for (int s = 0; s < 3; ++s) {
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
    {
        int hx, hy;
        gfx::toWindow(160, kBtnY + kBtnH + 3, hx, hy);
        gfx::drawTextCentered(hx, hy + 11, me_.evo > 0 ? "Move WASD (Shift run)   Guard Space/LB   Digivolve U/RB   Hold U: back to Rookie"
                                                     : "Move WASD (Shift run)   Guard Space/LB   Digivolve U/RB",
                              0xffb0e0ffu);
    }

    // the move just used, as a short banner over the partner
    if (moveNameT_ > 0 && !moveName_.empty()) {
        const float vy = std::max(kHudTop - 4.f, me_.a.y - me_.bodyH() - 14.f);
        gfx::toWindow(me_.cx(), vy, x, y);
        const int tw = int(moveName_.size()) * gfx::kCharW + 16;
        const uint8_t a = uint8_t(std::min(255, moveNameT_ * 12));
        const int bx = std::clamp(x - tw / 2, 4, ww - tw - 4);
        gfx::fillRect(float(bx), float(y - 15), float(tw), 20.f, rgba(0, 0, 0, uint8_t(a * 0.7f)));
        gfx::fillRect(float(bx), float(y + 3), float(tw), 2.f, rgba(255, 200, 60, a));
        gfx::drawText(bx + 8, y, moveName_.c_str(), rgba(255, 230, 120, a));
    }
    // holding U to go back to Rookie
    if (revertHold_ > 0 && !seq_.active) {
        gfx::toWindow(me_.cx() - 20, me_.a.y + 6, x, y);
        int x2, y2;
        gfx::toWindow(me_.cx() + 20, me_.a.y + 9, x2, y2);
        gfx::fillRect(float(x), float(y), float(x2 - x), float(y2 - y), rgba(0, 0, 0, 180));
        gfx::fillRect(float(x), float(y), float(x2 - x) * float(revertHold_) / kRevertHold, float(y2 - y), rgba(200, 220, 255));
        gfx::drawTextCentered((x + x2) / 2, y2 + 14, "back to Rookie...", rgba(200, 220, 255));
    }

    for (const auto& p : popups_) {
        gfx::toWindow(p.x, p.y, x, y);
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
    (void)ww;
    (void)wh;
}

}  // namespace dw3
