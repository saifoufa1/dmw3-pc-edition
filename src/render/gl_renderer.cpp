#include "render/gl_renderer.h"

#include <windows.h>
#include <GL/gl.h>

#include <algorithm>
#include <cstring>

#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif

namespace dw3::gfx {

static unsigned g_fontBase = 0;

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
    glViewport((ww - vw) / 2, (wh - vh) / 2, vw, vh);
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

}  // namespace dw3::gfx
