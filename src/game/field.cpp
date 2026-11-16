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
        if (a.charId == 1) continue;  // the player's own record
        votes[a.slot][a.charId]++;
        if (!first.count(a.slot * 1000 + a.charId)) first[a.slot * 1000 + a.charId] = &a;
    }
    for (const auto& [slot, v] : votes) {
        const OverlayActor* a = nullptr;
        if (story_) {
            // With a story: the first variant (record order) whose show conditions hold on the original game flags
            // (e.g. the lobby before / after the partner download, gflags 6002 / 6004). None = not here now.
            for (const auto& c : overlay_.actors) {
                if (c.slot != slot || c.charId == 1) continue;
                bool show = true;
                for (const auto& f : c.show)
                    show = show && story_->gflag(Story::gkey(stageIds_[stageIndex_], slot, f.flag)) == f.value;
                if (show) { a = &c; break; }
            }
            if (!a) continue;
        } else {
            int best = -1, n = 0;
            for (const auto& [cid, c] : v)
                if (c > n) { n = c; best = cid; }
            a = first[slot * 1000 + best];
        }
        if (a->x == 0 && a->y == 0) continue;  // off-map record (spawned by an event)
        const size_t num = size_t(a->charId & 0xFF);
        if (num >= spriteByNumber_.size() || spriteByNumber_[num].empty()) continue;
        bool taken = false;  // a hand-placed NPC of the .stage file already stands here
        for (const auto& o : data_.npcs) taken = taken || (std::abs(o.x - a->x) < 8 && std::abs(o.y - a->y) < 8);
        if (story_ && story_->hasActorAt(stageIds_[stageIndex_], a->x, a->y)) taken = true;  // the story's own NPC
        if (taken) continue;
        NpcDef d;
        d.kind = NpcKind::Talk;
        d.sprite = spriteByNumber_[num];
        d.x = a->x;
        d.y = a->y;
        d.dir = a->dir & 7;
        d.name = d.sprite.substr(4);
        d.text = "(Original NPC position. Dialogue not decoded yet.)";
        d.original = true;
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
    if (haveFile && data_.originalNpcs) addOriginalNpcs();
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
    statusTimer_ = dev_ ? 150 : 0;  // players do not see stage ids
    if (story_) story_->onStage(id);
    return true;
}

bool Field::warpTo(const std::string& stage, int x, int y) {
    auto it = std::find(stageIds_.begin(), stageIds_.end(), stage);
    if (it == stageIds_.end()) return false;
    std::string err;
    return loadStage(size_t(it - stageIds_.begin()), err, x, y);
}

void Field::reloadPartner() {
    if (auto sp = sprites_.get(party_.partnerId)) partner_.sprite = sp;
    partner_.anim = -1;
    partner_.tick();
    // the rest of the team walks behind the active partner (in team order)
    std::vector<Actor> old = std::move(followers_);
    followers_.clear();
    for (int i = 0; i < int(party_.team.size()); ++i) {
        if (i == party_.active) continue;
        Actor a;
        a.sprite = sprites_.get(party_.team[size_t(i)].sprite);
        if (!a.sprite) continue;
        const size_t k = followers_.size();
        a.x = k < old.size() ? old[k].x : partner_.x;
        a.y = k < old.size() ? old[k].y : partner_.y;
        a.dir = k < old.size() ? old[k].dir : partner_.dir;
        a.tick();
        followers_.push_back(a);
    }
}

// Partner download (story `beam`, reference shots 44-45): the team appears one by one around the player, each in a
// blue data beam on the floor. Not decoded: the original beam effect (ours = a simple glowing column + flash).
namespace {
constexpr int kBeamEach = 50;                                         // ticks per Digimon
constexpr float kBeamSpot[3][2] = {{-34, -6}, {-8, -24}, {-48, 12}};  // around the player (map pixels)
}  // namespace

