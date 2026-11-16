// Minimal OpenGL 1.1 2D renderer: textured quads in a 320x240 virtual screen.
#pragma once
#include <cstdint>

#include "formats/image.h"

namespace dw3 {

constexpr int kScreenW = 320;
constexpr int kScreenH = 240;

struct Texture {
    unsigned id = 0;
    int w = 0, h = 0;
};

namespace gfx {
Texture upload(const Image& img);
void destroy(Texture& t);

// Begins a frame: clears the window and sets a pixel-exact, letterboxed 320x240 view.
void beginFrame(int windowW, int windowH);
void drawTexture(const Texture& t, float x, float y, bool flipX = false, float alpha = 1.0f);
// Scaled + tinted sprite. (x, y) is the top-left; `additive` brightens (hit flashes, glows).
void drawSprite(const Texture& t, float x, float y, bool flipX, float scale, uint32_t rgbaColor, bool additive = false);
// Centered additive glow (projectiles, sparks). Uses a generated radial texture.
void drawGlow(float cx, float cy, float radius, uint32_t rgbaColor);
// Rotated sprite around a pivot (texture pixels). angle in radians, clockwise on screen.
void drawRotated(const Texture& t, float px, float py, float pivotX, float pivotY, float angle, float scale,
                 bool flipX, uint32_t rgbaColor);
void strokeRect(float x, float y, float w, float h, uint32_t rgbaColor);
// Virtual 320x240 coordinate -> window pixel (for HUD text placed over the world).
void toWindow(float vx, float vy, int& wx, int& wy);
// Window pixel -> virtual coordinate (mouse).
void toVirtual(int wx, int wy, float& vx, float& vy);
int pixelScale();
void fillRect(float x, float y, float w, float h, uint32_t rgbaColor);

// Text in window pixels (HUD). Requires initText() once after the GL context exists.
void initText(void* hdc);
void beginHud(int windowW, int windowH);
// Back to the 320x240 virtual projection (no clear) - for original-UI drawing after beginHud.
void beginVirtual();
void drawText(int x, int y, const char* s, uint32_t rgbaColor = 0xffffffffu);
void drawTextCentered(int cx, int y, const char* s, uint32_t rgbaColor = 0xffffffffu);
constexpr int kCharW = 8;  // Consolas 15px advance
}  // namespace gfx

}  // namespace dw3
