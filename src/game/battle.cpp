#include "game/battle.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace dw3 {

namespace {
// Arena bounds in screen space (feet positions).
constexpr float kArenaL = 22, kArenaR = 298, kArenaT = 112, kArenaB = 226;
constexpr int kEvoDuration = 60 * 25;  // a digivolution lasts 25 s, then reverts to Rookie
constexpr float kRookieHeightPx = 40;  // on-screen height of a Rookie model; forms grow by EvoStage::scale

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
     {{"Pepper Breath", AtkFx::Shot, kClipBreath, 6, 34, 10, 5, kOrange},
      {"Claw Attack", AtkFx::Melee, kClipStrike, 0, 22, 8, 1.0f, kWhite},
      {"Spitfire Rush", AtkFx::Spread, kClipBreath, 16, 80, 6, 4, kRed}}},
    {"Greymon", "M005GREY",
     {{"Nova Blast", AtkFx::Orb, kClipBreath, 8, 40, 12, 8, kOrange},
      {"Horn Strike", AtkFx::Dash, kClipStrike, 0, 30, 9, 1.0f, kWhite},
      {"Great Antler", AtkFx::Combo, kClipCombo3, 18, 90, 7, 1.0f, kGold}}},
    {"MetalGreymon", "M012MTGR",
     {{"Giga Destroyer", AtkFx::Spread, kClipBreath, 10, 46, 10, 6, kViolet},
      {"Trident Arm", AtkFx::Dash, kClipStrike, 0, 30, 10, 1.1f, kWhite},
      {"Mega Claw", AtkFx::Combo, kClipCombo3, 20, 90, 8, 1.1f, kGold}}},
    {"WarGreymon", "M213WGRE",
     {{"Gaia Force", AtkFx::Orb, kClipBreath, 14, 60, 18, 12, kOrange},
      {"Dramon Killer", AtkFx::Dash, kClipStrike, 0, 26, 11, 1.2f, kWhite},
      {"Brave Tornado", AtkFx::Combo, kClipCombo3, 22, 90, 9, 1.2f, kBlue}}},
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
    me_.evoTimer = evo > 0 ? kEvoDuration : 0;

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
    f.a.y = std::clamp(f.a.y, kArenaT, kArenaB);
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

void Battle::startAttack(Fighter& f, int slot, int duration) {
    f.act = slot;
    f.actTimer = duration;
    f.actTotal = duration;
    f.actHit = false;
    f.hitsDone = 0;
    f.a.move = Move::Action;
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
    const float foeR = 10.f * foe_.scale;

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
                fireShot(f, foe_, atk.dmg * pw, 3.8f, atk.size * std::sqrt(f.scale), 0, atk.color, 0);
                break;
            case AtkFx::Orb:
                fireShot(f, foe_, atk.dmg * pw, 2.8f, atk.size, 0, atk.color, 0);
                burst(f.cx(), f.cy() - f.bodyH() * 0.15f, atk.color, 12, 2.f);
                break;
            case AtkFx::Spread: {
                const int n = int(f.hitTicks.size());
                const float off = n > 1 ? (float(k) - (n - 1) / 2.f) * 0.2f : 0.f;
                fireShot(f, foe_, atk.dmg * pw, 3.4f, atk.size * std::sqrt(f.scale), off, atk.color, 0);
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
    me_.act = -1;
    me_.guarding = false;
    me_.stun = 0;
    me_.kbx = me_.kby = 0;
    shots_.clear();
    if (seq_.up) me_.dv = 0;
}

void Battle::updateEvoSeq() {
    EvoSeq& s = seq_;
    Fighter& f = me_;
    ++s.t;
    const float t = float(s.t);
    const float A = s.up ? 60.f : 24.f;   // charge ends
    const float B = s.up ? 110.f : 54.f;  // white silhouette ends -> flash
    const float C = s.up ? 150.f : 84.f;  // reveal ends
    const float toFoe = yawFor(foe_.a.x - f.a.x, foe_.a.y - f.a.y);

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
            setEvo(s.to);  // new model, new size, new attacks
            if (!s.up) me_.dv = 0;
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
        if (me_.model) setClip(me_, kClipIdle, 0.6f, true);
    }
}