void Field::updateBeam() {
    if (beamT_ < 0) return;
    const int n = 1 + int(followers_.size());
    if (beamT_ == 0) {  // stand the team on its spots
        for (int i = 0; i < n; ++i) {
            Actor& a = i == 0 ? partner_ : followers_[size_t(i - 1)];
            a.x = player_.x + kBeamSpot[i % 3][0];
            a.y = player_.y + kBeamSpot[i % 3][1];
            if (!walkable(a.x, a.y)) { a.x = player_.x; a.y = player_.y; }
            a.dir = SE;
            a.move = Move::Idle;
        }
        trail_.clear();
    }
    ++beamT_;
    if (beamT_ >= kBeamEach * n + 20) {
        beamT_ = -1;
        party_.partnersOut = true;
        if (story_) story_->beamDone();
    }
}

void Field::saveStory() {
    if (!story_) return;
    const bool ok = story_->saveGame(Story::defaultSavePath(), stageId(), int(player_.x), int(player_.y));
    status_ = ok ? "Game saved" : "SAVE FAILED: " + Story::defaultSavePath().string();
    statusTimer_ = 120;
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
        for (const auto& a : overlay_.actors)  // same spot as an original event actor -> its talk table
            if (a.charId != 1 && !a.talk.empty() && std::abs(a.x - n.def.x) <= 4 && std::abs(a.y - n.def.y) <= 4) {
                n.origSlot = a.slot;
                break;
            }
        npcs_.push_back(std::move(n));
    }
    if (story_) {
        storyRev_ = story_->revision();
        for (const StoryActor* sa : story_->actorsOn(stageIds_[stageIndex_])) {
            NpcRt n;
            n.def = sa->def;
            n.storyId = sa->id;
            n.actor.sprite = sprites_.get(n.def.sprite);
            n.actor.x = float(n.def.x);
            n.actor.y = float(n.def.y);
            n.actor.dir = Dir(n.def.dir & 7);
            n.actor.tick();
            npcs_.push_back(std::move(n));
        }
    }
}

void Field::startTalk(size_t i) {
    NpcRt& n = npcs_[i];
    n.actor.dir = dirFromVector(player_.x - n.actor.x, player_.y - n.actor.y);
    player_.dir = dirFromVector(n.actor.x - player_.x, n.actor.y - player_.y);
    if (story_) {
        if (!n.storyId.empty()) {
            if (!story_->onTalk(n.storyId)) story_->sayRef(n.def.text, n.def.name, "...");
            return;
        }
        if (n.origSlot >= 0 && story_->sayOriginal(stageId(), overlay_, n.origSlot, n.def.name)) return;
        story_->sayRef(n.def.text, n.def.name, "...");
        return;
    }
    talkNpc_ = int(i);
    talkPage_ = 0;
    talkPages_.clear();
    std::string t = n.def.text;
    size_t p;
    while ((p = t.find('|')) != std::string::npos) { talkPages_.push_back(t.substr(0, p)); t.erase(0, p + 1); }
    talkPages_.push_back(t);
}

