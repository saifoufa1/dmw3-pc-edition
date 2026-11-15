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
void fillRect(float x, float y, float w, float h, uint32_t rgbaColor);

// Text in window pixels (HUD). Requires initText() once after the GL context exists.
void initText(void* hdc);
void beginHud(int windowW, int windowH);
void drawText(int x, int y, const char* s, uint32_t rgbaColor = 0xffffffffu);
}  // namespace gfx

}  // namespace dw3
