// Draws DW3 battle models (formats/model.h) into the 320x240 virtual screen with OpenGL 1.1:
// orthographic camera tilted down at the arena, depth-tested, textured per CLUT, simple
// directional light from the model's own normals.
#pragma once
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "formats/model.h"
#include "render/gl_renderer.h"

namespace dw3 {

struct ModelGpu {
    std::vector<Texture> tex;  // one RGBA texture per CLUT
};
// Palette recolours: Digimon that reuse another Digimon's battle model with other colours.
// DAT/MODEL has no file of its own for Black Agumon / Blue Agumon (field sprites P171BAGM /
// P167SAGM), so they are Agumon's M003AGUM mesh and clips with a repainted texture.
enum class Recolor { None, Black, Blue };
void recolorImage(Image& img, Recolor r);
void uploadModel(const Model& m, ModelGpu& gpu, Recolor recolor = Recolor::None);
void destroyModel(ModelGpu& gpu);

struct ModelView {
    float x = 0, y = 0;         // ground point (feet) in virtual screen pixels
    float pxPerUnit = 0.04f;    // model units -> virtual pixels
    float yaw = 0;              // radians, 0 = facing the camera, -pi/2 = facing screen right
    float pitch = 0.32f;        // camera tilt (radians)
    uint32_t tint = 0xffffffffu;  // multiplies colour; alpha = overall opacity
    float flash = 0;            // 0..1 white overlay (hits, digivolution)
    bool silhouette = false;    // flat colour (tint) instead of the texture
};
void drawModel(const Model& m, const ModelGpu& gpu, const Pose& pose, const ModelView& v);

// 3D battle: a model placed in the world and drawn with the current perspective camera
// (render/camera3d.h, Camera3D::begin() must be active). World = PS1 axes (y down).
struct Camera3D;
struct WorldPlace {
    float x = 0, y = 0, z = 0;  // ground point (feet) in world units
    float yaw = 0;              // radians; the model's front (-z) points to (-sin yaw, -cos yaw)
    float scale = 1;            // model units -> world units
    uint32_t tint = 0xffffffffu;
    float flash = 0;
    bool silhouette = false;
    int passes = 3;             // bit 0: opaque prims, bit 1: semi-transparent prims
    bool shade = true;          // lit prims use their normals (false: full colour)
};
void drawModelWorld(const Model& m, const ModelGpu& gpu, const Pose& pose, const Camera3D& cam, const WorldPlace& p);
// Soft dark disc on the ground (contact shadow) in world space.
void drawGroundShadow3D(float x, float y, float z, float radius, float alpha);
// Soft dark ellipse on the ground (contact shadow).
void drawGroundShadow(float cx, float cy, float rx, float ry, float alpha);

struct GameModel {
    Model model;
    ModelGpu gpu;
    std::string file;
    ~GameModel() { destroyModel(gpu); }
};

// Loads DAT/MODEL/<prefix>*.BIN once (prefix like "M005GREY"; "EFFECT/MEFT0331" loads from
// DAT/EFFECT) and keeps it uploaded. A ":black" / ":blue" suffix uploads a recoloured copy.
class ModelCache {
public:
    void setRoot(std::filesystem::path r) { root_ = std::move(r); }
    const std::filesystem::path& root() const { return root_; }
    std::shared_ptr<GameModel> get(const std::string& id, std::string* err = nullptr);

private:
    std::filesystem::path root_;
    std::map<std::string, std::shared_ptr<GameModel>> cache_;
};

}  // namespace dw3
