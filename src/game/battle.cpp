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

// Attack tuning per slot: 0 = ranged, 1 = melee, 2 = spread special.
struct AttackDef { float mp; int cooldown; int duration; float dmg; };
constexpr AttackDef kAttacks[3] = {{6, 34, 20, 10}, {0, 22, 18, 7}, {16, 80, 26, 7}};
constexpr float kClawReach = 24;

float len(float x, float y) { return std::sqrt(x * x + y * y); }
float frand(long long& seed) {  // tiny deterministic LCG
    seed = seed * 6364136223846793005LL + 1442695040888963407LL;
    return float((seed >> 33) & 0xffffff) / float(0xffffff);
}
long long g_seed = 12345;

const char* kAgumonLine[kMaxEvo + 1] = {"Agumon", "Greymon", "MetalGreymon", "WarGreymon"};
const char* kAgumonAttacks[kMaxEvo + 1][3] = {
    {"Pepper Breath", "Claw Attack", "Spitfire Rush"},
    {"Nova Blast", "Horn Strike", "Mega Flame"},
    {"Giga Destroyer", "Trident Arm", "Mega Burst"},
    {"Gaia Force", "Dramon Killer", "Terra Destroyer"},
};
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

std::string Battle::formName() const {
    if (party_.partnerId == "P006AGUM") return kAgumonLine[me_.evo];
    return me_.evo == 0 ? me_.name : me_.name + " (" + evoStage(me_.evo).name + ")";
}

std::string Battle::attackName(int slot) const {
    if (party_.partnerId == "P006AGUM") return kAgumonAttacks[me_.evo][slot];
    static const char* generic[3] = {"Blast", "Strike", "Burst"};
    return generic[slot];
}

void Battle::setEvo(int evo) {
    const EvoStage& prev = evoStage(me_.evo);
    const EvoStage& next = evoStage(evo);
    me_.evo = evo;
    me_.scale = next.scale;
    me_.tint = next.tint;
    me_.power = next.power;
    me_.maxHp = baseMaxHp_ + next.hpBonus;
    if (next.hpBonus > prev.hpBonus) me_.hp += next.hpBonus - prev.hpBonus;  // growing heals the difference
    me_.hp = std::min(me_.hp, me_.maxHp);
    me_.maxMp = 60.f + 20.f * evo;
    me_.evoTimer = evo > 0 ? kEvoDuration : 0;
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
    float dx = to.cx() - from.cx(), dy = to.cy() - from.cy();
    float a = std::atan2(dy, dx) + angOff;
    float fx = from.cx() + std::cos(a) * 10.f * from.scale, fy = from.cy() + std::sin(a) * 6.f * from.scale;
    shots_.push_back({fx, fy, std::cos(a) * speed, std::sin(a) * speed, dmg, r, owner, 150, color});
}

void Battle::startAttack(Fighter& f, int slot, int duration) {
    f.act = slot;
    f.actTimer = duration;
    f.actHit = false;
    f.a.move = Move::Action;
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
    shake_ = std::max(shake_, foe_.boss ? 8 : 5);
    burst(t.cx(), t.cy(), rgba(255, 220, 120), 10, 2.2f);
    char buf[16];
    std::snprintf(buf, sizeof buf, "%d", int(dmg));
    popups_.push_back({t.cx(), t.cy() - 14.f * t.scale, buf, 50, &t == &me_ ? 0xff5050ffu : 0xffffffffu});
    // Digivolve gauge: fills from dealing and taking damage.
    if (&attacker == &me_) me_.dv = std::min(100.f, me_.dv + dmg * 1.1f);
    else me_.dv = std::min(100.f, me_.dv + dmg * 1.5f);
}

