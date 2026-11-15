#include "render/gl_renderer.h"

#include <windows.h>
#include <GL/gl.h>

#include <algorithm>
#include <cmath>
#include <cstring>

#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif

namespace dw3::gfx {

static unsigned g_fontBase = 0;
static int g_scale = 1, g_ox = 0, g_oy = 0, g_winW = 0, g_winH = 0;
static Texture g_glow;

Texture upload(const Image& img) {
    Texture t;
    t.w = img.w;
    t.h = img.h;
    glGenTextures(1, &t.id);
    glBindTexture(GL_TEXTURE_2D, t.id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, img.w, img.h, 0, GL_RGBA, GL_UNSIGNED_BYTE, img.px.data());
    return t;
}

void destroy(Texture& t) {
    if (t.id) glDeleteTextures(1, &t.id);
    t = {};
}

void beginFrame(int ww, int wh) {
    glViewport(0, 0, ww, wh);
    glClearColor(0.f, 0.f, 0.f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT);
    // Largest integer scale that fits keeps pixels crisp; fall back to fit when window is tiny.
    int scale = std::max(1, std::min(ww / kScreenW, wh / kScreenH));
    int vw = kScreenW * scale, vh = kScreenH * scale;
    g_scale = scale; g_ox = (ww - vw) / 2; g_oy = (wh - vh) / 2; g_winW = ww; g_winH = wh;
    glViewport(g_ox, g_oy, vw, vh);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, kScreenW, kScreenH, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glEnable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

void drawTexture(const Texture& t, float x, float y, bool flipX, float alpha) {
    if (!t.id) return;
    // Snap to whole pixels so sprites never shimmer.
    x = float(int(x + (x < 0 ? -0.5f : 0.5f)));
    y = float(int(y + (y < 0 ? -0.5f : 0.5f)));
    float u0 = flipX ? 1.f : 0.f, u1 = flipX ? 0.f : 1.f;
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, t.id);
    glColor4f(1, 1, 1, alpha);
    glBegin(GL_QUADS);
    glTexCoord2f(u0, 0); glVertex2f(x, y);
    glTexCoord2f(u1, 0); glVertex2f(x + t.w, y);
    glTexCoord2f(u1, 1); glVertex2f(x + t.w, y + t.h);
    glTexCoord2f(u0, 1); glVertex2f(x, y + t.h);
    glEnd();
}

static void color(uint32_t c);

void drawSprite(const Texture& t, float x, float y, bool flipX, float scale, uint32_t c, bool additive) {
    if (!t.id) return;
    x = float(int(x + (x < 0 ? -0.5f : 0.5f)));
    y = float(int(y + (y < 0 ? -0.5f : 0.5f)));
    float u0 = flipX ? 1.f : 0.f, u1 = flipX ? 0.f : 1.f;
    float w = t.w * scale, h = t.h * scale;
    if (additive) glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, t.id);
    color(c);
    glBegin(GL_QUADS);
    glTexCoord2f(u0, 0); glVertex2f(x, y);
    glTexCoord2f(u1, 0); glVertex2f(x + w, y);
    glTexCoord2f(u1, 1); glVertex2f(x + w, y + h);
    glTexCoord2f(u0, 1); glVertex2f(x, y + h);
    glEnd();
    if (additive) glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

void drawGlow(float cx, float cy, float r, uint32_t c) {
    if (!g_glow.id) {
        Image img;
        img.resize(32, 32);
        for (int y = 0; y < 32; ++y)
            for (int x = 0; x < 32; ++x) {
                float dx = (x + 0.5f) / 16.f - 1.f, dy = (y + 0.5f) / 16.f - 1.f;
                float d = 1.f - std::min(1.f, std::sqrt(dx * dx + dy * dy));
                img.px[size_t(y) * 32 + x] = rgba(255, 255, 255, uint8_t(255 * d * d));
            }
        g_glow = upload(img);
        glBindTexture(GL_TEXTURE_2D, g_glow.id);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    }
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, g_glow.id);
    color(c);
    glBegin(GL_QUADS);
    glTexCoord2f(0, 0); glVertex2f(cx - r, cy - r);
    glTexCoord2f(1, 0); glVertex2f(cx + r, cy - r);
    glTexCoord2f(1, 1); glVertex2f(cx + r, cy + r);
    glTexCoord2f(0, 1); glVertex2f(cx - r, cy + r);
    glEnd();
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

void drawRotated(const Texture& t, float px, float py, float pivX, float pivY, float ang, float scale, bool flipX,
                 uint32_t c) {
    if (!t.id) return;
    const float cs = std::cos(ang), sn = std::sin(ang);
    const float u0 = flipX ? 1.f : 0.f, u1 = flipX ? 0.f : 1.f;
    const float lx[4] = {-pivX, t.w - pivX, t.w - pivX, -pivX};
    const float ly[4] = {-pivY, -pivY, t.h - pivY, t.h - pivY};
    const float u[4] = {u0, u1, u1, u0}, v[4] = {0, 0, 1, 1};
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, t.id);
    color(c);
    glBegin(GL_QUADS);
    for (int i = 0; i < 4; ++i) {
        float x = lx[i] * scale, y = ly[i] * scale;
        glTexCoord2f(u[i], v[i]);
        glVertex2f(px + x * cs - y * sn, py + x * sn + y * cs);
    }
    glEnd();
}

