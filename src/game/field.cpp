#include "game/field.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "core/bytes.h"

namespace fs = std::filesystem;

namespace dw3 {

namespace {
constexpr float kWalkSpeed = 1.25f;  // map px per tick (60 Hz)
constexpr float kRunSpeed = 2.5f;
constexpr size_t kTrailGap = 22;     // how far behind the partner walks (in trail samples)

// Sprite anim slot for (move, dir): 5 stored directions per move type.
int animSlot(Move m, Dir d, bool& mirror) {
    int di = int(d);
    mirror = di > int(N);
    if (mirror) di = 8 - di;  // NW->NE, W->E, SW->SE
    return int(m) * 5 + di;
}

Dir dirFromVector(float dx, float dy) {
    // Screen-space angle, 0 = east, counter-clockwise with y pointing down.
    float a = std::atan2(-dy, dx);
    int oct = int(std::lround(a / (3.14159265f / 4))) & 7;  // 0=E,1=NE,2=N,3=NW,4=W,5=SW,6=S,7=SE
    static const Dir map[8] = {E, NE, N, NW, W, SW, S, SE};
    return map[oct];
}
}  // namespace

SpriteSet::~SpriteSet() {
    for (auto& t : tex) gfx::destroy(t);
}

void Actor::tick() {
    if (!sprite) return;
    bool mirror;
    int slot = animSlot(move, dir, mirror);
    if (slot >= int(sprite->pack.anims.size()) || sprite->pack.anims[slot].keys.empty()) return;
    if (slot != anim) {
        anim = slot;
        key = 0;
        timer = 0;
    }
    const Anim& a = sprite->pack.anims[anim];
    if (++timer >= a.keys[key].duration) {
        timer = 0;
        if (++key >= int(a.keys.size())) key = a.loop ? 0 : int(a.keys.size()) - 1;
    }
}

void Actor::draw(float camX, float camY) const {
    if (!sprite || anim < 0) return;
    bool mirror;
    animSlot(move, dir, mirror);
    const AnimKey& k = sprite->pack.anims[anim].keys[key];
    const Texture& t = sprite->tex[k.frame];
    float sx = x - camX, sy = y - camY;
    float dx = mirror ? sx - k.ox - t.w : sx + k.ox;
    gfx::fillRect(sx - 7, sy - 2, 14, 4, 0x50000000u);  // soft ground shadow
    gfx::drawTexture(t, dx, sy + k.oy, mirror);
}

Field::~Field() { gfx::destroy(stageTex_); }

std::shared_ptr<SpriteSet> Field::loadSprite(const std::string& id, std::string& err) {
    Bytes b;
    if (!readFile(root_ / "DAT" / "FIELD" / "SPRT" / (id + ".BIN"), b)) {
        err = "missing sprite " + id;
        return nullptr;
    }
    auto s = std::make_shared<SpriteSet>();
    s->name = id;
    if (!loadSpritePack(b, s->pack, &err)) { err = id + ": " + err; return nullptr; }
    if (s->pack.anims.size() < 15) { err = id + ": no movement animations"; return nullptr; }
    for (auto& f : s->pack.frames) s->tex.push_back(gfx::upload(f));
    return s;
}

bool Field::init(const fs::path& dataRoot, const std::string& startStage, std::string& err) {
    root_ = dataRoot;
    for (auto& e : fs::directory_iterator(root_ / "DAT" / "Z_STAGE")) {
        std::string n = e.path().filename().string();  // S200PACK.BIN
        if (n.size() == 12 && n[0] == 'S' && n.substr(4) == "PACK.BIN") stageIds_.push_back(n.substr(0, 4));
    }
    std::sort(stageIds_.begin(), stageIds_.end());
    if (stageIds_.empty()) { err = "no stages in DAT/Z_STAGE"; return false; }

    // Partner roster: the playable rookies' field sprites.
    for (const char* id : {"P006AGUM", "P003PKEN", "P004KOGU", "P005KOET", "P007VMON", "P009RENA", "P008GIRU"})
        if (fs::exists(root_ / "DAT" / "FIELD" / "SPRT" / (std::string(id) + ".BIN"))) partnerIds_.push_back(id);

    player_.sprite = loadSprite("P001PLAY", err);
    if (!player_.sprite) return false;
    if (!partnerIds_.empty()) partner_.sprite = loadSprite(partnerIds_[0], err);

    auto it = std::find(stageIds_.begin(), stageIds_.end(), startStage);
    return loadStage(it != stageIds_.end() ? size_t(it - stageIds_.begin()) : 0, err);
}

bool Field::loadStage(size_t index, std::string& err) {
    Bytes b;
    StageMap m;
    const std::string& id = stageIds_[index];
    if (!readFile(root_ / "DAT" / "Z_STAGE" / (id + "PACK.BIN"), b) || !loadStagePack(b, m, &err)) {
        err = "stage " + id + ": " + err;
        return false;
    }
    gfx::destroy(stageTex_);
    stage_ = std::move(m);
    stageTex_ = gfx::upload(stage_.image);
    stageIndex_ = index;
    spawn();
    status_ = "Stage " + id;
    statusTimer_ = 120;
    return true;
}

bool Field::walkable(float x, float y) const {
    // Placeholder collision until the MASK/attribute format is decoded:
    // the feet (and a little to each side) must stand on drawn background.
    const int ix = int(x), iy = int(y);
    return stage_.image.opaque(ix, iy) && stage_.image.opaque(ix - 5, iy) && stage_.image.opaque(ix + 5, iy) &&
           stage_.image.opaque(ix, iy - 3);
}

void Field::spawn() {
    const int cx = stage_.image.w / 2, cy = stage_.image.h / 2;
    float bx = float(cx), by = float(cy);
    // Spiral outwards from the centre for the first walkable spot.
    for (int r = 0; r < std::max(stage_.image.w, stage_.image.h); r += 4) {
        bool found = false;
        for (int a = 0; a < 32 && !found; ++a) {
            float px = cx + r * std::cos(a * 0.19635f), py = cy + r * std::sin(a * 0.19635f);
            if (walkable(px, py) && walkable(px + 12, py + 6)) { bx = px; by = py; found = true; }
        }
        if (found) break;
    }
    player_.x = bx; player_.y = by; player_.dir = S; player_.move = Move::Idle;
    partner_.x = bx + 12; partner_.y = by + 6; partner_.dir = S; partner_.move = Move::Idle;
    trail_.clear();
}

void Field::updatePartner(float speed) {
    if (!partner_.sprite) return;
    if (trail_.size() <= kTrailGap) {
        partner_.move = Move::Idle;
        return;
    }
    auto [tx, ty] = trail_[trail_.size() - 1 - kTrailGap];
    float dx = tx - partner_.x, dy = ty - partner_.y;
    float dist = std::sqrt(dx * dx + dy * dy);
    if (dist < 1.0f) {
        partner_.move = Move::Idle;
        return;
    }
    float step = std::min(dist, std::max(speed, dist * 0.25f));
    partner_.x += dx / dist * step;
    partner_.y += dy / dist * step;
    partner_.dir = dirFromVector(dx, dy);
    partner_.move = step > kWalkSpeed * 1.4f ? Move::Run : Move::Walk;
}

void Field::update(const Input& in) {
    ++ticks_;
    if (statusTimer_ > 0) --statusTimer_;
    std::string err;
    if (in.toggleHud) hud_ = !hud_;
    if (in.nextStage || in.prevStage) {
        size_t n = stageIds_.size();
        size_t idx = in.nextStage ? (stageIndex_ + 1) % n : (stageIndex_ + n - 1) % n;
        // Skip packs that fail to load rather than getting stuck.
        for (size_t tries = 0; tries < n && !loadStage(idx, err); ++tries)
            idx = in.nextStage ? (idx + 1) % n : (idx + n - 1) % n;
    }
    if (in.nextPartner && partnerIds_.size() > 1) {
        partnerIndex_ = (partnerIndex_ + 1) % partnerIds_.size();
        if (auto s = loadSprite(partnerIds_[partnerIndex_], err)) {
            partner_.sprite = s;
            partner_.anim = -1;
            status_ = "Partner " + s->name.substr(4);
            statusTimer_ = 120;
        }
    }

    float ix = float(in.right) - float(in.left), iy = float(in.down) - float(in.up);
    const float speed = in.run ? kRunSpeed : kWalkSpeed;
    if (ix != 0 || iy != 0) {
        player_.dir = dirFromVector(ix, iy);
        // Isometric feel: diagonals travel 2:1 like the pre-rendered floor grid.
        float vx = ix, vy = (ix != 0 && iy != 0) ? iy * 0.5f : iy;
        float len = std::sqrt(vx * vx + vy * vy);
        vx = vx / len * speed;
        vy = vy / len * speed;
        float ox = player_.x, oy = player_.y;
        if (walkable(player_.x + vx, player_.y + vy)) { player_.x += vx; player_.y += vy; }
        else if (vx != 0 && walkable(player_.x + vx, player_.y)) player_.x += vx;  // slide along walls
        else if (vy != 0 && walkable(player_.x, player_.y + vy)) player_.y += vy;
        bool moved = (player_.x != ox || player_.y != oy);
        player_.move = moved ? (in.run ? Move::Run : Move::Walk) : Move::Idle;
        if (moved) {
            trail_.emplace_back(player_.x, player_.y);
            if (trail_.size() > 256) trail_.pop_front();
        }
    } else {
        player_.move = Move::Idle;
    }
    updatePartner(speed);
    player_.tick();
    partner_.tick();

    // Camera follows the player, clamped to the map (centred if the map is smaller than the screen).
    auto clampCam = [](float c, int view, int size) {
        if (size <= view) return -float(view - size) / 2;
        return std::clamp(c, 0.f, float(size - view));
    };
    camX_ = clampCam(player_.x - kScreenW / 2.f, kScreenW, stage_.image.w);
    camY_ = clampCam(player_.y - kScreenH / 2.f - 16, kScreenH, stage_.image.h);
}

void Field::render() {
    gfx::drawTexture(stageTex_, -camX_, -camY_);
    // Painter's order by feet Y.
    const Actor* order[2] = {&player_, &partner_};
    if (partner_.y < player_.y) std::swap(order[0], order[1]);
    for (const Actor* a : order) a->draw(camX_, camY_);
}

void Field::renderHud(int ww, int wh) {
    char line[160];
    if (hud_) {
        std::snprintf(line, sizeof line, "DMW3 PC Edition v%s demo | Stage %s (%zu/%zu)  %dx%d  pos %d,%d",
                      DMW3_VERSION, stageIds_[stageIndex_].c_str(), stageIndex_ + 1, stageIds_.size(),
                      stage_.image.w, stage_.image.h, int(player_.x), int(player_.y));
        gfx::drawText(10, 20, line);
        gfx::drawText(10, wh - 12,
                      "Move: Arrows/WASD/D-pad  Run: Shift/A  Stage: PgUp/PgDn or LB/RB  Partner: Tab/Y  HUD: F1  Quit: Esc",
                      0xffb0e0ffu);
    }
    if (statusTimer_ > 0) gfx::drawText(ww / 2 - int(status_.size()) * 4, 46, status_.c_str(), 0xff60ffffu);
}

}  // namespace dw3
