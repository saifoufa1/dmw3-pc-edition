// OpenGL 1.1 2D renderer: textured quads in a virtual 320x240 screen (wider for field/battle on wide windows).
//
// Frame structure (see beginFrame / endFrame):
//   * Everything drawn in VIRTUAL coordinates (world, original UI) goes into an off-screen scene target
//     (framebuffer object) of the virtual size times an integer render scale R, so pixel art stays pixel exact
//     and the 3D battle is rasterised at (about) the output resolution. Optional MSAA for the 3D edges.
//   * endFrame() puts the scene target on the window with the chosen scale mode (Fit / Stretch / Integer):
//     sharp-bilinear (the integer-scaled target filtered down to the window: no blur, no shimmer) or nearest,
//     optional CRT scanlines, then draws the HUD text (Win32 bitmap font) in real window pixels on top.
//   * Without framebuffer objects (very old drivers) everything is drawn straight into the window instead.
//
// Virtual coordinates: x 0..320 is always the centred 4:3 area. When the frame is wide (field / battle in a
// wide window with the Widescreen option on), the visible range grows to viewLeft()..viewRight() (viewLeft()
// is then negative). A fill that covers the whole 0..320 x 0..240 screen is widened to the visible range.
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

enum class ScaleMode : int { Fit = 0, Stretch = 1, Integer = 2 };

// Display options (Options screen, saved in options.cfg by the App).
struct VideoOptions {
    ScaleMode scale = ScaleMode::Fit;
    bool wide = true;    // field + battle show more of the scene on windows wider than 4:3
    bool hiRes = true;   // render the virtual screen at the output resolution (else the original 320x240)
    bool sharp = true;   // sharp-bilinear upscale (else plain nearest)
    int msaa = 0;        // 0 / 2 / 4 / 8 samples (3D edges in battle), if the driver supports it
    bool crt = false;    // scanlines + soft vignette
};

namespace gfx {
Texture upload(const Image& img);
void destroy(Texture& t);

void setVideoOptions(const VideoOptions& o);
const VideoOptions& videoOptions();
int maxMsaa();  // highest MSAA sample count the driver offers (0 = none; valid after the first frame)

// Begins a frame (clears). wideScene: this frame may use the extra width of a wide window (field, battle).
void beginFrame(int windowW, int windowH, bool wideScene = false);
// Composes the scene onto the window (scale mode, filter, CRT) and draws the queued HUD text. Call once per
// frame before SwapBuffers / reading the screenshot.
void endFrame();
// Drops the HUD text queued so far this frame (e.g. a pause menu drawn over the game).
void clearHudText();
// Screenshots of any size: compose into an off-screen window-sized target instead of the real window.
void setOffscreenPresent(bool on);
// Reads the composed frame (after endFrame) into `out` (top-down RGBA).
bool readFrame(Image& out);
void shutdown();  // frees the render targets (context must be current)

// Visible virtual x range of the current frame (0..320 unless the frame is wide).
float viewLeft();
float viewRight();

void drawTexture(const Texture& t, float x, float y, bool flipX = false, float alpha = 1.0f);
// Scaled + tinted sprite. (x, y) is the top-left; `additive` brightens (hit flashes, glows).
void drawSprite(const Texture& t, float x, float y, bool flipX, float scale, uint32_t rgbaColor, bool additive = false);
// Centered additive glow (projectiles, sparks). Uses a generated radial texture.
void drawGlow(float cx, float cy, float radius, uint32_t rgbaColor);
// Rotated sprite around a pivot (texture pixels). angle in radians, clockwise on screen.
void drawRotated(const Texture& t, float px, float py, float pivotX, float pivotY, float angle, float scale,
                 bool flipX, uint32_t rgbaColor);
void strokeRect(float x, float y, float w, float h, uint32_t rgbaColor);
// Virtual coordinate -> window pixel (for HUD text placed over the world).
void toWindow(float vx, float vy, int& wx, int& wy);
// Window pixel -> virtual coordinate (mouse). Correct for every scale mode.
void toVirtual(int wx, int wy, float& vx, float& vy);
int pixelScale();  // window pixels per virtual pixel, rounded (>= 1)
void fillRect(float x, float y, float w, float h, uint32_t rgbaColor);
// Widens a rect that covers the whole 320x240 screen to the visible range (wide frames). Used by fills.
void coverView(float& x, float& w, float y, float h);

// Text in window pixels (HUD). Requires initText() once after the GL context exists.
void initText(void* hdc);
void beginHud(int windowW, int windowH);
// Back to the virtual projection (no clear) - for original-UI drawing after beginHud.
void beginVirtual();
void drawText(int x, int y, const char* s, uint32_t rgbaColor = 0xffffffffu);
void drawTextCentered(int cx, int y, const char* s, uint32_t rgbaColor = 0xffffffffu);
constexpr int kCharW = 8;  // Consolas 15px advance
}  // namespace gfx

}  // namespace dw3