void Battle::resolvePlayerAttack() {
    Fighter& f = me_;
    const int slot = f.act;
    const float pw = f.power;
    if (slot == 0 && f.actTimer == kAttacks[0].duration - 6) {
        bool big = f.evo >= 1;
        fireShot(f, foe_, kAttacks[0].dmg * pw, big ? 4.2f : 3.6f, (big ? 7.f : 5.f) * std::sqrt(f.scale), 0, rgba(255, 140, 40), 0);
    } else if (slot == 1 && !f.actHit && f.actTimer <= kAttacks[1].duration - 6) {
        f.actHit = true;
        float reach = kClawReach * f.scale;
        float d = len(foe_.cx() - f.cx(), (foe_.cy() - f.cy()) * 1.4f);
        if (d < reach + 10.f * foe_.scale) {
            hit(foe_, f, kAttacks[1].dmg * pw, f.a.x, f.a.y);
        }
    } else if (slot == 2 && f.actTimer == kAttacks[2].duration - 8) {
        int n = f.evo >= 2 ? 5 : 3;
        for (int i = 0; i < n; ++i) {
            float off = (i - (n - 1) / 2.f) * 0.22f;
            fireShot(f, foe_, kAttacks[2].dmg * pw, 3.3f, 4.5f * std::sqrt(f.scale), off, rgba(255, 90, 30), 0);
        }
    }
}