void Battle::updatePlayer(const Input& in) {
    Fighter& f = me_;
    for (int& c : f.cd) c = std::max(0, c - 1);
    f.mp = std::min(f.maxMp, f.mp + 0.07f);
    if (f.evoTimer > 0 && --f.evoTimer == 0 && f.act < 0) {
        startEvoSeq(0);  // the form runs out: de-digivolve back to Rookie
        return;
    }
    if (f.stun > 0) { --f.stun; return; }

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
        if (!want[s] || f.cd[s] > 0) continue;
        if (f.mp < atk.mp) {
            popups_.push_back({f.cx(), f.a.y - f.bodyH() - 8.f, "No MP", 40, 0xffa0a0a0u});
            continue;
        }
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
                for (int i = 0; i < n; ++i)
                    fireShot(e, me_, 8.f * e.power, e.boss ? 2.9f : 2.5f, e.boss ? 6.f : 5.f, (i - (n - 1) / 2.f) * 0.3f,
                             e.boss ? rgba(200, 60, 255) : rgba(120, 80, 255), 1);
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
            if (foe_.hp <= 0 || me_.hp <= 0) {
                won_ = foe_.hp <= 0;
                phase_ = Phase::Ending;
                phaseTimer_ = 160;
                banner_ = won_ ? "VICTORY!" : "DEFEATED...";
                if (won_) burst(foe_.cx(), foe_.cy(), rgba(255, 255, 200), 60, 3.f);
                shots_.clear();
                me_.act = foe_.act = -1;
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
            if (frand(g_seed) < 0.5f) sparks_.push_back({s.x, s.y, -s.vx * 0.2f, -s.vy * 0.2f, 10, 10, s.r * 0.6f, s.color});
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

    if (f.evoTimer > 0 && !seq_.active) {  // digivolved aura, pulses faster as it runs out
        float p = 0.5f + 0.5f * std::sin(ticks_ * (f.evoTimer < 300 ? 0.4f : 0.12f));
        gfx::drawGlow(f.cx() - camX, f.cy() - camY, f.bodyH() * 0.75f, rgba(255, 200, 80, uint8_t(40 + 50 * p)));
    }
    if (f.model) {
        drawGroundShadow(f.a.x - camX, f.a.y - camY, f.heightPx * 0.32f, f.heightPx * 0.1f, 0.45f * f.deathFade);
        ModelView v;
        v.x = f.a.x - camX;
        v.y = f.a.y - camY;
        v.pxPerUnit = f.heightPx / std::max(1.f, f.model->model.height);
        v.yaw = f.yaw;
        v.tint = (tint & 0x00ffffffu) | (uint32_t(255 * f.deathFade) << 24);
        v.flash = f.flash > 0 ? f.flash / 8.f : 0.f;
        if (isMe && seq_.active) {
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
    if (backdrop_) gfx::drawTexture(*backdrop_, -bgX_ + sx, -bgY_ + sy);
    gfx::fillRect(0, 0, kScreenW, kScreenH, 0x99000010u);
    // arena floor glow
    for (int i = 0; i < 5; ++i) gfx::drawGlow(40 + i * 60.f + sx, 175 + sy, 70, rgba(90, 120, 255, 26));
    gfx::strokeRect(kArenaL - 8 + sx, kArenaT - 18 + sy, kArenaR - kArenaL + 16, kArenaB - kArenaT + 24, 0x40ffffffu);
    if (darken_ > 0) gfx::fillRect(0, 0, kScreenW, kScreenH, rgba(0, 0, 8, uint8_t(255 * darken_)));

    // digivolution light pillar behind the partner
    if (seq_.active) {
        const float B = seq_.up ? 110.f : 54.f;
        const float k = std::min(smooth(0, 30, float(seq_.t)), 1.f - smooth(B, B + 50, float(seq_.t)));
        const uint32_t c = seq_.up ? rgba(255, 210, 90, uint8_t(70 * k)) : rgba(150, 190, 255, uint8_t(60 * k));
        for (int i = 0; i < 12; ++i)
            gfx::drawGlow(me_.a.x - camX, me_.a.y - camY - i * 20.f, 22.f + me_.heightPx * 0.25f + 3.f * std::sin(ticks_ * 0.3f + i), c);
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
    if (me_.evoTimer > 0) bar(10, 43, 124, 1, float(me_.evoTimer) / kEvoDuration, 0xffffffffu);
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

    // attack bar (this form's own moves)
    static const char* keys[3] = {"J", "K", "L"};
    int cx = 10;
    for (int s = 0; s < 3; ++s) {
        const FormAttack& atk = form()->atk[s];
        bool can = me_.cd[s] == 0 && me_.mp >= atk.mp && !seq_.active;
        std::snprintf(buf, sizeof buf, "[%s] %s (%dMP)", keys[s], atk.name, int(atk.mp));
        gfx::drawText(cx, wh - 30, buf, can ? 0xffffffffu : 0xff707070u);
        cx += int(std::strlen(buf) + 3) * gfx::kCharW;
    }
    gfx::drawText(10, wh - 12, "[Space] Guard   [U] Digivolve   Move: WASD (Shift run)   Gamepad: X/B/Y attacks, LB guard, RB digivolve",
                  0xffb0e0ffu);

    for (const auto& p : popups_) {
        gfx::toWindow(p.x, p.y, x, y);
        gfx::drawTextCentered(x, y, p.text.c_str(), (p.color & 0x00ffffffu) | (uint32_t(std::min(255, p.life * 8)) << 24));
    }
    if (seq_.active) {
        const int B = seq_.up ? 110 : 54;
        gfx::toWindow(160, 64, x, y);
        if (seq_.t > 12 && seq_.t < B) {
            std::snprintf(buf, sizeof buf, seq_.up ? "%s digivolve to..." : "%s de-digivolve to...", formNameOf(seq_.from).c_str());
            gfx::drawTextCentered(x, y, buf, seq_.up ? 0xff60e0ffu : 0xffffd0a0u);
        } else if (seq_.t >= B) {
            std::snprintf(buf, sizeof buf, "%s!", formNameOf(seq_.to).c_str());
            gfx::drawTextCentered(x, y, buf, seq_.up ? 0xff40ffffu : 0xffffffffu);
            if (seq_.up) {
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
}

}  // namespace dw3
