#include "render/gl_renderer.h"

#include <windows.h>
#include <GL/gl.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

// GL 1.2+ / framebuffer-object constants (not in the Windows SDK's GL 1.1 header).
#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif
#define DW3_GL_FRAMEBUFFER 0x8D40
#define DW3_GL_READ_FRAMEBUFFER 0x8CA8
#define DW3_GL_DRAW_FRAMEBUFFER 0x8CA9
#define DW3_GL_RENDERBUFFER 0x8D41
#define DW3_GL_COLOR_ATTACHMENT0 0x8CE0
#define DW3_GL_DEPTH_ATTACHMENT 0x8D00
#define DW3_GL_FRAMEBUFFER_COMPLETE 0x8CD5
#define DW3_GL_DEPTH_COMPONENT24 0x81A6
#define DW3_GL_RGBA8 0x8058
#define DW3_GL_MAX_SAMPLES 0x8D57
#define DW3_GL_MAX_RENDERBUFFER_SIZE 0x84E8
#define DW3_GL_MULTISAMPLE 0x809D

namespace dw3::gfx {

namespace {

// ---- framebuffer-object entry points (ARB_framebuffer_object / GL 3.0, or EXT_framebuffer_object) ----
using GenFn = void(APIENTRY*)(GLsizei, GLuint*);
using DelFn = void(APIENTRY*)(GLsizei, const GLuint*);
using BindFn = void(APIENTRY*)(GLenum, GLuint);
using FbTexFn = void(APIENTRY*)(GLenum, GLenum, GLenum, GLuint, GLint);
using FbRbFn = void(APIENTRY*)(GLenum, GLenum, GLenum, GLuint);
using StatusFn = GLenum(APIENTRY*)(GLenum);
using RbStorageFn = void(APIENTRY*)(GLenum, GLenum, GLsizei, GLsizei);
using RbStorageMsFn = void(APIENTRY*)(GLenum, GLsizei, GLenum, GLsizei, GLsizei);
using BlitFn = void(APIENTRY*)(GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLbitfield, GLenum);

struct FboApi {
    GenFn genFramebuffers = nullptr, genRenderbuffers = nullptr;
    DelFn deleteFramebuffers = nullptr, deleteRenderbuffers = nullptr;
    BindFn bindFramebuffer = nullptr, bindRenderbuffer = nullptr;
    FbTexFn framebufferTexture2D = nullptr;
    FbRbFn framebufferRenderbuffer = nullptr;
    StatusFn checkFramebufferStatus = nullptr;
    RbStorageFn renderbufferStorage = nullptr;
    RbStorageMsFn renderbufferStorageMultisample = nullptr;  // optional (MSAA)
    BlitFn blitFramebuffer = nullptr;                        // optional (MSAA resolve)
    bool ok = false;
} g_api;

template <class F>
F proc(const char* core, const char* ext) {
    PROC p = wglGetProcAddress(core);
    if (!p && ext) p = wglGetProcAddress(ext);
    return reinterpret_cast<F>(reinterpret_cast<void*>(p));
}

bool g_apiTried = false;
int g_maxSamples = 0, g_maxSize = 4096;

void loadApi() {
    if (g_apiTried) return;
    g_apiTried = true;
    FboApi& a = g_api;
    a.genFramebuffers = proc<GenFn>("glGenFramebuffers", "glGenFramebuffersEXT");
    a.deleteFramebuffers = proc<DelFn>("glDeleteFramebuffers", "glDeleteFramebuffersEXT");
    a.bindFramebuffer = proc<BindFn>("glBindFramebuffer", "glBindFramebufferEXT");
    a.framebufferTexture2D = proc<FbTexFn>("glFramebufferTexture2D", "glFramebufferTexture2DEXT");
    a.framebufferRenderbuffer = proc<FbRbFn>("glFramebufferRenderbuffer", "glFramebufferRenderbufferEXT");
    a.checkFramebufferStatus = proc<StatusFn>("glCheckFramebufferStatus", "glCheckFramebufferStatusEXT");
    a.genRenderbuffers = proc<GenFn>("glGenRenderbuffers", "glGenRenderbuffersEXT");
    a.deleteRenderbuffers = proc<DelFn>("glDeleteRenderbuffers", "glDeleteRenderbuffersEXT");
    a.bindRenderbuffer = proc<BindFn>("glBindRenderbuffer", "glBindRenderbufferEXT");
    a.renderbufferStorage = proc<RbStorageFn>("glRenderbufferStorage", "glRenderbufferStorageEXT");
    a.renderbufferStorageMultisample =
        proc<RbStorageMsFn>("glRenderbufferStorageMultisample", "glRenderbufferStorageMultisampleEXT");
    a.blitFramebuffer = proc<BlitFn>("glBlitFramebuffer", "glBlitFramebufferEXT");
    a.ok = a.genFramebuffers && a.deleteFramebuffers && a.bindFramebuffer && a.framebufferTexture2D &&
           a.framebufferRenderbuffer && a.checkFramebufferStatus && a.genRenderbuffers && a.deleteRenderbuffers &&
           a.bindRenderbuffer && a.renderbufferStorage;
    GLint v = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &v);
    g_maxSize = std::max(1024, int(v));
    if (a.ok) {
        v = 0;
        glGetIntegerv(DW3_GL_MAX_RENDERBUFFER_SIZE, &v);
        if (v > 0) g_maxSize = std::min(g_maxSize, int(v));
    }
    if (a.ok && a.renderbufferStorageMultisample && a.blitFramebuffer) {
        v = 0;
        glGetIntegerv(DW3_GL_MAX_SAMPLES, &v);
        g_maxSamples = std::clamp(int(v), 0, 8);
    }
    while (glGetError() != GL_NO_ERROR) {}
}

// One render target: a colour texture (or a multisampled colour renderbuffer) + optional depth renderbuffer.
struct Target {
    GLuint fb = 0, tex = 0, colorRb = 0, depthRb = 0;
    int w = 0, h = 0, samples = 0;
    bool depth = false;