void strokeRect(float x, float y, float w, float h, uint32_t c) {
    fillRect(x, y, w, 1, c);
    fillRect(x, y + h - 1, w, 1, c);
    fillRect(x, y, 1, h, c);
    fillRect(x + w - 1, y, 1, h, c);
}

void toWindow(float vx, float vy, int& wx, int& wy) {
    wx = g_ox + int(vx * g_scale);
    wy = g_oy + int(vy * g_scale);
}

void toVirtual(int wx, int wy, float& vx, float& vy) {
    vx = float(wx - g_ox) / float(g_scale);
    vy = float(wy - g_oy) / float(g_scale);
}

int pixelScale() { return g_scale; }

static void color(uint32_t c) {
    glColor4ub(GLubyte(c & 0xff), GLubyte((c >> 8) & 0xff), GLubyte((c >> 16) & 0xff), GLubyte(c >> 24));
}

void fillRect(float x, float y, float w, float h, uint32_t c) {
    glDisable(GL_TEXTURE_2D);
    color(c);
    glBegin(GL_QUADS);
    glVertex2f(x, y); glVertex2f(x + w, y); glVertex2f(x + w, y + h); glVertex2f(x, y + h);
    glEnd();
}

void initText(void* hdc) {
    HDC dc = static_cast<HDC>(hdc);
    HFONT font = CreateFontA(-15, 0, 0, 0, FW_BOLD, 0, 0, 0, ANSI_CHARSET, OUT_DEFAULT_PRECIS,
                             CLIP_DEFAULT_PRECIS, NONANTIALIASED_QUALITY, FIXED_PITCH, "Consolas");
    HGDIOBJ old = SelectObject(dc, font);
    g_fontBase = glGenLists(96);
    wglUseFontBitmapsA(dc, 32, 96, g_fontBase);
    SelectObject(dc, old);
    DeleteObject(font);
}

void beginHud(int ww, int wh) {
    glViewport(0, 0, ww, wh);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, ww, wh, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
}

void drawText(int x, int y, const char* s, uint32_t c) {
    if (!g_fontBase) return;
    glDisable(GL_TEXTURE_2D);
    // 1px drop shadow for readability over busy backgrounds.
    for (int pass = 0; pass < 2; ++pass) {
        color(pass == 0 ? 0xc0000000u : c);
        glRasterPos2i(x + (pass == 0 ? 1 : 0), y + (pass == 0 ? 1 : 0));
        glListBase(g_fontBase - 32);
        glCallLists(GLsizei(std::strlen(s)), GL_UNSIGNED_BYTE, s);
    }
}

void drawTextCentered(int cx, int y, const char* s, uint32_t c) {
    drawText(cx - int(std::strlen(s)) * kCharW / 2, y, s, c);
}

}  // namespace dw3::gfx
