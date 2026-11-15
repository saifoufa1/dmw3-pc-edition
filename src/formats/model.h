// DW3 battle 3D models: DAT/MODEL/M###XXXX.BIN (see docs/FORMATS.md, "Battle model").
//
// One file = an offset table of sections. A hierarchy section names the texture section and
// lists the skeleton as (parent, meshSection, trackSection) triples. Each node has its own
// keyframe tracks (position / rotation / scale, keys = int16 x, y, z, time on one shared
// timeline) and an optional rigid mesh (int16 vertices, 4096-scaled normals, a byte-coded
// tri/quad stream). Section 0 is the clip table: each clip is a list of segments that play
// timeline frames [from..to] or blend to the next segment.
#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "core/bytes.h"
#include "formats/image.h"

namespace dw3 {

struct ModelPrim {
    uint8_t count = 3;                  // 3 = triangle, 4 = quad (PS1 order: 0,1,2 + 1,3,2)
    uint8_t v[4]{}, n[4]{};             // vertex / normal indices (normals only when lit)
    uint8_t u[4]{}, t[4]{};             // absolute texel coords in the 256x256 texture
    uint8_t clut = 0;                   // index into Model::cluts
    bool lit = true;                    // false: flat colour (r, g, b; 128 = 1.0), no normals
    uint8_t r = 128, g = 128, b = 128;
    uint8_t blend = 0;                  // PS1 semi-transparency mode (state 0xE?)
};

struct ModelMesh {
    std::vector<std::array<int16_t, 3>> verts, norms;
    std::vector<ModelPrim> prims;
};

struct ModelKey { int16_t x, y, z, t; };
struct ModelTrack { std::vector<ModelKey> pos, rot, scl; };

struct ModelNode {
    int parent = -1;  // node index, -1 = root
    int mesh = -1;    // index into Model::meshes, -1 = no geometry
    int track = -1;   // index into Model::tracks
};

struct ClipSeg { int start = 0, len = 0, from = 0, to = 0; bool blend = false; };
struct ModelClip {
    std::vector<ClipSeg> segs;
    int length = 0;
    bool loop = false;
    bool valid() const { return !segs.empty(); }
};

struct Model {
    std::vector<ModelNode> nodes;
    std::vector<ModelMesh> meshes;
    std::vector<ModelTrack> tracks;
    std::vector<ModelClip> clips;
    std::vector<std::array<int, 2>> cluts;  // (x in 16-bit words, y row) inside the texture
    std::vector<uint16_t> vram;             // texture as 64x256 16-bit words (4bpp pixels + CLUT rows)
    static constexpr int kTexW = 256, kTexH = 256;

    uint8_t texel(int u, int v) const {     // 4bpp palette index
        uint16_t w = vram[size_t(v & 255) * 64 + size_t((u & 255) >> 2)];
        return uint8_t((w >> ((u & 3) * 4)) & 15);
    }
    // RGBA texture for one CLUT (palette colour 0x0000 is transparent).
    Image textureFor(size_t clut) const;
    // Height of the bind pose in model units (feet at y = 0, up is -y).
    float height = 0;
};

bool loadModel(ByteView file, Model& out, std::string& err);

// ---- animation ----
struct NodePose { float px, py, pz, rx, ry, rz, sx, sy, sz; };  // rotation in 4096 = 360 deg
using Pose = std::vector<NodePose>;

void samplePose(const Model& m, float frame, Pose& out);
void lerpPose(const Pose& a, const Pose& b, float f, Pose& out);

struct Mat34 { float m[3][3]; float t[3]; };
// World matrices for every node (PS1 RotMatrix order: R = Rx * Ry * Rz).
void poseMatrices(const Model& m, const Pose& pose, std::vector<Mat34>& out);
inline void xform(const Mat34& M, float x, float y, float z, float& ox, float& oy, float& oz) {
    ox = M.m[0][0] * x + M.m[0][1] * y + M.m[0][2] * z + M.t[0];
    oy = M.m[1][0] * x + M.m[1][1] * y + M.m[1][2] * z + M.t[1];
    oz = M.m[2][0] * x + M.m[2][1] * y + M.m[2][2] * z + M.t[2];
}

// Plays clips from a model's clip table. Blend segments cross-fade from whatever pose was
// showing (so switching clips is smooth too).
class ClipPlayer {
public:
    // Starts `clip` (falls back to clip 0 when missing). Keeps the current pose for blending.
    void play(const Model& m, int clip);
    void update(const Model& m, float frames);  // advance by `frames` timeline frames
    const Pose& pose() const { return cur_; }
    int clip() const { return clip_; }
    float time() const { return t_; }
    bool done() const { return done_; }
    // Timeline frame being shown (or -1 while blending).
    int sourceFrame() const { return srcFrame_; }

private:
    void evaluate(const Model& m);
    Pose cur_, from_, target_, tmp_;
    int clip_ = -1, seg_ = -1, srcFrame_ = -1;
    float t_ = 0;
    bool done_ = false;
};

}  // namespace dw3
