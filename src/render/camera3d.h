// Perspective camera for the 3D battle. World = PS1 / DW3 model space (x right, y DOWN, z into
// the screen at yaw 0), so model and arena data are used as stored. The camera orbits a target
// point: yaw turns around the vertical axis, pitch looks down onto the ground.
#pragma once

namespace dw3 {

struct Camera3D {
    float target[3] = {0, 0, 0};
    float yaw = 0;          // radians; 0 = looking along +z (world x is screen right)
    float pitch = 0.3f;     // radians, > 0 looks down
    float dist = 8000;      // eye distance from the target
    float fovY = 0.85f;     // vertical field of view (radians)
    float zNear = 120, zFar = 220000;

    // Derived by update().
    float eye[3] = {}, fwd[3] = {}, right[3] = {}, down[3] = {};
    float focal = 1;        // virtual pixels per unit at depth 1 (240 px tall screen)

    void update();
    // Sets GL projection + modelview for the 320x240 virtual viewport (already set by beginFrame),
    // clears depth and enables depth testing. end() restores the 2D state.
    void begin() const;
    static void end();
    // World point -> virtual screen (320x240). depth = distance along the view axis; false when
    // the point is behind the camera.
    bool project(float x, float y, float z, float& sx, float& sy, float& depth) const;
    // Virtual pixels per world unit at a given depth.
    float scaleAt(float depth) const { return depth > 1 ? focal / depth : focal; }
    // Ground-plane (x, z) directions of the camera: forward (into the screen) and right.
    void groundAxes(float& fx, float& fz, float& rx, float& rz) const;
};

}  // namespace dw3