// Applies what the story asked for: fights, warps, partner changes, saves, NPC visibility.
void Field::handleStoryRequests() {
    if (!story_) return;
    if (story_->takePartnerChanged()) reloadPartner();
    if (auto w = story_->takeWarp()) warpTo(w->stage, w->x, w->y);
    if (auto p = story_->takePlace()) placeParty(float(p->x), float(p->y));
    if (story_->takeBeam()) { reloadPartner(); beamT_ = 0; }
    if (story_->takeSaveRequest()) saveStory();
    if (story_->revision() != storyRev_ && !battle_) buildNpcs();
    if (auto id = story_->takeFight()) {
        battleNpc_ = -1;
        for (size_t i = 0; i < npcs_.size(); ++i)
            if (npcs_[i].storyId == *id) battleNpc_ = int(i);
        const StoryActor* sa = story_->actor(*id);
        const NpcDef* d = battleNpc_ >= 0 ? &npcs_[size_t(battleNpc_)].def : sa ? &sa->def : nullptr;
        if (d) {
            battle_ = BattleRequest{d->sprite, d->name, d->level, d->kind == NpcKind::Boss};
            if (sa && !sa->enemySprite.empty()) { battle_->enemySprite = sa->enemySprite; battle_->enemyName = sa->enemyName; }
            storyBattle_ = *id;
        } else {
            story_->onBattleEnd(*id, true);  // unknown actor: do not soft-lock the script
        }
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
    for (size_t i = 0; i < followers_.size(); ++i) {
        followers_[i].x = x + 12 + 8.f * float(i + 1);
        followers_[i].y = y + 6 + 5.f * float(i + 1);
        if (!walkable(followers_[i].x, followers_[i].y)) { followers_[i].x = partner_.x; followers_[i].y = partner_.y; }
        followers_[i].move = Move::Idle;
        followers_[i].tick();
    }
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
    if (story_ && !storyBattle_.empty()) {
        const std::string id = storyBattle_;
        storyBattle_.clear();
        if (!won) {  // back to the start point; the story decides what happens next
            float sx, sy;
            if (data_.spawnX >= 0) { sx = float(data_.spawnX); sy = float(data_.spawnY); } else findAutoSpawn(sx, sy);
            placeParty(sx, sy);
        }
        story_->onBattleEnd(id, won);
        battleNpc_ = -1;
        touchCooldown_ = 90;
        return;
    }
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
    // the active partner walks kTrailGap samples behind the player, each other team member one gap further
    auto follow = [&](Actor& a, size_t gap) {
        if (trail_.size() <= gap) { a.move = Move::Idle; return; }
        auto [tx, ty] = trail_[trail_.size() - 1 - gap];
        float dx = tx - a.x, dy = ty - a.y;
        float d = std::sqrt(dx * dx + dy * dy);
        if (d < 1.0f) { a.move = Move::Idle; return; }
        float step = std::min(d, std::max(speed, d * 0.25f));
        a.x += dx / d * step;
        a.y += dy / d * step;
        a.dir = dirFromVector(dx, dy);
        a.move = step > kWalkSpeed * 1.4f ? Move::Run : Move::Walk;
    };
    follow(partner_, kTrailGap);
    for (size_t i = 0; i < followers_.size(); ++i) follow(followers_[i], kTrailGap * (i + 2));
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
    if (in.editorP && dev_) {
        editor_ = !editor_;
        status_ = editor_ ? "EDITOR ON (F2 to leave, Ctrl+S to save)" : "Editor off";
        statusTimer_ = 120;
    }

    // ---- stage transition fade ----
    if (fade_ > 0) {
        if (fadingOut_) {
            if (++fade_ >= kFadeTicks) {
                auto it = std::find(stageIds_.begin(), stageIds_.end(), pendingExit_.target);
                if (it != stageIds_.end() && loadStage(size_t(it - stageIds_.begin()), err, pendingExit_.arriveX, pendingExit_.arriveY) &&
                    !dev_)
                    saveStory();  // PC flow: autosave on every area change (Continue resumes here)
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

    handleStoryRequests();
    if (story_ && story_->busy() && !editor_) {
        story_->update(in);
        handleStoryRequests();
        updateBeam();
        player_.move = partner_.move = Move::Idle;
        player_.tick();
        partner_.tick();
        for (auto& f : followers_) { f.move = Move::Idle; f.tick(); }
        for (auto& n : npcs_) n.actor.tick();
        updateCamera();
        return;
    }

    if (party_.team.size() > 1 && party_.partnersOut && in.partnerP && talkNpc_ < 0 && !editor_) {  // Tab: next team member
        party_.setActive((party_.active + 1) % int(party_.team.size()));
        reloadPartner();
        status_ = party_.partnerName + " is your partner now";
        statusTimer_ = 120;
    }
    if (dev_ && (in.nextStageP || in.prevStageP)) {
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
    const float prevX = player_.x, prevY = player_.y;
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
    for (auto& f : followers_) f.tick();
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
            if (best >= 0) startTalk(size_t(best));
        }
        // ---- touch a fighter -> battle ----
        if (touchCooldown_ == 0 && !battle_) {
            for (size_t i = 0; i < npcs_.size(); ++i) {
                const auto& n = npcs_[i];
                if (n.defeated || n.def.kind == NpcKind::Talk) continue;
                float range = n.def.kind == NpcKind::Boss ? kTouchRange + 6 : kTouchRange;
                if (dist(player_.x, player_.y, n.actor.x, n.actor.y) < range ||
                    dist(partner_.x, partner_.y, n.actor.x, n.actor.y) < range) {
                    touchCooldown_ = 60;
                    if (!n.storyId.empty() && story_) {
                        if (story_->onTouch(n.storyId)) break;  // the event decides (usually 'fight')
                        storyBattle_ = n.storyId;
                    }
                    battle_ = BattleRequest{n.def.sprite, n.def.name, n.def.level, n.def.kind == NpcKind::Boss};
                    if (const StoryActor* sa = story_ && !n.storyId.empty() ? story_->actor(n.storyId) : nullptr)
                        if (!sa->enemySprite.empty()) { battle_->enemySprite = sa->enemySprite; battle_->enemyName = sa->enemyName; }
                    battleNpc_ = int(i);
                    break;
                }
            }
        }
        // ---- exits ----
        if (story_) story_->onEnter(stageId(), player_.x, player_.y);
        for (const auto& e : data_.exits) {
            if (player_.x >= e.x && player_.x < e.x + e.w && player_.y >= e.y && player_.y < e.y + e.h) {
                std::string msg;
                if (story_ && story_->exitBlocked(stageId(), e.target, msg)) {
                    player_.x = prevX;  // step back out of the exit zone
                    player_.y = prevY;
                    story_->sayRef(msg, "", msg);  // a message ref (dmg200:8:1) or our own text
                    break;
                }
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
    std::vector<Item> order{{player_.y, 0, 0}};
    if (partnersVisible()) {
        order.push_back({partner_.y, 1, 0});
        for (size_t i = 0; i < followers_.size(); ++i) order.push_back({followers_[i].y, 3, i});
    }
    for (size_t i = 0; i < npcs_.size(); ++i)
        if (!npcs_[i].defeated) order.push_back({npcs_[i].actor.y, 2, i});
    std::sort(order.begin(), order.end(), [](const Item& a, const Item& b) { return a.y < b.y; });
    for (const auto& o : order) {
        if (o.kind == 0) player_.draw(camX_, camY_);
        else if (o.kind == 1 || o.kind == 3) {
            const Actor& a = o.kind == 1 ? partner_ : followers_[o.idx];
            const int i = o.kind == 1 ? 0 : int(o.idx) + 1;  // download order
            if (beamT_ < 0) { a.draw(camX_, camY_); continue; }
            const int t = beamT_ - i * kBeamEach;  // this Digimon's beam time
            if (t < 0) continue;
            const float sx = a.x - camX_, sy = a.y - camY_;
            const float in = std::min(1.f, float(t) / 14.f), out = t > 34 ? std::max(0.f, 1.f - float(t - 34) / 16.f) : 1.f;
            const float col = in * out;
            if (col > 0.f) {  // the data beam: a translucent blue column with bright scan lines
                gfx::fillRect(sx - 9, sy - 44, 18, 46, rgba(80, 170, 255, uint8_t(110 * col)));
                for (int k = 0; k < 6; ++k) {
                    const float ly = sy - 44 + float((k * 8 + t * 2) % 46);
                    gfx::fillRect(sx - 9, ly, 18, 1, rgba(200, 240, 255, uint8_t(200 * col)));
                }
                gfx::drawGlow(sx, sy - 2, 16, rgba(120, 200, 255, uint8_t(120 * col)));
            }
            if (t >= 18) a.draw(camX_, camY_, 1.f, 0xffffffffu, t < 30 ? 1.f - float(t - 18) / 12.f : 0.f);
        } else {
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

    if (story_ && !editor_) story_->render();
    if (talkNpc_ >= 0) {
        gfx::fillRect(12, 168, 296, 62, 0xd8301808u);
        gfx::strokeRect(12, 168, 296, 62, 0xffc0e0ffu);
    }
    if (fade_ > 0) gfx::fillRect(0, 0, kScreenW, kScreenH, rgba(0, 0, 0, uint8_t(255 * fade_ / kFadeTicks)));
}

void Field::renderHud(int ww, int wh) {
    char line[256];
    const bool cine = story_ && story_->busy() && !editor_;  // story scene: keep the screen clean
    // NPC labels
    for (const auto& n : npcs_) {
        if (cine) break;
        if (n.defeated && !editor_) continue;
        int wx, wy;
        float top = n.def.kind == NpcKind::Boss ? 52.f : 38.f;
        gfx::toWindow(n.actor.x - camX_, n.actor.y - camY_ - top, wx, wy);
        if (n.def.kind == NpcKind::Boss) gfx::drawTextCentered(wx, wy, ("BOSS " + n.def.name).c_str(), 0xff4040ffu);
        else if (n.def.kind == NpcKind::Fight) gfx::drawTextCentered(wx, wy, ("! " + n.def.name).c_str(), 0xff40a0ffu);
        else if (editor_ || dist(player_.x, player_.y, n.actor.x, n.actor.y) < kTalkRange * 1.6f)
            gfx::drawTextCentered(wx, wy, editor_ ? n.def.name.c_str() : "[U] Talk", 0xffffffffu);
    }
    if (editor_)
        for (const auto& e : data_.exits) {
            int wx, wy;
            gfx::toWindow(e.x - camX_ + e.w / 2.f, e.y - camY_ + e.h / 2.f, wx, wy);
            gfx::drawTextCentered(wx, wy, ("-> " + e.target).c_str(), 0xff40ffffu);
        }

    if (hud_ && !cine && !dev_) {
        gfx::drawText(10, wh - 12,
                      party_.team.size() > 1 && party_.partnersOut ? "Move: WASD/Arrows  Run: Shift  Talk: U or Enter  Partner: Tab  Menu: Esc  HUD: F1"
                                             : "Move: WASD/Arrows  Run: Shift  Talk: U or Enter  Menu: Esc  HUD: F1",
                      0xffb0e0ffu);
    }
    if (hud_ && !cine && dev_) {
        std::snprintf(line, sizeof line, "DMW3 PC Edition v%s | Stage %s (%zu/%zu)  pos %d,%d  %s%s", DMW3_VERSION,
                      stageIds_[stageIndex_].c_str(), stageIndex_ + 1, stageIds_.size(), int(player_.x), int(player_.y),
                      "", editor_ ? "  [EDITOR]" : "");
        gfx::drawText(10, 20, line);
        if (!editor_)
            gfx::drawText(10, wh - 12,
                          "Move: WASD/Arrows  Run: Shift  Talk: U or Enter  Stage: PgUp/PgDn  Editor: F2  HUD: F1",
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
    if (statusTimer_ > 0 && !cine) gfx::drawTextCentered(ww / 2, 46 + (editor_ ? 110 : 0), status_.c_str(), 0xff60ffffu);

    if (story_ && !editor_) story_->renderHud(ww, wh);
    if (talkNpc_ >= 0) {
        int x, y;
        gfx::toWindow(20, 176, x, y);
        gfx::drawText(x, y + 12, npcs_[talkNpc_].def.name.c_str(), 0xff40d0ffu);
        gfx::drawText(x, y + 12 + 22, talkPages_[talkPage_].c_str(), 0xffffffffu);
        gfx::toWindow(250, 222, x, y);
        gfx::drawText(x, y, talkPage_ + 1 < talkPages_.size() ? "[U] >" : "[U] close", 0xffb0e0ffu);
    }
}

}  // namespace dw3
