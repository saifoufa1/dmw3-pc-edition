#include "game/common.h"

#include <algorithm>
#include <cmath>

#include "core/bytes.h"

namespace fs = std::filesystem;

namespace dw3 {

Dir dirFromVector(float dx, float dy) {
    float a = std::atan2(-dy, dx);
    int oct = int(std::lround(a / (3.14159265f / 4))) & 7;  // 0=E,1=NE,2=N,3=NW,4=W,5=SW,6=S,7=SE
    static const Dir map[8] = {E, NE, N, NW, W, SW, S, SE};
    return map[oct];
}

void dirVector(Dir d, float& x, float& y) {
    static const float v[8][2] = {{0, 1}, {0.89f, 0.45f}, {1, 0}, {0.89f, -0.45f},
                                  {0, -1}, {-0.89f, -0.45f}, {-1, 0}, {-0.89f, 0.45f}};
    x = v[d][0];
    y = v[d][1];
}

SpriteSet::~SpriteSet() {
    for (auto& t : tex) gfx::destroy(t);
}

std::shared_ptr<SpriteSet> SpriteCache::get(const std::string& id, std::string* err) {
    auto it = cache_.find(id);
    if (it != cache_.end()) return it->second;
    Bytes b;
    if (!readFile(root_ / "DAT" / "FIELD" / "SPRT" / (id + ".BIN"), b)) {
        if (err) *err = "missing sprite " + id;
        return nullptr;
    }
    auto s = std::make_shared<SpriteSet>();
    s->name = id;
    std::string e;
    if (!loadSpritePack(b, s->pack, &e)) {
        if (err) *err = id + ": " + e;
        return nullptr;
    }
    for (auto& f : s->pack.frames) s->tex.push_back(gfx::upload(f));
    cache_[id] = s;
    return s;
}

std::vector<std::string> SpriteCache::listMovable() const {
    std::vector<std::string> out;
    std::error_code ec;
    for (auto& e : fs::directory_iterator(root_ / "DAT" / "FIELD" / "SPRT", ec)) {
        Bytes b;
        SpritePack sp;
        if (!readFile(e.path(), b) || !loadSpritePack(b, sp)) continue;
        if (sp.anims.size() < 15) continue;
        bool ok = std::all_of(sp.anims.begin(), sp.anims.begin() + 15, [](const Anim& a) { return !a.keys.empty(); });
        if (ok) out.push_back(e.path().stem().string());
    }
    std::sort(out.begin(), out.end());
    return out;
}

static int animSlot(Move m, Dir d, bool& mirror) {
    int di = int(d);
    mirror = di > int(N);
    if (mirror) di = 8 - di;  // NW->NE, W->E, SW->SE
    return int(m) * 5 + di;
}

bool Actor::mirrored() const { return int(dir) > int(N); }

void Actor::tick() {
    if (!sprite) return;
    bool mirror;
    int slot = animSlot(move, dir, mirror);
    const auto& anims = sprite->pack.anims;
    if (slot >= int(anims.size()) || anims[slot].keys.empty()) {
        slot = animSlot(Move::Idle, dir, mirror);  // missing group -> idle
        if (slot >= int(anims.size()) || anims[slot].keys.empty()) return;
    }
    if (slot != anim) {
        anim = slot;
        key = 0;
        timer = 0;
    }
    const Anim& a = anims[anim];
    if (++timer >= a.keys[key].duration) {
        timer = 0;
        if (++key >= int(a.keys.size())) key = a.loop ? 0 : int(a.keys.size()) - 1;
    }
}

bool Actor::actionDone() const {
    if (!sprite || anim < 0) return true;
    const Anim& a = sprite->pack.anims[anim];
    return !a.loop && key == int(a.keys.size()) - 1;
}

void Actor::draw(float camX, float camY, float scale, uint32_t tint, float flash) const {
    if (!sprite || anim < 0) return;
    const bool mirror = mirrored();
    const AnimKey& k = sprite->pack.anims[anim].keys[key];
    const Texture& t = sprite->tex[k.frame];
    const float sx = x - camX, sy = y - camY;
    const float dx = mirror ? sx - (k.ox + t.w) * scale : sx + k.ox * scale;
    const float dy = sy + k.oy * scale;
    gfx::fillRect(sx - 7 * scale, sy - 2 * scale, 14 * scale, 4 * scale, 0x50000000u);  // ground shadow
    gfx::drawSprite(t, dx, dy, mirror, scale, tint);
    if (flash > 0.f) gfx::drawSprite(t, dx, dy, mirror, scale, rgba(255, 255, 255, uint8_t(255 * std::min(1.f, flash))), true);
}

const EvoStage& evoStage(int level) {
    static const EvoStage stages[kMaxEvo + 1] = {
        {"Rookie", 1.00f, 1.0f, 0, 0xffffffffu},
        {"Champion", 1.45f, 1.7f, 60, rgba(255, 220, 190)},
        {"Ultimate", 1.90f, 2.5f, 120, rgba(255, 200, 150)},
        {"Mega", 2.40f, 3.6f, 200, rgba(255, 235, 140)},
    };
    return stages[std::clamp(level, 0, kMaxEvo)];
}

const Texture& swordTexture() {
    static Texture tex;
    if (!tex.id) {
        // 7x22 pixel sword, tip at the top, grip at the bottom.
        static const char* art[22] = {
            "...W...", "..WLW..", "..WLW..", "..WLD..", "..WLD..", "..WLD..", "..WLD..", "..WLD..",
            "..WLD..", "..WLD..", "..WLD..", "..WLD..", "..WLD..", "..WLD..", "..WLD..", "GGGGGGG",
            ".GYYYG.", "...B...", "...b...", "...B...", "...b...", "..GYG.."};
        Image img;
        img.resize(7, 22);
        for (int y = 0; y < 22; ++y)
            for (int x = 0; x < 7; ++x) {
                uint32_t c = 0;
                switch (art[y][x]) {
                    case 'W': c = rgba(250, 252, 255); break;   // edge highlight
                    case 'L': c = rgba(200, 214, 230); break;   // blade
                    case 'D': c = rgba(120, 136, 160); break;   // blade shade
                    case 'G': c = rgba(214, 160, 40); break;    // guard
                    case 'Y': c = rgba(255, 220, 90); break;    // gem/highlight
                    case 'B': c = rgba(110, 60, 30); break;     // grip
                    case 'b': c = rgba(70, 36, 18); break;
                }
                img.px[size_t(y) * 7 + x] = c;
            }
        tex = gfx::upload(img);
    }
    return tex;
}

bool swordBehindBody(const Actor& a) { return a.dir == N || a.dir == NE || a.dir == NW; }

void drawHeldSword(const Actor& a, float camX, float camY, float scale, float swing01) {
    if (!a.sprite || a.anim < 0) return;
    float fx, fy;
    dirVector(a.dir, fx, fy);
    const bool left = fx < -0.1f || (fx > -0.1f && fx < 0.1f && a.mirrored());
    const float side = left ? -1.f : 1.f;
    // Hand position relative to the feet.
    const float hx = a.x - camX + (fx * 7.f + side * (std::fabs(fx) < 0.1f ? 7.f : 0.f)) * scale;
    const float hy = a.y - camY + (-11.f + fy * 3.f) * scale;
    const float deg = 3.14159265f / 180.f;
    float ang;
    if (swing01 < 0.f) ang = 40.f * deg;  // resting, tip forward-up
    else {
        float t = std::clamp(swing01, 0.f, 1.f);
        t = 1.f - (1.f - t) * (1.f - t);  // ease-out
        ang = (-70.f + 200.f * t) * deg;  // big overhead arc
    }
    gfx::drawRotated(swordTexture(), hx, hy, 3.5f, 19.f, ang * side, scale, left, 0xffffffffu);
    if (swing01 >= 0.f && swing01 < 0.8f) {
        // motion streak at the blade tip
        float tipA = ang * side, len = 17.f * scale;
        gfx::drawGlow(hx + std::sin(tipA) * len, hy - std::cos(tipA) * len, 6.f * scale, rgba(200, 230, 255, 170));
    }
}

}  // namespace dw3
