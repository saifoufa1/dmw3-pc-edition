#include "render/camera3d.h"

#include <windows.h>
#include <GL/gl.h>

#include <cmath>

#include "render/gl_renderer.h"

namespace dw3 {

void Camera3D::update() {
    const float cy = std::cos(yaw), sy = std::sin(yaw), cp = std::cos(pitch), sp = std::sin(pitch);
    fwd[0] = sy * cp; fwd[1] = sp; fwd[2] = cy * cp;      // +y is down: looking down = +y
    right[0] = cy; right[1] = 0; right[2] = -sy;
    // down = fwd x right (view-space y, pointing down the screen)
    down[0] = fwd[1] * right[2] - fwd[2] * right[1];
    down[1] = fwd[2] * right[0] - fwd[0] * right[2];
    down[2] = fwd[0] * right[1] - fwd[1] * right[0];
    for (int i = 0; i < 3; ++i) eye[i] = target[i] - fwd[i] * dist;
    focal = (kScreenH * 0.5f) / std::tan(fovY * 0.5f);
}

void Camera3D::begin() const {
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    const double h = zNear * std::tan(fovY * 0.5), w = h * double(kScreenW) / double(kScreenH);
    glFrustum(-w, w, -h, h, zNear, zFar);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    // GL eye space = (view x, -view y, -view z): rows right, -down, -fwd.
    const float* r = right;
    const float* d = down;
    const float* f = fwd;
    auto dot = [&](const float* a) { return a[0] * eye[0] + a[1] * eye[1] + a[2] * eye[2]; };
    const GLfloat m[16] = {
        r[0], -d[0], -f[0], 0,
        r[1], -d[1], -f[1], 0,
        r[2], -d[2], -f[2], 0,
        -dot(r), dot(d), dot(f), 1,
    };
    glLoadMatrixf(m);
    glClear(GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDisable(GL_CULL_FACE);
}

void Camera3D::end() {
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
    glEnable(GL_TEXTURE_2D);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColor4f(1, 1, 1, 1);
}

bool Camera3D::project(float x, float y, float z, float& sx, float& sy, float& depth) const {
    const float d[3] = {x - eye[0], y - eye[1], z - eye[2]};
    const float vx = d[0] * right[0] + d[1] * right[1] + d[2] * right[2];
    const float vy = d[0] * down[0] + d[1] * down[1] + d[2] * down[2];
    depth = d[0] * fwd[0] + d[1] * fwd[1] + d[2] * fwd[2];
    if (depth < zNear) return false;
    sx = kScreenW * 0.5f + vx * focal / depth;
    sy = kScreenH * 0.5f + vy * focal / depth;
    return true;
}

void Camera3D::groundAxes(float& fx, float& fz, float& rx, float& rz) const {
    fx = std::sin(yaw);
    fz = std::cos(yaw);
    rx = std::cos(yaw);
    rz = -std::sin(yaw);
}

}  // namespace dw3