void Battle::updatePlayer(const Input& in) {
    Fighter& f = me_;
    for (int& c : f.cd) c = std::max(0, c - 1);
    f.mp = std::min(f.maxMp, f.mp + 0.07f);
    if (f.evoTimer > 0 && --f.evoTimer == 0) {
        setEvo(0);
        burst(f.cx(), f.cy(), rgba(255, 255, 255), 24, 2.5f);
        popups_.push_back({f.cx(), f.cy() - 30, "De-digivolved", 70, 0xffffc080u});
    }
    if (f.stun > 0) { --f.stun; return; }

    // digivolve (stages grow in size)
    if (in.digivolveP && f.dv >= 100.f && f.evo < kMaxEvo && f.act < 0) {
        setEvo(f.evo + 1);
        f.dv = 0;
        screenFlash_ = 24;
        shake_ = 14;
        burst(f.cx(), f.cy(), rgba(255, 230, 120), 50, 3.5f);
        popups_.push_back({f.cx(), f.cy() - 40.f * f.scale, std::string("DIGIVOLVE! ") + formName(), 100, 0xff40ffffu});
        return;
    }

    if (f.act >= 0) {
        resolvePlayerAttack();
        if (--f.actTimer <= 0) { f.act = -1; f.a.move = Move::Idle; }
        return;
    }

    f.guarding = in.guard;
    if (f.guarding) { f.a.move = Move::Idle; return; }

    const bool want[3] = {in.atk1P, in.atk2P, in.atk3P};
    for (int s = 0; s < 3; ++s) {
        if (!want[s] || f.cd[s] > 0) continue;
        if (f.mp < kAttacks[s].mp) {
            popups_.push_back({f.cx(), f.cy() - 24.f * f.scale, "No MP", 40, 0xffa0a0a0u});
            continue;
        }
        f.mp -= kAttacks[s].mp;
        f.cd[s] = kAttacks[s].cooldown;
        f.a.dir = dirFromVector(foe_.a.x - f.a.x, foe_.a.y - f.a.y);  // auto-face target
        startAttack(f, s, kAttacks[s].duration);
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
                if (len(me_.cx() - e.cx(), (me_.cy() - e.cy()) * 1.4f) < 30.f * e.scale) hit(me_, e, 9.f * e.power, e.a.x, e.a.y);
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
        if (d < 20.f * e.scale && me_.invuln == 0) { hit(me_, e, 7.f * e.power, e.a.x, e.a.y); e.dashTimer = 0; }
        return;
    }

    if (--e.aiTimer <= 0) {
        e.a.dir = dirFromVector(dx, dy);
        float r = frand(g_seed);
        if (d < 36.f * e.scale) startAttack(e, 1, 26);
        else if (r < 0.6f) startAttack(e, 0, 30);
        else e.dashTimer = 40;
        e.aiTimer = e.boss ? 45 + int(frand(g_seed) * 35) : 70 + int(frand(g_seed) * 45);
        return;
    }

    // keep a fighting distance, strafing a little
    const float want = e.boss ? 75.f : 60.f;
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

void Battle::update(const Input& in) {
    ++ticks_;
    if (shake_ > 0) --shake_;
    if (screenFlash_ > 0) --screenFlash_;
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
        }
    } else {
        if (won_) foe_.deathFade = std::max(0.f, foe_.deathFade - 0.02f);
        else me_.deathFade = std::max(0.f, me_.deathFade - 0.02f);
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
        if (len(s.x - target.cx(), (s.y - target.cy()) * 1.3f) < s.r + 9.f * target.scale) {
            hit(target, source, s.dmg, s.x - s.vx * 4, s.y - s.vy * 4);
            s.life = 0;
        }
        if (s.x < -20 || s.x > kScreenW + 20 || s.y < 40 || s.y > kScreenH + 20) s.life = 0;
    }
    shots_.erase(std::remove_if(shots_.begin(), shots_.end(), [](const Shot& s) { return s.life <= 0; }), shots_.end());
    for (auto& p : sparks_) { p.x += p.vx; p.y += p.vy; p.vy += 0.05f; --p.life; }
    sparks_.erase(std::remove_if(sparks_.begin(), sparks_.end(), [](const Spark& p) { return p.life <= 0; }), sparks_.end());
    for (auto& p : popups_) { p.y -= 0.5f; --p.life; }
    popups_.erase(std::remove_if(popups_.begin(), popups_.end(), [](const Popup& p) { return p.life <= 0; }), popups_.end());

    me_.a.tick();
    foe_.a.tick();
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

    Fighter* order[2] = {&me_, &foe_};
    if (foe_.a.y < me_.a.y) std::swap(order[0], order[1]);
    for (Fighter* f : order) {
        if (f->deathFade <= 0.f) continue;
        uint32_t tint = f->tint;
        if (f == &foe_ && f->act >= 0 && f->actTimer > (f->act == 1 ? 8 : 12))  // telegraph wind-up in red
            tint = (ticks_ / 3) % 2 ? rgba(255, 120, 120) : 0xffffffffu;
        tint = (tint & 0x00ffffffu) | (uint32_t(255 * f->deathFade) << 24);
        if (f->evoTimer > 0) {  // digivolved aura, pulses faster as it runs out
            float p = 0.5f + 0.5f * std::sin(ticks_ * (f->evoTimer < 300 ? 0.4f : 0.12f));
            gfx::drawGlow(f->cx() + camX * -1, f->cy() - camY, 22.f * f->scale, rgba(255, 200, 80, uint8_t(50 + 60 * p)));
        }
        f->a.draw(camX, camY, f->scale, tint, f->flash > 0 ? f->flash / 8.f : 0.f);
        if (f->guarding) gfx::drawGlow(f->cx() - camX, f->cy() - camY, 20.f * f->scale, rgba(100, 180, 255, 110));
        if (f == &me_ && f->act == 1 && f->actTimer > 8) {  // claw slash streak
            float fx, fy;
            dirVector(f->a.dir, fx, fy);
            gfx::drawGlow(f->cx() + fx * 18 * f->scale - camX, f->cy() + fy * 8 - camY, 10 * f->scale, rgba(255, 255, 255, 140));
        }
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

    if (screenFlash_ > 0) gfx::fillRect(0, 0, kScreenW, kScreenH, rgba(255, 255, 255, uint8_t(10 * screenFlash_)));
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

    // attack bar
    static const char* keys[3] = {"J", "K", "L"};
    std::string line;
    int cx = 10;
    for (int s = 0; s < 3; ++s) {
        bool can = me_.cd[s] == 0 && me_.mp >= kAttacks[s].mp;
        std::snprintf(buf, sizeof buf, "[%s] %s (%dMP)", keys[s], attackName(s).c_str(), int(kAttacks[s].mp));
        gfx::drawText(cx, wh - 30, buf, can ? 0xffffffffu : 0xff707070u);
        cx += int(std::strlen(buf) + 3) * gfx::kCharW;
    }
    gfx::drawText(10, wh - 12, "[Space] Guard   [U] Digivolve   Move: WASD (Shift run)   Gamepad: X/B/Y attacks, LB guard, RB digivolve",
                  0xffb0e0ffu);

    for (const auto& p : popups_) {
        gfx::toWindow(p.x, p.y, x, y);
        gfx::drawTextCentered(x, y, p.text.c_str(), (p.color & 0x00ffffffu) | (uint32_t(std::min(255, p.life * 8)) << 24));
    }
    if (phase_ != Phase::Fight) {
        gfx::toWindow(160, 100, x, y);
        gfx::drawTextCentered(x, y, banner_.c_str(), won_ || phase_ == Phase::Intro ? 0xff40ffffu : 0xff4040ffu);
        if (phase_ == Phase::Ending && phaseTimer_ < 100) gfx::drawTextCentered(x, y + 24, "[Space] continue", 0xffffffffu);
    }
    (void)ww;
}

}  // namespace dw3
