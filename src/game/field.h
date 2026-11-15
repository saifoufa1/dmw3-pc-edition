// Field mode: walk the tamer + partner Digimon around a pre-rendered stage.
#pragma once
#include <deque>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "formats/sprite_pack.h"
#include "formats/stage_pack.h"
#include "render/gl_renderer.h"

namespace dw3 {

struct Input {
    bool up = false, down = false, left = false, right = false, run = false;
    bool nextStage = false, prevStage = false, nextPartner = false, toggleHud = false;  // edge-triggered
};

// 8 facings in screen space. Stored sprites cover S..N; W side is drawn mirrored.
enum Dir { S, SE, E, NE, N, NW, W, SW };
enum class Move { Idle = 0, Walk = 1, Run = 2 };

struct SpriteSet {
    std::string name;
    SpritePack pack;
    std::vector<Texture> tex;
    ~SpriteSet();
};

struct Actor {
    std::shared_ptr<SpriteSet> sprite;
    float x = 0, y = 0;  // feet position in map pixels
    Dir dir = S;
    Move move = Move::Idle;
    int anim = -1, key = 0, timer = 0;

    void tick();
    void draw(float camX, float camY) const;
};

class Field {
public:
    bool init(const std::filesystem::path& dataRoot, const std::string& startStage, std::string& err);
    void update(const Input& in);
    void render();
    void renderHud(int windowW, int windowH);
    ~Field();

private:
    bool loadStage(size_t index, std::string& err);
    std::shared_ptr<SpriteSet> loadSprite(const std::string& id, std::string& err);
    bool walkable(float x, float y) const;
    void spawn();
    void updatePartner(float speed);

    std::filesystem::path root_;
    std::vector<std::string> stageIds_;
    size_t stageIndex_ = 0;
    StageMap stage_;
    Texture stageTex_;

    Actor player_, partner_;
    std::vector<std::string> partnerIds_;
    size_t partnerIndex_ = 0;
    std::deque<std::pair<float, float>> trail_;

    float camX_ = 0, camY_ = 0;
    bool hud_ = true;
    std::string status_;
    int statusTimer_ = 0;
    long long ticks_ = 0;
};

}  // namespace dw3