    void release() {
        if (fb) g_api.deleteFramebuffers(1, &fb);
        if (colorRb) g_api.deleteRenderbuffers(1, &colorRb);
        if (depthRb) g_api.deleteRenderbuffers(1, &depthRb);
        if (tex) glDeleteTextures(1, &tex);
        *this = {};
    }
    // (Re)creates the target when its size / samples change. False = not usable.
    bool ensure(int W, int H, int S, bool withDepth) {
        if (fb && w == W && h == H && samples == S && depth == withDepth) return true;
        release();
        w = W; h = H; samples = S; depth = withDepth;
        g_api.genFramebuffers(1, &fb);
        g_api.bindFramebuffer(DW3_GL_FRAMEBUFFER, fb);
        if (S > 0) {
            g_api.genRenderbuffers(1, &colorRb);
            g_api.bindRenderbuffer(DW3_GL_RENDERBUFFER, colorRb);
            g_api.renderbufferStorageMultisample(DW3_GL_RENDERBUFFER, S, DW3_GL_RGBA8, W, H);
            g_api.framebufferRenderbuffer(DW3_GL_FRAMEBUFFER, DW3_GL_COLOR_ATTACHMENT0, DW3_GL_RENDERBUFFER, colorRb);
        } else {
            glGenTextures(1, &tex);
            glBindTexture(GL_TEXTURE_2D, tex);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glTexImage2D(GL_TEXTURE_2D, 0, DW3_GL_RGBA8, W, H, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
            g_api.framebufferTexture2D(DW3_GL_FRAMEBUFFER, DW3_GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
        }
        if (withDepth) {
            g_api.genRenderbuffers(1, &depthRb);
            g_api.bindRenderbuffer(DW3_GL_RENDERBUFFER, depthRb);
            if (S > 0) g_api.renderbufferStorageMultisample(DW3_GL_RENDERBUFFER, S, DW3_GL_DEPTH_COMPONENT24, W, H);
            else g_api.renderbufferStorage(DW3_GL_RENDERBUFFER, DW3_GL_DEPTH_COMPONENT24, W, H);
            g_api.framebufferRenderbuffer(DW3_GL_FRAMEBUFFER, DW3_GL_DEPTH_ATTACHMENT, DW3_GL_RENDERBUFFER, depthRb);
        }
        const bool complete = g_api.checkFramebufferStatus(DW3_GL_FRAMEBUFFER) == DW3_GL_FRAMEBUFFER_COMPLETE;
        g_api.bindFramebuffer(DW3_GL_FRAMEBUFFER, 0);
        while (glGetError() != GL_NO_ERROR) {}
        if (!complete) release();
        return complete;
    }
};

// Frame layout: where the virtual screen lands in the window and how big the scene target is.
struct Layout {
    float vl = 0, vr = kScreenW;      // visible virtual x range
    int destX = 0, destY = 0, destW = kScreenW, destH = kScreenH;  // window rect (top-left origin)
    float sx = 1, sy = 1;             // window pixels per virtual pixel
    int R = 1;                        // render scale of the scene target
    int fbW = kScreenW, fbH = kScreenH;
};

VideoOptions g_opt;
Layout g_lay;
int g_winW = kScreenW, g_winH = kScreenH;
bool g_useFbo = false;      // this frame renders into the scene target
bool g_hud = false;         // in window-pixel (HUD) mode
bool g_frameOpen = false;
bool g_offscreen = false;
Target g_scene, g_msaa, g_prescale, g_present;
Texture g_glow, g_scan, g_vignette;
unsigned g_fontBase = 0;

struct HudText { int x, y; uint32_t c; std::string s; };
std::vector<HudText> g_text;

constexpr float kMaxWide = 576.f;  // widest virtual view (2.4:1); wider windows get side bars

void color(uint32_t c) {
    glColor4ub(GLubyte(c & 0xff), GLubyte((c >> 8) & 0xff), GLubyte((c >> 16) & 0xff), GLubyte(c >> 24));
}

void computeLayout(int ww, int wh, bool wideScene) {
    Layout L;
    const bool wide = wideScene && g_opt.wide;
    const float fx = float(ww) / kScreenW, fy = float(wh) / kScreenH;
    float vw = kScreenW;
    ScaleMode mode = g_opt.scale;
    if (mode == ScaleMode::Integer && std::min(fx, fy) < 1.f) mode = ScaleMode::Fit;  // tiny window
    if (mode == ScaleMode::Stretch) {
        L.sx = fx; L.sy = fy;
        L.destX = 0; L.destY = 0; L.destW = ww; L.destH = wh;
        L.R = g_opt.hiRes ? int(std::ceil(std::max(fx, fy) - 1e-3f)) : 1;
    } else if (mode == ScaleMode::Integer) {
        const int s = int(std::floor(std::min(fx, fy) + 1e-4f));
        if (wide) vw = std::clamp(std::floor(float(ww) / float(s)), float(kScreenW), kMaxWide);
        L.sx = L.sy = float(s);
        L.destW = int(vw) * s; L.destH = kScreenH * s;
        L.destX = (ww - L.destW) / 2; L.destY = (wh - L.destH) / 2;
        L.R = g_opt.hiRes ? s : 1;
    } else {
        const float s = std::min(fx, fy);
        if (wide) vw = std::clamp(float(ww) / s, float(kScreenW), kMaxWide);
        L.sx = L.sy = s;
        L.destW = std::max(1, int(std::lround(vw * s))); L.destH = std::max(1, int(std::lround(kScreenH * s)));
        L.destX = (ww - L.destW) / 2; L.destY = (wh - L.destH) / 2;
        L.R = g_opt.hiRes ? int(std::ceil(s - 1e-3f)) : 1;
    }
    L.R = std::clamp(L.R, 1, 8);
    while (L.R > 1 && (vw * float(L.R) > float(g_maxSize) || kScreenH * L.R > g_maxSize)) --L.R;
    L.fbW = std::max(1, int(std::lround(vw * float(L.R))));
    L.fbH = kScreenH * L.R;
    vw = float(L.fbW) / float(L.R);  // exact: virtual width = target width / R
    L.vl = -(vw - kScreenW) * 0.5f;
    L.vr = L.vl + vw;
    g_lay = L;
}

void bindTarget(const Target& t) { g_api.bindFramebuffer(DW3_GL_FRAMEBUFFER, t.fb); }

// Current draw target for the scene (MSAA renderbuffer or the plain scene texture).
const Target& sceneDraw() { return g_msaa.fb ? g_msaa : g_scene; }

void virtualProjection() {
    if (g_useFbo) {
        glViewport(0, 0, g_lay.fbW, g_lay.fbH);
    } else {
        glViewport(g_lay.destX, g_winH - g_lay.destY - g_lay.destH, g_lay.destW, g_lay.destH);
    }
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(g_lay.vl, g_lay.vr, kScreenH, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glEnable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

void windowProjection(int ww, int wh) {
    glViewport(0, 0, ww, wh);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, ww, wh, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
}

void texQuad(unsigned tex, float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1) {
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, tex);
    glBegin(GL_QUADS);
    glTexCoord2f(u0, v0); glVertex2f(x0, y0);
    glTexCoord2f(u1, v0); glVertex2f(x1, y0);
    glTexCoord2f(u1, v1); glVertex2f(x1, y1);
    glTexCoord2f(u0, v1); glVertex2f(x0, y1);
    glEnd();
}

void setFilter(unsigned tex, bool linear) {
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, linear ? GL_LINEAR : GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, linear ? GL_LINEAR : GL_NEAREST);
}

Texture makeTexture(const Image& img, bool linear, bool repeat) {
    Texture t = upload(img);
    glBindTexture(GL_TEXTURE_2D, t.id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, linear ? GL_LINEAR : GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, linear ? GL_LINEAR : GL_NEAREST);
    if (repeat) {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    }
    return t;
}

// CRT look: one soft dark line per virtual row + a gentle vignette, multiplied over the composed scene.
void drawCrt() {
    if (!g_scan.id) {
        Image img;
        img.resize(1, 4);
        const uint8_t k[4] = {255, 236, 150, 236};
        for (int y = 0; y < 4; ++y) img.px[size_t(y)] = rgba(k[y], k[y], k[y], 255);
        g_scan = makeTexture(img, true, true);
    }
    if (!g_vignette.id) {
        Image img;
        img.resize(64, 64);
        for (int y = 0; y < 64; ++y)
            for (int x = 0; x < 64; ++x) {
                const float dx = (x + 0.5f) / 32.f - 1.f, dy = (y + 0.5f) / 32.f - 1.f;
                const float d = std::min(1.f, std::sqrt(dx * dx * 0.7f + dy * dy));
                const uint8_t v = uint8_t(255.f * (1.f - 0.28f * d * d * d));
                img.px[size_t(y) * 64 + x] = rgba(v, v, v, 255);
            }
        g_vignette = makeTexture(img, true, false);
    }
    const float x0 = float(g_lay.destX), y0 = float(g_lay.destY);
    const float x1 = x0 + float(g_lay.destW), y1 = y0 + float(g_lay.destH);
    glEnable(GL_BLEND);
    glBlendFunc(GL_DST_COLOR, GL_ZERO);
    glColor4f(1, 1, 1, 1);
    if (g_lay.sy >= 2.f) texQuad(g_scan.id, x0, y0, x1, y1, 0, 0, 1, float(kScreenH));  // lines only when visible
    texQuad(g_vignette.id, x0, y0, x1, y1, 0, 0, 1, 1);
    // a little brightness back (scanlines darken the picture)
    glBlendFunc(GL_DST_COLOR, GL_ONE);
    glDisable(GL_TEXTURE_2D);
    glColor4ub(30, 30, 30, 255);
    glBegin(GL_QUADS);
    glVertex2f(x0, y0); glVertex2f(x1, y0); glVertex2f(x1, y1); glVertex2f(x0, y1);
    glEnd();
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

void drawTextNow(int x, int y, const char* s, uint32_t c) {
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

}  // namespace

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

void setVideoOptions(const VideoOptions& o) {
    g_opt = o;
    g_opt.msaa = o.msaa >= 8 ? 8 : o.msaa >= 4 ? 4 : o.msaa >= 2 ? 2 : 0;
}
const VideoOptions& videoOptions() { return g_opt; }
int maxMsaa() { return g_maxSamples; }
void setOffscreenPresent(bool on) { g_offscreen = on; }
float viewLeft() { return g_lay.vl; }
float viewRight() { return g_lay.vr; }
void clearHudText() { g_text.clear(); }

void beginFrame(int ww, int wh, bool wideScene) {
    loadApi();
    g_winW = std::max(1, ww);
    g_winH = std::max(1, wh);
    computeLayout(g_winW, g_winH, wideScene);
    g_text.clear();
    g_hud = false;
    g_frameOpen = true;
    g_useFbo = g_api.ok && g_scene.ensure(g_lay.fbW, g_lay.fbH, 0, true);
    if (g_useFbo) {
        const int S = std::min(g_opt.msaa, g_maxSamples);
        if (S > 0) { if (!g_msaa.ensure(g_lay.fbW, g_lay.fbH, S, true)) g_msaa.release(); }
        else if (g_msaa.fb) g_msaa.release();
        bindTarget(sceneDraw());
        if (g_msaa.fb) glEnable(DW3_GL_MULTISAMPLE);
        glViewport(0, 0, g_lay.fbW, g_lay.fbH);
        glClearColor(0.f, 0.f, 0.f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    } else {
        if (g_api.ok) g_api.bindFramebuffer(DW3_GL_FRAMEBUFFER, 0);
        glViewport(0, 0, g_winW, g_winH);
        glClearColor(0.f, 0.f, 0.f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    }
    virtualProjection();
}

void endFrame() {
    if (!g_frameOpen) return;
    g_frameOpen = false;
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_ALPHA_TEST);
    glColor4f(1, 1, 1, 1);
    if (g_useFbo) {
        // MSAA: resolve into the plain scene texture.
        if (g_msaa.fb) {
            g_api.bindFramebuffer(DW3_GL_READ_FRAMEBUFFER, g_msaa.fb);
            g_api.bindFramebuffer(DW3_GL_DRAW_FRAMEBUFFER, g_scene.fb);
            g_api.blitFramebuffer(0, 0, g_lay.fbW, g_lay.fbH, 0, 0, g_lay.fbW, g_lay.fbH, GL_COLOR_BUFFER_BIT, GL_NEAREST);
            g_api.bindFramebuffer(DW3_GL_FRAMEBUFFER, 0);
        }
        unsigned src = g_scene.tex;
        int srcW = g_lay.fbW, srcH = g_lay.fbH;
        glDisable(GL_BLEND);
        // Original resolution + sharp filter: first an integer nearest upscale, so the final linear step only
        // softens the pixel borders (sharp-bilinear).
        const float s = std::max(g_lay.sx, g_lay.sy);
        if (g_opt.sharp && g_lay.R == 1 && s > 1.01f && (srcW != g_lay.destW || srcH != g_lay.destH)) {
            const int K = std::min(8, int(std::ceil(s - 1e-3f)));
            if (g_prescale.ensure(srcW * K, srcH * K, 0, false)) {
                bindTarget(g_prescale);
                glViewport(0, 0, srcW * K, srcH * K);
                glMatrixMode(GL_PROJECTION);
                glLoadIdentity();
                glOrtho(0, 1, 0, 1, -1, 1);
                glMatrixMode(GL_MODELVIEW);
                glLoadIdentity();
                setFilter(src, false);
                texQuad(src, 0, 0, 1, 1, 0, 0, 1, 1);
                src = g_prescale.tex;
                srcW *= K;
                srcH *= K;
            }
        }
        if (g_offscreen && g_present.ensure(g_winW, g_winH, 0, false)) bindTarget(g_present);
        else g_api.bindFramebuffer(DW3_GL_FRAMEBUFFER, 0);
        windowProjection(g_winW, g_winH);
        glClearColor(0.f, 0.f, 0.f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT);
        const bool exact = srcW == g_lay.destW && srcH == g_lay.destH;
        setFilter(src, !exact && g_opt.sharp);
        glColor4f(1, 1, 1, 1);
        // texture row 0 = bottom of the target
        texQuad(src, float(g_lay.destX), float(g_lay.destY), float(g_lay.destX + g_lay.destW),
                float(g_lay.destY + g_lay.destH), 0, 1, 1, 0);
        glEnable(GL_BLEND);
    } else {
        windowProjection(g_winW, g_winH);
        glEnable(GL_BLEND);
    }
    if (g_opt.crt) drawCrt();
    // HUD text in real window pixels (queued while drawing the frame).
    windowProjection(g_winW, g_winH);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    for (const auto& t : g_text) drawTextNow(t.x, t.y, t.s.c_str(), t.c);
    g_text.clear();
    glEnable(GL_TEXTURE_2D);
    glColor4f(1, 1, 1, 1);
}

bool readFrame(Image& out) {
    out.resize(g_winW, g_winH);
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    glReadPixels(0, 0, g_winW, g_winH, GL_RGBA, GL_UNSIGNED_BYTE, out.px.data());
    for (int y = 0; y < g_winH / 2; ++y)  // GL origin is bottom-left
        std::swap_ranges(out.px.begin() + size_t(y) * g_winW, out.px.begin() + size_t(y + 1) * g_winW,
                         out.px.begin() + size_t(g_winH - 1 - y) * g_winW);
    for (auto& c : out.px) c |= 0xff000000u;
    return true;
}

void shutdown() {
    if (g_api.ok) {
        g_api.bindFramebuffer(DW3_GL_FRAMEBUFFER, 0);
        g_scene.release();
        g_msaa.release();
        g_prescale.release();
        g_present.release();
    }
    destroy(g_glow);
    destroy(g_scan);
    destroy(g_vignette);
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
        g_glow = makeTexture(img, true, false);
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
    wx = g_lay.destX + int(std::floor((vx - g_lay.vl) * g_lay.sx));
    wy = g_lay.destY + int(std::floor(vy * g_lay.sy));
}

void toVirtual(int wx, int wy, float& vx, float& vy) {
    vx = g_lay.vl + float(wx - g_lay.destX) / g_lay.sx;
    vy = float(wy - g_lay.destY) / g_lay.sy;
}

int pixelScale() { return std::max(1, int(std::lround(g_lay.sy))); }

void coverView(float& x, float& w, float y, float h) {
    if (g_hud) return;
    if (x <= 0.5f && x + w >= kScreenW - 0.5f && y <= 0.5f && y + h >= kScreenH - 0.5f) {
        x = std::min(x, g_lay.vl);
        w = std::max(x + w, g_lay.vr) - x;
    }
}

void fillRect(float x, float y, float w, float h, uint32_t c) {
    coverView(x, w, y, h);
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

void beginVirtual() {
    g_hud = false;
    virtualProjection();
}

void beginHud(int ww, int wh) {
    g_hud = true;
    if (g_useFbo) {
        // Window-pixel coordinates, mapped onto the scene target (rects / bars land where they belong; text is
        // queued and drawn in real window pixels by endFrame).
        glViewport(0, 0, g_lay.fbW, g_lay.fbH);
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glOrtho(g_lay.destX, g_lay.destX + g_lay.destW, g_lay.destY + g_lay.destH, g_lay.destY, -1, 1);
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
    } else {
        windowProjection(ww, wh);
    }
}

void drawText(int x, int y, const char* s, uint32_t c) {
    if (!g_fontBase || !s || !*s) return;
    if (g_useFbo && g_frameOpen) g_text.push_back({x, y, c, s});
    else drawTextNow(x, y, s, c);
}

void drawTextCentered(int cx, int y, const char* s, uint32_t c) {
    drawText(cx - int(std::strlen(s)) * kCharW / 2, y, s, c);
}

}  // namespace dw3::gfx
