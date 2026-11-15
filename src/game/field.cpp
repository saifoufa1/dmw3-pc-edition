#include "game/field.h"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdio>
#include <map>

#include "core/bytes.h"

namespace fs = std::filesystem;

namespace dw3 {

namespace {
constexpr float kWalkSpeed = 1.25f;  // map px per tick (60 Hz)
constexpr float kRunSpeed = 2.5f;
constexpr size_t kTrailGap = 22;  // how far behind the partner walks (in trail samples)
constexpr int kFadeTicks = 20;
constexpr float kTalkRange = 28.f, kTouchRange = 15.f, kItemRange = 14.f;

float dist(float ax, float ay, float bx, float by) { return std::sqrt((ax - bx) * (ax - bx) + (ay - by) * (ay - by)); }

// Win32 virtual-key codes used by the editor.
enum : int { VK_CTRL = 0x11, VK_LBRACKET = 0xDB, VK_RBRACKET = 0xDD, VK_COMMA = 0xBC, VK_PERIOD = 0xBE };
}  // namespace

Field::~Field() {
    gfx::destroy(stageTex_);
    clearPieceTex();
}

void Field::clearPieceTex() {
    for (auto& p : pieceTex_) gfx::destroy(p.tex);
    pieceTex_.clear();
}

const Field::PieceTex* Field::pieceTex(int piece) {
    if (piece < 0 || size_t(piece) >= fstage_.pieces.size()) return nullptr;
    if (pieceTex_.size() != fstage_.pieces.size()) pieceTex_.resize(fstage_.pieces.size());
    PieceTex& pt = pieceTex_[size_t(piece)];
    if (!pt.tried) {
        pt.tried = true;
        Image img;
        if (fstage_.renderPiece(piece, img, pt.ox, pt.oy)) pt.tex = gfx::upload(img);
    }
    return pt.tex.id ? &pt : nullptr;
}

// Original per-stage data: DAT/FIELD/STAGE/S###MASK.BIN + S###TMPK.BIN (walk map, trigger map, scene
// pieces) and PRO/WSTAG###.PRO (player start, scene objects, event actors). Static opaque pieces are
// baked into the background (they fill the "holes" of the Z_STAGE pack: doors, machines, signs);
// animated and glow pieces are drawn every frame.
void Field::loadOriginalStage(const std::string& id) {
    fstage_ = {};
    overlay_ = {};
    anims_.clear();
    clearPieceTex();
    Bytes mask, tmpk, pro;
    readFile(root_ / "DAT" / "FIELD" / "STAGE" / (id + "MASK.BIN"), mask);
    readFile(root_ / "DAT" / "FIELD" / "STAGE" / (id + "TMPK.BIN"), tmpk);
    loadFieldStage(mask, tmpk, stage_.gridW, stage_.gridH, fstage_);
    if (readFile(root_ / "PRO" / ("WSTAG" + id.substr(1) + ".PRO"), pro)) loadStageOverlay(pro, overlay_);

    Image& bg = stage_.image;
    for (const auto& o : overlay_.objects) {
        SceneAnim a;
        a.x = o.x;
        a.y = o.y;
        a.delay = std::max(2, o.delay * 2);
        if (o.mode == 1) {  // cycle frame ids loopFrom..loopTo
            for (int f = o.loopFrom; f <= o.loopTo && f < 256; ++f) {
                int pi = fstage_.pieceForFrame(f);
                if (pi >= 0) a.pieces.push_back(pi);
            }
        } else if (o.mode == 2) {  // base frame + sub-counter loopFrom..loopTo (hypothesis, see FORMATS.md)
            for (int k = o.loopFrom; k <= o.loopTo && o.frame + k < 256; ++k) {
                int pi = fstage_.pieceForFrame(o.frame + k);
                if (pi >= 0) a.pieces.push_back(pi);
            }
        } else {
            int pi = fstage_.pieceForFrame(o.frame);
            if (pi >= 0) a.pieces.push_back(pi);
        }
        if (a.pieces.empty()) continue;
        a.additive = fstage_.pieces[size_t(a.pieces[0])].flag != -1;
        if (a.pieces.size() == 1 && !a.additive) {  // static opaque piece: bake into the background
            Image img;
            int ox, oy;
            if (!fstage_.renderPiece(a.pieces[0], img, ox, oy)) continue;
            for (int y = 0; y < img.h; ++y)
                for (int x = 0; x < img.w; ++x) {
                    const uint32_t c = img.px[size_t(y) * img.w + x];
                    const int dx = o.x + ox + x, dy = o.y + oy + y;
                    if ((c >> 24) && bg.inside(dx, dy)) bg.px[size_t(dy) * bg.w + dx] = c;
                }
            continue;
        }
        anims_.push_back(std::move(a));
    }
}

void Field::applyOriginalWalls() {
    data_.cell = 4;
    data_.resize(stage_.image.w, stage_.image.h);
    for (int cy = 0; cy < data_.rows; ++cy)
        for (int cx = 0; cx < data_.cols; ++cx)
            data_.setWallCell(cx, cy, fstage_.walkAt(cx * 4 + 2, cy * 4 + 2) == 0);
    data_.originalWalls = true;
}

void Field::addOriginalNpcs() {
    // Several records share an actor slot (one per story state): show the most common variant.
    std::map<int, std::map<int, int>> votes;
    std::map<int, const OverlayActor*> first;
    for (const auto& a : overlay_.actors) {
        votes[a.slot][a.charId]++;
        if (!first.count(a.slot * 1000 + a.charId)) first[a.slot * 1000 + a.charId] = &a;
    }
    for (const auto& [slot, v] : votes) {
        int best = -1, n = 0;
        for (const auto& [cid, c] : v)
            if (c > n) { n = c; best = cid; }
        const OverlayActor* a = first[slot * 1000 + best];
        const size_t num = size_t(a->charId & 0xFF);
        if (num >= spriteByNumber_.size() || spriteByNumber_[num].empty()) continue;
        NpcDef d;
        d.kind = NpcKind::Talk;
        d.sprite = spriteByNumber_[num];
        d.x = a->x;
        d.y = a->y;
        d.dir = a->dir & 7;
        d.name = d.sprite.substr(4);
        d.text = "(Original NPC position. Dialogue not decoded yet.)";
        data_.npcs.push_back(d);
    }
}

bool Field::init(const fs::path& dataRoot, const fs::path& stageDir, const std::string& startStage, std::string& err) {
    root_ = dataRoot;
    stageDir_ = stageDir;
    sprites_.setRoot(root_);
    for (auto& e : fs::directory_iterator(root_ / "DAT" / "Z_STAGE")) {
        std::string n = e.path().filename().string();  // S200PACK.BIN
        if (n.size() == 12 && n[0] == 'S' && n.substr(4) == "PACK.BIN") stageIds_.push_back(n.substr(0, 4));
    }
    std::sort(stageIds_.begin(), stageIds_.end());
    if (stageIds_.empty()) { err = "no stages in DAT/Z_STAGE"; return false; }
    npcSprites_ = sprites_.listMovable();
    // P### number -> sprite id (first file with that number, e.g. 57 -> P057AOAB).
    spriteByNumber_.assign(256, std::string());
    std::vector<std::string> sprt;
    std::error_code ec;
    for (auto& e : fs::directory_iterator(root_ / "DAT" / "FIELD" / "SPRT", ec)) {
        std::string n = e.path().stem().string();
        if (n.size() == 8 && n[0] == 'P' && std::isdigit((unsigned char)n[1]) && std::isdigit((unsigned char)n[2]) &&
            std::isdigit((unsigned char)n[3]))
            sprt.push_back(n);
    }
    std::sort(sprt.begin(), sprt.end());
    for (const auto& n : sprt) {
        const int num = std::stoi(n.substr(1, 3));
        if (num < 256 && spriteByNumber_[size_t(num)].empty()) spriteByNumber_[size_t(num)] = n;
    }

    player_.sprite = sprites_.get("P001PLAY", &err);
    if (!player_.sprite) return false;
    partner_.sprite = sprites_.get(party_.partnerId, &err);
    if (!partner_.sprite) return false;

    auto it = std::find(stageIds_.begin(), stageIds_.end(), startStage);
    return loadStage(it != stageIds_.end() ? size_t(it - stageIds_.begin()) : 0, err);
}

std::string Field::stageFile() const { return (stageDir_ / (stageIds_[stageIndex_] + ".stage")).string(); }
std::string Field::npcKey(size_t i) const { return stageIds_[stageIndex_] + "#" + std::to_string(i); }

bool Field::loadStage(size_t index, std::string& err, int arriveX, int arriveY) {
    Bytes b;
    StageMap m;
    const std::string& id = stageIds_[index];
    if (!readFile(root_ / "DAT" / "Z_STAGE" / (id + "PACK.BIN"), b) || !loadStagePack(b, m, &err)) {
        err = "stage " + id + ": " + err;
        return false;
    }
    gfx::destroy(stageTex_);
    stage_ = std::move(m);
    stageIndex_ = index;
    loadOriginalStage(id);  // bakes the original scene pieces into stage_.image
    stageTex_ = gfx::upload(stage_.image);

    std::string e;
    data_ = {};
    bool haveFile = fs::exists(stageDir_ / (id + ".stage")) && loadStageData(stageDir_ / (id + ".stage"), data_, e);
    if (fstage_.hasWalk && (!haveFile || data_.originalWalls || !data_.hasWallGrid)) {
        applyOriginalWalls();  // the game's own walk map
    } else {
        const int wantCols = (stage_.image.w + data_.cell - 1) / data_.cell, wantRows = (stage_.image.h + data_.cell - 1) / data_.cell;
        if (!haveFile || data_.cols != wantCols || data_.rows != wantRows) {
            auto keep = data_;
            data_.resize(stage_.image.w, stage_.image.h);
            data_.autoWalls(stage_.image);  // fallback: background holes are walls
            if (haveFile) { data_.exits = keep.exits; data_.npcs = keep.npcs; data_.items = keep.items; data_.spawnX = keep.spawnX; data_.spawnY = keep.spawnY; }
        }
    }
    if (!haveFile) {
        addOriginalNpcs();
        if (overlay_.spawnX >= 0 && fstage_.walkAt(overlay_.spawnX, overlay_.spawnY)) { data_.spawnX = overlay_.spawnX; data_.spawnY = overlay_.spawnY; }
    }
    buildNpcs();

    float sx, sy;
    if (arriveX >= 0) { sx = float(arriveX); sy = float(arriveY); }
    else if (data_.spawnX >= 0) { sx = float(data_.spawnX); sy = float(data_.spawnY); }
    else findAutoSpawn(sx, sy);
    placeParty(sx, sy);
    talkNpc_ = -1;
    status_ = "Stage " + id + (haveFile ? "" : "  (no stage data yet: auto walls - press F2 to edit)");
    statusTimer_ = 150;
    return true;
}

void Field::buildNpcs() {
    npcs_.clear();
    for (size_t i = 0; i < data_.npcs.size(); ++i) {
        NpcRt n;
        n.def = data_.npcs[i];
        n.actor.sprite = sprites_.get(n.def.sprite);
        n.actor.x = float(n.def.x);
        n.actor.y = float(n.def.y);
        n.actor.dir = Dir(n.def.dir & 7);
        n.actor.tick();
        if (n.actor.sprite && n.actor.anim < 0) {  // not a full idle/walk set (e.g. P109MTHI): show its first anim
            const auto& anims = n.actor.sprite->pack.anims;
            for (size_t a = 0; a < anims.size(); ++a)
                if (!anims[a].keys.empty()) { n.actor.anim = int(a); break; }
        }
        n.defeated = defeated_.count(stageIds_[stageIndex_] + "#" + std::to_string(i)) > 0;
        npcs_.push_back(std::move(n));
    }
}

bool Field::walkable(float x, float y) const {
    if (editor_) return stage_.image.inside(int(x), int(y));
    const int ix = int(x), iy = int(y);
    if (data_.isWall(ix, iy) || data_.isWall(ix - 4, iy) || data_.isWall(ix + 4, iy) || data_.isWall(ix, iy - 2))
        return false;
    for (const auto& n : npcs_)  // talk NPCs are solid
        if (!n.defeated && n.def.kind == NpcKind::Talk && dist(x, y, n.actor.x, n.actor.y) < 9.f) return false;
    return true;
}

void Field::findAutoSpawn(float& outX, float& outY) const {
    const int cx = stage_.image.w / 2, cy = stage_.image.h / 2;
    outX = float(cx);
    outY = float(cy);
    for (int r = 0; r < std::max(stage_.image.w, stage_.image.h); r += 4)
        for (int a = 0; a < 32; ++a) {
            float px = cx + r * std::cos(a * 0.19635f), py = cy + r * std::sin(a * 0.19635f);
            if (walkable(px, py) && walkable(px + 12, py + 6)) { outX = px; outY = py; return; }
        }
}

void Field::placeParty(float x, float y) {
    player_.x = x; player_.y = y; player_.move = Move::Idle;
    partner_.x = x + 12; partner_.y = y + 6; partner_.move = Move::Idle;
    if (!walkable(partner_.x, partner_.y)) { partner_.x = x; partner_.y = y; }
    trail_.clear();
    player_.tick();
    partner_.tick();
    updateCamera();
}

std::optional<BattleRequest> Field::takeBattleRequest() {
    auto r = battle_;
    battle_.reset();
    return r;
}

void Field::endBattle(bool won) {
    if (battleNpc_ >= 0 && battleNpc_ < int(npcs_.size())) {
        if (won) {
            npcs_[battleNpc_].defeated = true;
            defeated_.insert(npcKey(size_t(battleNpc_)));
            status_ = "Victory! " + npcs_[battleNpc_].def.name + " was defeated.";
        } else {
            status_ = "You were defeated... back to the start point.";
            float sx, sy;
            if (data_.spawnX >= 0) { sx = float(data_.spawnX); sy = float(data_.spawnY); } else findAutoSpawn(sx, sy);
            placeParty(sx, sy);
        }
        statusTimer_ = 180;
    }
    battleNpc_ = -1;
    touchCooldown_ = 60;
}

void Field::updatePartner(float speed) {
    if (trail_.size() <= kTrailGap) { partner_.move = Move::Idle; return; }
    auto [tx, ty] = trail_[trail_.size() - 1 - kTrailGap];
    float dx = tx - partner_.x, dy = ty - partner_.y;
    float d = std::sqrt(dx * dx + dy * dy);
    if (d < 1.0f) { partner_.move = Move::Idle; return; }
    float step = std::min(d, std::max(speed, d * 0.25f));
    partner_.x += dx / d * step;
    partner_.y += dy / d * step;
    partner_.dir = dirFromVector(dx, dy);
    partner_.move = step > kWalkSpeed * 1.4f ? Move::Run : Move::Walk;
}

void Field::updateCamera() {
    auto clampCam = [](float c, int view, int size) {
        if (size <= view) return -float(view - size) / 2;
        return std::clamp(c, 0.f, float(size - view));
    };
    camX_ = clampCam(player_.x - kScreenW / 2.f, kScreenW, stage_.image.w);
    camY_ = clampCam(player_.y - kScreenH / 2.f - 16, kScreenH, stage_.image.h);
}

void Field::update(const Input& in) {
    ++ticks_;
    if (statusTimer_ > 0) --statusTimer_;
    if (touchCooldown_ > 0) --touchCooldown_;
    std::string err;
    if (in.hudP) hud_ = !hud_;
    if (in.editorP) {
        editor_ = !editor_;
        status_ = editor_ ? "EDITOR ON (F2 to leave, Ctrl+S to save)" : "Editor off";
        statusTimer_ = 120;
    }

    // ---- stage transition fade ----
    if (fade_ > 0) {
        if (fadingOut_) {
            if (++fade_ >= kFadeTicks) {
                auto it = std::find(stageIds_.begin(), stageIds_.end(), pendingExit_.target);
                if (it != stageIds_.end() && loadStage(size_t(it - stageIds_.begin()), err, pendingExit_.arriveX, pendingExit_.arriveY)) {}
                fadingOut_ = false;
                fade_ = kFadeTicks;
            }
        } else {
            --fade_;
        }
        player_.move = partner_.move = Move::Idle;
        player_.tick();
        partner_.tick();
        return;
    }

    if (in.nextStageP || in.prevStageP) {
        size_t n = stageIds_.size();
        size_t idx = in.nextStageP ? (stageIndex_ + 1) % n : (stageIndex_ + n - 1) % n;
        for (size_t tries = 0; tries < n && !loadStage(idx, err); ++tries)
            idx = in.nextStageP ? (idx + 1) % n : (idx + n - 1) % n;
    }
    // ---- dialogue ----
    if (talkNpc_ >= 0) {
        if (in.confirmP) {
            if (++talkPage_ >= talkPages_.size()) talkNpc_ = -1;
        }
        player_.move = partner_.move = Move::Idle;
        player_.tick();
        partner_.tick();
        for (auto& n : npcs_) n.actor.tick();
        return;
    }

    if (editor_) updateEditor(in);

    // ---- player movement ----
    float ix = float(in.right) - float(in.left), iy = float(in.down) - float(in.up);
    const float speed = (editor_ ? 2.f : 1.f) * (in.run ? kRunSpeed : kWalkSpeed);
    if (ix != 0 || iy != 0) {
        player_.dir = dirFromVector(ix, iy);
        float vx = ix, vy = (ix != 0 && iy != 0) ? iy * 0.5f : iy;  // 2:1 isometric diagonals
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
    for (auto& a : anims_)
        if (a.pieces.size() > 1 && ++a.timer >= a.delay) { a.timer = 0; a.cur = (a.cur + 1) % a.pieces.size(); }
    player_.tick();
    partner_.tick();
    for (auto& n : npcs_) n.actor.tick();

    if (!editor_) {
        // ---- talk ----
        if (in.confirmP) {
            int best = -1;
            float bestD = kTalkRange;
            for (size_t i = 0; i < npcs_.size(); ++i) {
                const auto& n = npcs_[i];
                if (n.defeated || n.def.kind != NpcKind::Talk) continue;
                float d = dist(player_.x, player_.y, n.actor.x, n.actor.y);
                if (d < bestD) { bestD = d; best = int(i); }
            }
            if (best >= 0) {
                talkNpc_ = best;
                talkPage_ = 0;
                talkPages_.clear();
                std::string t = npcs_[best].def.text;
                size_t p;
                while ((p = t.find('|')) != std::string::npos) { talkPages_.push_back(t.substr(0, p)); t.erase(0, p + 1); }
                talkPages_.push_back(t);
                npcs_[best].actor.dir = dirFromVector(player_.x - npcs_[best].actor.x, player_.y - npcs_[best].actor.y);
                player_.dir = dirFromVector(npcs_[best].actor.x - player_.x, npcs_[best].actor.y - player_.y);
            }
        }
        // ---- touch a fighter -> battle ----
        if (touchCooldown_ == 0 && !battle_) {
            for (size_t i = 0; i < npcs_.size(); ++i) {
                const auto& n = npcs_[i];
                if (n.defeated || n.def.kind == NpcKind::Talk) continue;
                float range = n.def.kind == NpcKind::Boss ? kTouchRange + 6 : kTouchRange;
                if (dist(player_.x, player_.y, n.actor.x, n.actor.y) < range ||
                    dist(partner_.x, partner_.y, n.actor.x, n.actor.y) < range) {
                    battle_ = BattleRequest{n.def.sprite, n.def.name, n.def.level, n.def.kind == NpcKind::Boss};
                    battleNpc_ = int(i);
                    break;
                }
            }
        }
        // ---- exits ----
        for (const auto& e : data_.exits) {
            if (player_.x >= e.x && player_.x < e.x + e.w && player_.y >= e.y && player_.y < e.y + e.h) {
                pendingExit_ = e;
                fadingOut_ = true;
                fade_ = 1;
                break;
            }
        }
    }
    updateCamera();
}

// ------------------------------------------------------------------ editor

void Field::updateEditor(const Input& in) {
    mapMx_ = in.mx + camX_;
    mapMy_ = in.my + camY_;
    const bool ctrl = in.key[VK_CTRL];
    if (in.keyP['1']) tool_ = Tool::Walls;
    if (in.keyP['2']) tool_ = Tool::Exits;
    if (in.keyP['3']) tool_ = Tool::Npcs;
    if (in.keyP['4']) tool_ = Tool::Spawn;
    if (in.keyP[VK_LBRACKET]) brush_ = std::max(0, brush_ - 1);
    if (in.keyP[VK_RBRACKET]) brush_ = std::min(8, brush_ + 1);
    const bool prev = in.keyP[VK_COMMA], next = in.keyP[VK_PERIOD];
    if (tool_ == Tool::Exits && (prev || next)) {
        size_t n = stageIds_.size();
        editTarget_ = next ? (editTarget_ + 1) % n : (editTarget_ + n - 1) % n;
    }
    if (tool_ == Tool::Npcs && (prev || next) && !npcSprites_.empty()) {
        size_t n = npcSprites_.size();
        npcSprite_ = next ? (npcSprite_ + 1) % n : (npcSprite_ + n - 1) % n;
    }
    if (tool_ == Tool::Npcs && in.keyP['T'])
        npcKind_ = npcKind_ == NpcKind::Talk ? NpcKind::Fight : npcKind_ == NpcKind::Fight ? NpcKind::Boss : NpcKind::Talk;
    if (ctrl && in.keyP['G']) {
        if (fstage_.hasWalk) { applyOriginalWalls(); status_ = "Walls reset from the game's walk map"; }
        else { data_.autoWalls(stage_.image); status_ = "Walls reset from background"; }
        statusTimer_ = 120;
    }
    if (ctrl && in.keyP['X']) { std::fill(data_.wall.begin(), data_.wall.end(), uint8_t(0)); data_.originalWalls = false; status_ = "All walls cleared"; statusTimer_ = 120; }
    if (ctrl && in.keyP['S']) {
        bool ok = saveStageData(stageFile(), data_);
        status_ = ok ? "Saved " + stageFile() : "SAVE FAILED: " + stageFile();
        statusTimer_ = 180;
    }
    const bool onScreen = in.mx >= 0 && in.my >= 0 && in.mx < kScreenW && in.my < kScreenH;
    if (!onScreen) return;
    const int mx = int(mapMx_), my = int(mapMy_);

    switch (tool_) {
        case Tool::Walls:
            if (in.lmb || in.rmb) {
                data_.originalWalls = false;  // hand-painted walls are saved as a grid
                int cx = mx / data_.cell, cy = my / data_.cell;
                for (int y = cy - brush_; y <= cy + brush_; ++y)
                    for (int x = cx - brush_; x <= cx + brush_; ++x) data_.setWallCell(x, y, in.lmb);
            }
            break;
        case Tool::Exits:
            if (in.lmbP) { dragging_ = true; dragX_ = mapMx_; dragY_ = mapMy_; }
            if (dragging_ && in.lmbR) {
                dragging_ = false;
                int x0 = int(std::min(dragX_, mapMx_)), y0 = int(std::min(dragY_, mapMy_));
                int w = int(std::fabs(mapMx_ - dragX_)), h = int(std::fabs(mapMy_ - dragY_));
                if (w >= 4 && h >= 4) {
                    data_.exits.push_back({x0, y0, w, h, stageIds_[editTarget_], -1, -1});
                    status_ = "Exit -> " + stageIds_[editTarget_] + " (arrives at its spawn; edit arriveX/Y in the file)";
                    statusTimer_ = 150;
                }
            }
            if (in.rmbP)
                for (size_t i = 0; i < data_.exits.size(); ++i) {
                    const auto& e = data_.exits[i];
                    if (mx >= e.x && mx < e.x + e.w && my >= e.y && my < e.y + e.h) { data_.exits.erase(data_.exits.begin() + i); break; }
                }
            break;
        case Tool::Npcs:
            if (in.lmbP && !npcSprites_.empty()) {
                NpcDef n;
                n.kind = npcKind_;
                n.sprite = npcSprites_[npcSprite_];
                n.x = mx; n.y = my; n.dir = int(S);
                n.name = n.sprite.substr(4);
                n.level = npcKind_ == NpcKind::Boss ? 8 : 3;
                n.text = "Hello, tamer!|(Edit my lines in " + stageIds_[stageIndex_] + ".stage)";
                data_.npcs.push_back(n);
                buildNpcs();
            }
            if (in.rmbP) {
                for (size_t i = 0; i < data_.npcs.size(); ++i)
                    if (dist(float(mx), float(my), float(data_.npcs[i].x), float(data_.npcs[i].y)) < 16.f) {
                        data_.npcs.erase(data_.npcs.begin() + i);
                        buildNpcs();
                        break;
                    }
            }
            break;
        case Tool::Spawn:
            if (in.lmbP) { data_.spawnX = mx; data_.spawnY = my; }
            break;
    }
}

void Field::renderEditor() {
    // walls (only visible cells)
    const int c = data_.cell;
    const int x0 = std::max(0, int(camX_) / c), y0 = std::max(0, int(camY_) / c);
    const int x1 = std::min(data_.cols, int(camX_ + kScreenW) / c + 1), y1 = std::min(data_.rows, int(camY_ + kScreenH) / c + 1);
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x)
            if (data_.wall[size_t(y) * data_.cols + x]) gfx::fillRect(x * c - camX_, y * c - camY_, float(c), float(c), 0x600000ffu);
    for (const auto& e : data_.exits) {
        gfx::fillRect(e.x - camX_, e.y - camY_, float(e.w), float(e.h), 0x50ff8000u);
        gfx::strokeRect(e.x - camX_, e.y - camY_, float(e.w), float(e.h), 0xffffc040u);
    }
    if (dragging_) gfx::strokeRect(std::min(dragX_, mapMx_) - camX_, std::min(dragY_, mapMy_) - camY_, std::fabs(mapMx_ - dragX_), std::fabs(mapMy_ - dragY_), 0xffffffffu);
    if (data_.spawnX >= 0) {
        gfx::fillRect(data_.spawnX - camX_ - 6, data_.spawnY - camY_, 13, 1, 0xff00ff00u);
        gfx::fillRect(data_.spawnX - camX_, data_.spawnY - camY_ - 6, 1, 13, 0xff00ff00u);
    }
    if (tool_ == Tool::Walls && mapMx_ >= 0) {
        int cx = int(mapMx_) / c, cy = int(mapMy_) / c;
        gfx::strokeRect((cx - brush_) * c - camX_, (cy - brush_) * c - camY_, float((2 * brush_ + 1) * c), float((2 * brush_ + 1) * c), 0xffffffffu);
    }
}

// ------------------------------------------------------------------ render

void Field::render() {
    gfx::drawTexture(stageTex_, -camX_, -camY_);
    for (const auto& a : anims_) {  // original animated scene pieces and glows
        const PieceTex* pt = pieceTex(a.pieces[a.cur]);
        if (!pt) continue;
        const float x = float(a.x + pt->ox) - camX_, y = float(a.y + pt->oy) - camY_;
        if (x > kScreenW || y > kScreenH || x + pt->tex.w < 0 || y + pt->tex.h < 0) continue;
        if (a.additive) gfx::drawSprite(pt->tex, x, y, false, 1.f, 0xffffffffu, true);
        else gfx::drawTexture(pt->tex, x, y);
    }

    // painter's order by feet Y
    struct Item { float y; int kind; size_t idx; };
    std::vector<Item> order{{player_.y, 0, 0}, {partner_.y, 1, 0}};
    for (size_t i = 0; i < npcs_.size(); ++i)
        if (!npcs_[i].defeated) order.push_back({npcs_[i].actor.y, 2, i});
    std::sort(order.begin(), order.end(), [](const Item& a, const Item& b) { return a.y < b.y; });
    for (const auto& o : order) {
        if (o.kind == 0) player_.draw(camX_, camY_);
        else if (o.kind == 1) partner_.draw(camX_, camY_);
        else {
            const auto& n = npcs_[o.idx];
            float scale = n.def.kind == NpcKind::Boss ? 1.35f : 1.f;
            n.actor.draw(camX_, camY_, scale);
            if (n.def.kind != NpcKind::Talk) {  // pulsing aura marks fighters
                float p = 0.5f + 0.5f * std::sin(ticks_ * 0.1f);
                gfx::drawGlow(n.actor.x - camX_, n.actor.y - camY_ - 2, 14.f * scale,
                              n.def.kind == NpcKind::Boss ? rgba(255, 60, 60, uint8_t(60 + 60 * p)) : rgba(255, 140, 60, uint8_t(40 + 40 * p)));
            }
        }
    }
    if (editor_) renderEditor();

    if (talkNpc_ >= 0) {
        gfx::fillRect(12, 168, 296, 62, 0xd8301808u);
        gfx::strokeRect(12, 168, 296, 62, 0xffc0e0ffu);
    }
    if (fade_ > 0) gfx::fillRect(0, 0, kScreenW, kScreenH, rgba(0, 0, 0, uint8_t(255 * fade_ / kFadeTicks)));
}

void Field::renderHud(int ww, int wh) {
    char line[256];
    // NPC labels
    for (const auto& n : npcs_) {
        if (n.defeated && !editor_) continue;
        int wx, wy;
        float top = n.def.kind == NpcKind::Boss ? 52.f : 38.f;
        gfx::toWindow(n.actor.x - camX_, n.actor.y - camY_ - top, wx, wy);
        if (n.def.kind == NpcKind::Boss) gfx::drawTextCentered(wx, wy, ("BOSS " + n.def.name).c_str(), 0xff4040ffu);
        else if (n.def.kind == NpcKind::Fight) gfx::drawTextCentered(wx, wy, ("! " + n.def.name).c_str(), 0xff40a0ffu);
        else if (editor_ || dist(player_.x, player_.y, n.actor.x, n.actor.y) < kTalkRange * 1.6f)
            gfx::drawTextCentered(wx, wy, editor_ ? n.def.name.c_str() : "[Space] Talk", 0xffffffffu);
    }
    if (editor_)
        for (const auto& e : data_.exits) {
            int wx, wy;
            gfx::toWindow(e.x - camX_ + e.w / 2.f, e.y - camY_ + e.h / 2.f, wx, wy);
            gfx::drawTextCentered(wx, wy, ("-> " + e.target).c_str(), 0xff40ffffu);
        }

    if (hud_) {
        std::snprintf(line, sizeof line, "DMW3 PC Edition v%s | Stage %s (%zu/%zu)  pos %d,%d  %s%s", DMW3_VERSION,
                      stageIds_[stageIndex_].c_str(), stageIndex_ + 1, stageIds_.size(), int(player_.x), int(player_.y),
                      "", editor_ ? "  [EDITOR]" : "");
        gfx::drawText(10, 20, line);
        if (!editor_)
            gfx::drawText(10, wh - 12,
                          "Move: WASD/Arrows  Run: Shift  Talk: Space  Stage: PgUp/PgDn  Editor: F2  HUD: F1",
                          0xffb0e0ffu);
    }
    if (editor_) {
        static const char* names[] = {"1 WALLS: LMB paint, RMB erase, [ ] brush", "2 EXITS: drag LMB, RMB delete, , . target",
                                      "3 NPCS: LMB place, RMB delete, , . sprite, T kind", "4 SPAWN: LMB set player start"};
        int y = 44;
        for (int i = 0; i < 4; ++i, y += 17) gfx::drawText(10, y, names[i], int(tool_) == i ? 0xff40ffffu : 0xffa0a0a0u);
        if (tool_ == Tool::Exits) std::snprintf(line, sizeof line, "exit target: %s", stageIds_[editTarget_].c_str());
        else if (tool_ == Tool::Npcs) std::snprintf(line, sizeof line, "npc: %s  kind: %s", npcSprites_.empty() ? "-" : npcSprites_[npcSprite_].c_str(), npcKindName(npcKind_));
        else if (tool_ == Tool::Walls) std::snprintf(line, sizeof line, "brush %d   Ctrl+G auto walls  Ctrl+X clear", brush_ * 2 + 1);
        else line[0] = 0;
        gfx::drawText(10, y + 4, line, 0xffffffffu);
        gfx::drawText(10, wh - 12, "EDITOR: Ctrl+S save  F2 exit  (walls ignored while editing)", 0xff40ffffu);
    }
    if (statusTimer_ > 0) gfx::drawTextCentered(ww / 2, 46 + (editor_ ? 110 : 0), status_.c_str(), 0xff60ffffu);

    if (talkNpc_ >= 0) {
        int x, y;
        gfx::toWindow(20, 176, x, y);
        gfx::drawText(x, y + 12, npcs_[talkNpc_].def.name.c_str(), 0xff40d0ffu);
        gfx::drawText(x, y + 12 + 22, talkPages_[talkPage_].c_str(), 0xffffffffu);
        gfx::toWindow(250, 222, x, y);
        gfx::drawText(x, y, talkPage_ + 1 < talkPages_.size() ? "[Space] >" : "[Space] close", 0xffb0e0ffu);
    }
}

}  // namespace dw3
