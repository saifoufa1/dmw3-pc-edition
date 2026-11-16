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

// Sprite packs store 5 facings in the order S, SW, W, NW, N (the LEFT side).
// The right side (SE, E, NE) is the same frames mirrored.
static int animSlot(Move m, Dir d, bool& mirror) {
    int di = int(d);
    mirror = di >= int(SE) && di <= int(NE);
    int stored = mirror ? di : (di == S ? 0 : di == N ? 4 : 8 - di);  // SW->1, W->2, NW->3
    return int(m) * 5 + stored;
}

bool Actor::mirrored() const { return dir == SE || dir == E || dir == NE; }

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
        // Owner sizes: Agumon 0.8, Greymon 1.5, MetalGreymon 2.5, WarGreymon 3.3 (relative to Rookie = 1, 1.875, 3.125, 4.125).
        {"Rookie", 1.00f, 1.0f, 0, 0xffffffffu},
        {"Champion", 1.875f, 1.7f, 60, rgba(255, 220, 190)},
        {"Ultimate", 3.125f, 2.5f, 120, rgba(255, 200, 150)},
        {"Mega", 4.125f, 3.6f, 200, rgba(255, 235, 140)},
    };
    return stages[std::clamp(level, 0, kMaxEvo)];
}

}  // namespace dw3
