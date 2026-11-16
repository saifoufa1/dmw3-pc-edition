#include "game/field.h"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>

#include "core/bytes.h"
#include "formats/ui_sheet.h"

namespace fs = std::filesystem;

namespace dw3 {

namespace {
constexpr float kWalkSpeed = 1.25f;  // map px per tick (60 Hz)
constexpr float kRunSpeed = 2.5f;
constexpr size_t kTrailGap = 22;  // how far behind the partner walks (in trail samples)
constexpr int kDoorOpenTicks = 14, kWipeTicks = 24, kCardTicks = 100, kCardOutTicks = 18;
constexpr int kPortTicks = 70;
constexpr int kMarkFirst = 35, kMarkFrames = 4, kMarkTicks = 6;  // FIELDCOM door marker pieces, ticks per frame
constexpr float kTalkRange = 36.f, kTouchRange = 15.f, kItemRange = 14.f;

float dist(float ax, float ay, float bx, float by) { return std::sqrt((ax - bx) * (ax - bx) + (ay - by) * (ay - by)); }

// Win32 virtual-key codes used by the editor.
enum : int { VK_CTRL = 0x11, VK_LBRACKET = 0xDB, VK_RBRACKET = 0xDD, VK_COMMA = 0xBC, VK_PERIOD = 0xBE };
}  // namespace

Field::~Field() {
    for (auto& m : doorMark_) gfx::destroy(m.tex);
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
        // flags 0x400 + a sort line: foreground piece (counter fronts, railings) drawn in depth order with the
        // actors, so NPCs behind a counter are hidden by it. link 0x64..: the door piece of an area link.
        if ((o.flags & 0x400) && o.depthY > 0) a.depthY = o.depthY;
        if (o.link >= 0x64) a.link = o.link;
        if (a.pieces.size() == 1 && !a.additive && !a.depthY && !a.link) {  // static opaque piece: bake into the background
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
    loadDoorMark();
    loadAreaNames();
    loadPorts();
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
    // Original NPCs and doors: the tables extracted from the disc (dw3check --extract-npcs data).
    origNpcs_.clear();
    doors_.clear();
    origDoors_ = false;
    const fs::path base = stageDir_.parent_path();
    if (loadNpcTable((base / "npcs" / (id + ".npc")).string(), origNpcs_)) {
        data_.npcs.clear();  // only the original NPCs here (no hand-placed ones)
        overlay_.actors.clear();
        for (const auto& r : origNpcs_) {  // talk tables (Story::sayOriginal) from the same data
            OverlayActor a;
            a.charId = uint16_t(r.charId);
            a.slot = uint16_t(r.slot);
            a.x = int16_t(r.x);
            a.y = int16_t(r.y);
            a.dir = int16_t(r.dir);
            a.show = r.show;
            a.talk = r.talk;
            overlay_.actors.push_back(std::move(a));
        }
    }
    if (fstage_.hasTrigger && loadDoorTable((base / "doors" / (id + ".door")).string(), doors_)) {
        origDoors_ = true;
    } else {
        doors_.clear();
        for (size_t i = 0; i < data_.exits.size(); ++i) {  // hand-made exits of our own stages (e.g. S400)
            DoorRecord d;
            d.index = int(i);
            d.kind = 1;
            d.target = data_.exits[i].target;
            d.x = data_.exits[i].arriveX;
            d.y = data_.exits[i].arriveY;
            d.dir = -1;
            doors_.push_back(d);
        }
    }
    if (!haveFile) {
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
    if (story_) {
        if (seq_.phase >= 0) pendingStage_ = id;  // after the door sequence (the card first, then the scene)
        else story_->onStage(id);
    }
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
        npcs_.push_back(std::move(n));
    }
    // Original event actors (data/npcs): like FIELDSTG 0x8008A7CC, every record whose show conditions hold is
    // created (skipped there: char 1 = the player, 0x6A, 0x146, 0x147). (0,0) = placed later by an event.
    const std::string& sid = stageIds_[stageIndex_];
    std::set<int> slotsDone;  // without a story (sandbox): the first variant of each slot
    for (const auto& r : origNpcs_) {
        if (r.charId == 1 || r.charId == 0x6A || r.charId == 0x146 || r.charId == 0x147) continue;
        if (r.x == 0 && r.y == 0) continue;
        if (story_ ? !flagsHold(r.show, r.slot) : slotsDone.count(r.slot) > 0) continue;
        slotsDone.insert(r.slot);
        if (story_ && story_->hasActorSlot(sid, r.slot)) continue;  // the story's own version of this NPC
        NpcRt n;
        n.def.kind = NpcKind::Talk;
        n.def.x = r.x;
        n.def.y = r.y;
        n.def.dir = r.dir & 7;
        n.origSlot = r.slot;
        n.hidden = r.sprite == "-";
        if (!n.hidden) {
            n.def.sprite = r.sprite;
            n.actor.sprite = sprites_.get(r.sprite);
            if (!n.actor.sprite) continue;
        }
        n.actor.x = float(r.x);
        n.actor.y = float(r.y);
        n.actor.dir = Dir(r.dir & 7);
        n.actor.tick();
        if (n.actor.sprite && n.actor.anim < 0) {
            const auto& anims = n.actor.sprite->pack.anims;
            for (size_t a = 0; a < anims.size(); ++a)
                if (!anims[a].keys.empty()) { n.actor.anim = int(a); break; }
        }
        npcs_.push_back(std::move(n));
    }
    if (story_) {
        storyRev_ = story_->revision();
        for (const StoryActor* sa : story_->actorsOn(stageIds_[stageIndex_])) {
            NpcRt n;
            n.def = sa->def;
            n.storyId = sa->id;
            if (sa->slot >= 0) {  // the original NPC of that slot: its sprite, position and facing
                const NpcRecord* best = nullptr;
                for (const auto& r : origNpcs_) {
                    if (r.slot != sa->slot || (r.x == 0 && r.y == 0) || r.sprite == "-") continue;
                    if (!best) best = &r;
                    if (flagsHold(r.show, r.slot)) { best = &r; break; }
                }
                if (!best) continue;
                n.def.sprite = best->sprite;
                if (sa->def.x < 0) {  // `slot N at X Y DIR` keeps its own spot
                    n.def.x = best->x;
                    n.def.y = best->y;
                    n.def.dir = best->dir & 7;
                }
                n.origSlot = sa->slot;
            }
            n.actor.sprite = sprites_.get(n.def.sprite);
            n.actor.x = float(n.def.x);
            n.actor.y = float(n.def.y);
            n.actor.dir = Dir(n.def.dir & 7);
            n.actor.tick();
            npcs_.push_back(std::move(n));
        }
    }
}

bool Field::flagsHold(const std::vector<FlagCond>& l, int slot) const {
    if (!story_) return l.empty();
    for (const auto& c : l)
        if (story_->gflag(Story::gkey(stageIds_[stageIndex_], slot, c.flag)) != c.value) return false;
    return true;
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
        if (!n.defeated && n.def.kind == NpcKind::Talk && dist(x, y, n.actor.x, n.actor.y) < (n.hidden ? 12.f : 9.f)) return false;
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

    // ---- door / exit sequence, port effect ----
    if (seq_.phase >= 0) {
        updateDoorSeq(in);
        player_.move = partner_.move = Move::Idle;
        player_.tick();
        partner_.tick();
        for (auto& f : followers_) { f.move = Move::Idle; f.tick(); }
        return;
    }
    if (portT_ >= 0) {
        ++portT_;
        player_.move = Move::Idle;
        player_.tick();
        for (auto& a : anims_)
            if (a.pieces.size() > 1 && ++a.timer >= a.delay) { a.timer = 0; a.cur = (a.cur + 1) % a.pieces.size(); }
        if (portT_ >= kPortTicks) {
            const Port& pt = ports_[size_t(portIdx_)];
            portT_ = -1;
            if (!pt.target.empty()) {
                DoorRecord d;
                d.kind = 1;
                d.target = pt.target;
                d.x = pt.tx;
                d.y = pt.ty;
                d.dir = -1;
                enterDoor(d);
                seq_.phase = 1;  // no door piece: straight to the wipe
            }
        }
        updateCamera();
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
        // ---- doors: standing in a door zone shows the arrow; U / Enter goes through (never by walking) ----
        const int door = playerDoor();
        if (door >= 0 && in.confirmP) {
            const DoorRecord& d = doors_[size_t(door)];
            std::string msg;
            if (story_ && story_->exitBlocked(stageId(), d.target, msg)) story_->sayRef(msg, "", msg);
            else enterDoor(d);
            updateCamera();
            return;
        }
        // ---- ports: stepping on one (or U / Enter on it) opens it ----
        const int port = portAt(player_.x, player_.y);
        if (port >= 0 && (port != lastPort_ || in.confirmP)) {
            portIdx_ = port;
            portT_ = 0;
            lastPort_ = port;
            updateCamera();
            return;
        }
        lastPort_ = port;
        // ---- talk: the nearest NPC in reach, preferring the one the player faces ----
        if (in.confirmP) {
            int best = -1;
            float bestScore = 1e9f;
            float fx, fy;
            dirVector(player_.dir, fx, fy);
            for (size_t i = 0; i < npcs_.size(); ++i) {
                const auto& n = npcs_[i];
                if (n.defeated || n.hidden || n.def.kind != NpcKind::Talk) continue;
                const float dx = n.actor.x - player_.x, dy = n.actor.y - player_.y, d = dist(0, 0, dx, dy);
                if (d >= kTalkRange) continue;
                const float facing = d > 0.f ? (dx * fx + dy * fy) / (d * std::max(0.01f, dist(0, 0, fx, fy))) : 1.f;
                const float score = d - 12.f * facing;
                if (score < bestScore) { bestScore = score; best = int(i); }
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
        if (story_) story_->onEnter(stageId(), player_.x, player_.y);
    }
    updateCamera();
}

// ------------------------------------------------------------------ doors

// The usable warp under (x, y): extracted stages use the trigger map (zone value & 31 = link index), our own
// stages the .stage exit rectangles. Event zones (kind 8) are handled by the story's gates.
int Field::doorAt(float x, float y) const {
    if (origDoors_) {
        const int v = fstage_.triggerAt(int(x), int(y));
        if (!v) return -1;
        for (size_t i = 0; i < doors_.size(); ++i) {
            const DoorRecord& d = doors_[i];
            if (d.index == (v & 31) && d.kind == 1 && flagsHold(d.cond) &&
                std::find(stageIds_.begin(), stageIds_.end(), d.target) != stageIds_.end())
                return int(i);
        }
        return -1;
    }
    for (size_t i = 0; i < data_.exits.size() && i < doors_.size(); ++i) {
        const auto& e = data_.exits[i];
        if (x >= e.x && x < e.x + e.w && y >= e.y && y < e.y + e.h) return int(i);
    }
    return -1;
}

// The door the player can use now: standing in its zone or right next to it (the original drops the player just
// outside the zone on arrival and already shows the arrow there: reference shots 38 and 49). The facing direction
// is tried first.
int Field::playerDoor() const {
    const int d = doorAt(player_.x, player_.y);
    if (d >= 0) return d;
    for (int k = 0; k < 8; ++k) {
        float fx, fy;
        dirVector(Dir((int(player_.dir) + k) & 7), fx, fy);
        const float len = std::max(0.01f, dist(0, 0, fx, fy));
        for (float r = 5.f; r <= 15.f; r += 5.f) {
            const int f = doorAt(player_.x + fx / len * r, player_.y + fy / len * r);
            if (f >= 0) return f;
        }
    }
    return -1;
}

void Field::enterDoor(const DoorRecord& d) {
    seq_ = {};
    seq_.door = d;
    seq_.phase = 0;
    seq_.t = 0;
    const auto it = areaNames_.find(d.target);
    const std::string name = it != areaNames_.end() ? it->second : d.target;
    const size_t sl = name.find(" / ");
    seq_.city = sl == std::string::npos ? "" : name.substr(0, sl);
    seq_.area = sl == std::string::npos ? name : name.substr(sl + 3);
}

bool Field::arriveThrough(const DoorRecord& d, std::string& err) {
    auto it = std::find(stageIds_.begin(), stageIds_.end(), d.target);
    if (it == stageIds_.end()) { err = "no stage " + d.target; return false; }
    if (!loadStage(size_t(it - stageIds_.begin()), err, d.x, d.y)) return false;
    if (d.dir >= 0) player_.dir = Dir(d.dir & 7);
    return true;
}

void Field::updateDoorSeq(const Input& in) {
    ++seq_.t;
    for (auto& a : anims_)
        if (a.pieces.size() > 1 && ++a.timer >= a.delay) { a.timer = 0; a.cur = (a.cur + 1) % a.pieces.size(); }
    switch (seq_.phase) {
        case 0:  // the door piece opens (no door piece: a short pause)
            if (seq_.t >= kDoorOpenTicks) { seq_.phase = 1; seq_.t = 0; }
            break;
        case 1:  // tile wipe covers the screen, then the new stage loads under it
            if (seq_.t >= kWipeTicks) {
                std::string err;
                if (arriveThrough(seq_.door, err) && !dev_) saveStory();  // PC flow: autosave on every area change
                seq_.phase = 2;
                seq_.t = 0;
            }
            break;
        case 2:  // area card ("Asuka City" / "Main Lobby"); U / Enter skips it
            if (seq_.t >= kCardTicks || (seq_.t > 20 && in.confirmP)) { seq_.phase = 3; seq_.t = 0; }
            break;
        default:  // card fades into the new area
            if (seq_.t >= kCardOutTicks) {
                seq_.phase = -1;
                if (story_ && !pendingStage_.empty()) story_->onStage(pendingStage_);
                pendingStage_.clear();
            }
            break;
    }
    updateCamera();
}

// Tile wipe (reference shot 08) and area card (shots 37, 48, 52): drawn over the 320x240 field view; the
// card's text is drawn in renderHud.
void Field::renderDoorSeq() {
    const uint32_t navy = rgba(6, 8, 30), line = rgba(70, 80, 200);
    auto tiles = [&](float k) {  // k 0..1: share of tiles shown (diagonal sweep from the top-left)
        constexpr int tw = 32, th = 30;
        for (int ty = 0; ty < kScreenH / th + 1; ++ty)
            for (int tx = 0; tx < kScreenW / tw; ++tx) {
                const float order = float(tx + ty) / float(kScreenW / tw + kScreenH / th);
                const float a = std::clamp((k * 1.6f - order) * 4.f, 0.f, 1.f);
                if (a <= 0.f) continue;
                const float x = float(tx * tw), y = float(ty * th);
                gfx::fillRect(x, y, tw, th, rgba(6, 8, 30, uint8_t(255 * a)));
                gfx::strokeRect(x + 1, y + 1, tw - 2, th - 2, rgba(50, 60, 170, uint8_t(255 * a)));
                // the circuit trace of each tile
                gfx::fillRect(x + 4, y + 8, 8, 1, rgba(60, 70, 210, uint8_t(200 * a)));
                for (int k2 = 0; k2 < 6; ++k2) gfx::fillRect(x + 12 + k2, y + 8 + k2, 1, 1, rgba(60, 70, 210, uint8_t(200 * a)));
                gfx::fillRect(x + 18, y + 14, 9, 1, rgba(60, 70, 210, uint8_t(200 * a)));
            }
    };
    if (seq_.phase == 1) { tiles(float(seq_.t) / float(kWipeTicks)); return; }
    if (seq_.phase == 0) return;
    const float out = seq_.phase == 3 ? 1.f - float(seq_.t) / float(kCardOutTicks) : 1.f;
    const float in = seq_.phase == 2 ? std::min(1.f, float(seq_.t) / 14.f) : 1.f;
    const uint8_t A = uint8_t(255 * out);
    gfx::fillRect(0, 0, kScreenW, kScreenH, rgba(0, 0, 0, A));
    const uint32_t cyan = rgba(40, 210, 235, A), blue = rgba(60, 60, 210, A), deep = rgba(4, 6, 120, A);
    (void)navy;
    (void)line;
    // the band (city) and the box (area) slide in from the right, the vertical lines from the left
    const float bx = kScreenW * (1.f - in);
    gfx::fillRect(bx, 63, kScreenW, 14, deep);
    gfx::fillRect(bx, 77, kScreenW, 16, blue);
    gfx::fillRect(bx, 83, kScreenW, 1, cyan);
    gfx::fillRect(bx + 125, 12, 116, 26, deep);
    gfx::fillRect(bx + 125, 38, 116, 5, blue);
    gfx::fillRect(bx + 124, 37, kScreenW, 1, cyan);
    const float vx = -40.f + 54.f * in;
    gfx::fillRect(vx, 0, 1, kScreenH, cyan);
    gfx::fillRect(vx + 13, 0, 9, kScreenH, blue);
    gfx::fillRect(vx + 18, 0, 1, kScreenH, cyan);
    gfx::fillRect(kScreenW - 93.f * in, 0, 1, kScreenH, cyan);
}

void Field::loadDoorMark() {
    Bytes b;
    UiPack pk;
    if (!readFile(root_ / "DAT" / "FIELD" / "FIELDCOM.BIN", b) || !loadUiPack(b, pk) || pk.cells.empty()) return;
    const UiTexGroup& tex = pk.tex[size_t(pk.texFor[0])];
    for (int p = kMarkFirst; p < kMarkFirst + kMarkFrames; ++p) {
        Image img;
        MarkFrame m;
        if (!renderUiPiece(tex, pk.cells[0], p, img, m.ox, m.oy)) continue;
        m.tex = gfx::upload(img);
        doorMark_.push_back(m);
    }
}

void Field::loadAreaNames() {
    std::ifstream f(stageDir_.parent_path() / "areas.cfg");
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream is(line);
        std::string id, name;
        is >> id >> std::ws;
        std::getline(is, name);
        if (!name.empty() && name.back() == '\r') name.pop_back();
        if (name.size() >= 2 && name.front() == '"') name = name.substr(1, name.find('"', 1) - 1);
        if (!id.empty()) areaNames_[id] = name;
    }
}

// data/ports.cfg (ours): port STAGE X Y [RADIUS] [-> TARGET X Y]
void Field::loadPorts() {
    std::ifstream f(stageDir_.parent_path() / "ports.cfg");
    std::string line;
    while (std::getline(f, line)) {
        std::istringstream is(line);
        std::string k;
        if (!(is >> k) || k != "port") continue;
        Port p;
        if (!(is >> p.stage >> p.x >> p.y)) continue;
        std::string w;
        while (is >> w) {
            if (w == "->") is >> p.target >> p.tx >> p.ty;
            else p.r = std::atoi(w.c_str());
        }
        ports_.push_back(p);
    }
}

int Field::portAt(float x, float y) const {
    for (size_t i = 0; i < ports_.size(); ++i) {
        const Port& p = ports_[i];
        if (p.stage != stageIds_[stageIndex_]) continue;
        const float dx = x - float(p.x), dy = (y - float(p.y)) * 2.f;  // iso ellipse
        if (dx * dx + dy * dy < float(p.r * p.r)) return int(i);
    }
    return -1;
}

int Field::doorSelfTest(const std::vector<std::string>& stages, std::vector<std::string>& report) {
    int fails = 0;
    char b[256];
    auto zoneSpot = [&](int index, float& ox, float& oy) {  // a walkable pixel of trigger zone `index`
        for (int y = 0; y < fstage_.h; y += 2)
            for (int x = 0; x < fstage_.w; x += 2) {
                const int v = fstage_.triggerAt(x, y);
                if (v && (v & 31) == index && walkable(float(x), float(y))) { ox = float(x); oy = float(y); return true; }
            }
        return false;
    };
    for (const auto& st : stages) {
        if (!warpTo(st, -1, -1)) { report.push_back("FAIL " + st + ": cannot load"); ++fails; continue; }
        const std::vector<DoorRecord> list = doors_;
        for (const auto& d : list) {
            if (d.kind != 1) continue;
            if (!warpTo(st, -1, -1)) break;
            float x, y;
            if (!zoneSpot(d.index, x, y)) {
                std::snprintf(b, sizeof b, "FAIL %s zone %d -> %s: no walkable spot in the trigger zone", st.c_str(), d.index, d.target.c_str());
                report.push_back(b); ++fails; continue;
            }
            placeParty(x, y);
            const int here = doorAt(player_.x, player_.y);
            if (here < 0) {
                std::snprintf(b, sizeof b, "SKIP %s zone %d -> %s: closed now (condition %s) or target stage missing", st.c_str(), d.index,
                              d.target.c_str(), flagListText(d.cond).c_str());
                report.push_back(b); continue;
            }
            std::string err;
            const DoorRecord go = doors_[size_t(here)];
            if (!arriveThrough(go, err) || stageId() != d.target) {
                std::snprintf(b, sizeof b, "FAIL %s zone %d: did not arrive in %s (%s)", st.c_str(), d.index, d.target.c_str(), err.c_str());
                report.push_back(b); ++fails; continue;
            }
            // The original drops the player just OUTSIDE the matching door's zone (so it does not fire again):
            // the door back must be within a step of the arrival point.
            const int ax = int(player_.x), ay = int(player_.y);
            if (!fstage_.hasTrigger) {
                std::snprintf(b, sizeof b, "SKIP %s zone %d -> %s: arrived at %d,%d, but %s has no decoded trigger map (no doors to test)",
                              st.c_str(), d.index, d.target.c_str(), ax, ay, d.target.c_str());
                report.push_back(b);
                continue;
            }
            auto nearDoor = [&](int cx, int cy, const std::string& to, int& zone) {
                int best = -1, bestD = 1 << 30;
                for (int y = cy - 16; y <= cy + 16; ++y)
                    for (int x = cx - 16; x <= cx + 16; ++x) {
                        const int i = doorAt(float(x), float(y));
                        const int dd = (x - cx) * (x - cx) + (y - cy) * (y - cy);
                        if (i >= 0 && doors_[size_t(i)].target == to && dd < bestD) { bestD = dd; best = i; }
                    }
                zone = best >= 0 ? doors_[size_t(best)].index : -1;
                return best;
            };
            int zoneBack = -1;
            const int back = nearDoor(ax, ay, st, zoneBack);
            if (back < 0) {
                std::snprintf(b, sizeof b, "FAIL %s zone %d -> %s at %d,%d: no door back to %s within 16 px", st.c_str(), d.index,
                              d.target.c_str(), ax, ay, st.c_str());
                report.push_back(b); ++fails; continue;
            }
            const DoorRecord ret = doors_[size_t(back)];
            if (!arriveThrough(ret, err) || stageId() != st) {
                std::snprintf(b, sizeof b, "FAIL %s zone %d -> %s -> back: arrived in %s", st.c_str(), d.index, d.target.c_str(), stageId().c_str());
                report.push_back(b); ++fails; continue;
            }
            int zoneHere = -1;
            nearDoor(int(player_.x), int(player_.y), d.target, zoneHere);
            const bool same = zoneHere == d.index;
            std::snprintf(b, sizeof b, "%s %s zone %d -> %s (arrive %d,%d next to its zone %d) -> back at %d,%d %s", same ? "PASS" : "FAIL",
                          st.c_str(), d.index, d.target.c_str(), ax, ay, ret.index, int(player_.x), int(player_.y),
                          same ? "next to the same door" : "NOT next to the door it was entered from");
            report.push_back(b);
            if (!same) ++fails;
        }
    }
    return fails;
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
    const bool opening = seq_.phase == 0 && seq_.door.link >= 0x64;
    for (const auto& a : anims_) {  // original animated scene pieces and glows (foreground pieces: below)
        if (a.depthY) continue;
        const PieceTex* pt = pieceTex(a.pieces[a.cur]);
        if (!pt) continue;
        if (a.link && opening && a.link == seq_.door.link) {  // the door opens: its panel fades into the doorway
            const float k = std::min(1.f, float(seq_.t) / float(kDoorOpenTicks - 4));
            const float x = float(a.x + pt->ox) - camX_, y = float(a.y + pt->oy) - camY_;
            gfx::drawTexture(pt->tex, x, y - 3.f * k, false, 1.f - k);
            gfx::drawGlow(x + pt->tex.w / 2.f, y + pt->tex.h / 2.f, 10.f + 14.f * k, rgba(150, 210, 255, uint8_t(120 * k)));
            continue;
        }
        const float x = float(a.x + pt->ox) - camX_, y = float(a.y + pt->oy) - camY_;
        if (x > kScreenW || y > kScreenH || x + pt->tex.w < 0 || y + pt->tex.h < 0) continue;
        if (a.additive) gfx::drawSprite(pt->tex, x, y, false, 1.f, 0xffffffffu, true);
        else gfx::drawTexture(pt->tex, x, y);
    }

    // painter's order by feet Y
    struct Item { float y; int kind; size_t idx; };
    std::vector<Item> order{{player_.y, 0, 0}};
    for (size_t i = 0; i < anims_.size(); ++i)  // foreground pieces sort with the actors by their line
        if (anims_[i].depthY) order.push_back({float(anims_[i].depthY), 4, i});
    if (partnersVisible()) {
        order.push_back({partner_.y, 1, 0});
        for (size_t i = 0; i < followers_.size(); ++i) order.push_back({followers_[i].y, 3, i});
    }
    for (size_t i = 0; i < npcs_.size(); ++i)
        if (!npcs_[i].defeated && !npcs_[i].hidden) order.push_back({npcs_[i].actor.y, 2, i});
    std::sort(order.begin(), order.end(), [](const Item& a, const Item& b) { return a.y < b.y; });
    for (const auto& o : order) {
        if (o.kind == 4) {
            const auto& a = anims_[o.idx];
            if (const PieceTex* pt = pieceTex(a.pieces[a.cur]))
                gfx::drawTexture(pt->tex, float(a.x + pt->ox) - camX_, float(a.y + pt->oy) - camY_);
            continue;
        }
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

    // port opening: rings of light rise from the pad, a column flashes
    if (portT_ >= 0 && portIdx_ >= 0) {
        const Port& pt = ports_[size_t(portIdx_)];
        const float cx = float(pt.x) - camX_, cy = float(pt.y) - camY_, t = float(portT_) / float(kPortTicks);
        const float col = std::sin(std::min(1.f, t * 1.4f) * 3.14159f);
        gfx::drawGlow(cx, cy, 18.f + 10.f * col, rgba(90, 220, 255, uint8_t(150 * col)));
        gfx::fillRect(cx - 6, cy - 60 * col, 12, 60 * col, rgba(150, 230, 255, uint8_t(90 * col)));
        for (int k = 0; k < 4; ++k) {
            const float rt = std::fmod(t * 2.f + float(k) * 0.25f, 1.f);
            const float ry = cy - 50.f * rt, rw = 26.f * (1.f - 0.4f * rt);
            gfx::strokeRect(cx - rw / 2, ry - rw / 4, rw, rw / 2, rgba(180, 240, 255, uint8_t(220 * (1.f - rt) * col)));
        }
    }
    // the door arrow over the player (reference shot 38): U / Enter goes through
    const bool cine = story_ && story_->busy();
    if (!editor_ && !cine && seq_.phase < 0 && portT_ < 0 && playerDoor() >= 0) {
        if (!doorMark_.empty()) {  // the original marker frames (timing: our guess, see FORMATS.md)
            const MarkFrame& m = doorMark_[size_t(ticks_ / kMarkTicks) % doorMark_.size()];
            gfx::drawTexture(m.tex, std::floor(player_.x - camX_) + float(m.ox), std::floor(player_.y - camY_) + float(m.oy));
        }
    }
    if (seq_.phase >= 0) renderDoorSeq();

    if (story_ && !editor_) story_->render();
    if (talkNpc_ >= 0) {
        gfx::fillRect(12, 168, 296, 62, 0xd8301808u);
        gfx::strokeRect(12, 168, 296, 62, 0xffc0e0ffu);
    }
}

void Field::renderHud(int ww, int wh) {
    char line[256];
    const bool cine = story_ && story_->busy() && !editor_;  // story scene: keep the screen clean
    // NPC labels
    for (const auto& n : npcs_) {
        if (cine || seq_.phase >= 0 || portT_ >= 0) break;
        if ((n.defeated || n.hidden) && !editor_) continue;
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
                      party_.team.size() > 1 && party_.partnersOut ? "Move: WASD/Arrows  Run: Shift  Talk/Door: U or Enter  Partner: Tab  Menu: Esc  HUD: F1"
                                             : "Move: WASD/Arrows  Run: Shift  Talk/Door: U or Enter  Menu: Esc  HUD: F1",
                      0xffb0e0ffu);
    }
    if (hud_ && !cine && dev_) {
        std::snprintf(line, sizeof line, "DMW3 PC Edition v%s | Stage %s (%zu/%zu)  pos %d,%d  %s%s", DMW3_VERSION,
                      stageIds_[stageIndex_].c_str(), stageIndex_ + 1, stageIds_.size(), int(player_.x), int(player_.y),
                      "", editor_ ? "  [EDITOR]" : "");
        gfx::drawText(10, 20, line);
        if (!editor_)
            gfx::drawText(10, wh - 12,
                          "Move: WASD/Arrows  Run: Shift  Talk/Door: U or Enter  Stage: PgUp/PgDn  Editor: F2  HUD: F1",
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

    if (seq_.phase >= 2) {  // area card text
        const float in = seq_.phase == 2 ? std::min(1.f, float(seq_.t) / 14.f) : 1.f;
        const uint8_t a = uint8_t(255 * (seq_.phase == 3 ? 1.f - float(seq_.t) / float(kCardOutTicks) : 1.f));
        const float bx = kScreenW * (1.f - in);
        int x, y;
        gfx::toWindow(bx + 40, 64, x, y);  // band: the city
        gfx::drawText(x, y, (seq_.city.empty() ? seq_.area : seq_.city).c_str(), (uint32_t(a) << 24) | 0x00ffffffu);
        gfx::toWindow(bx + 129, 22, x, y);  // box: the area
        if (!seq_.city.empty()) gfx::drawText(x, y, seq_.area.c_str(), (uint32_t(a) << 24) | 0x00ffffffu);
    }
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
