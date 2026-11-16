#include "ui/ui_draw.h"

#include <windows.h>
#include <GL/gl.h>

#include <algorithm>
#include <cmath>

namespace dw3::ui {

namespace {

bool g_ready = false;
int g_smallAdv[1024];  // trimmed advance for Small-font parts (filled at init)

float snap(float v) { return std::floor(v + 0.5f); }

void setColor(uint32_t c, float alphaMul = 1.f) {
    glColor4ub(GLubyte(c & 255), GLubyte((c >> 8) & 255), GLubyte((c >> 16) & 255),
               GLubyte(std::clamp(float((c >> 24) & 255) * alphaMul, 0.f, 255.f)));
}

void setBlend(Blend b) {
    if (b == Blend::Add) glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    else glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

// Textured quad from atlas rect (sx, sy, sw, sh) to screen (x, y, w, h).
void quad(float sx, float sy, float sw, float sh, float x, float y, float w, float h, bool flipX = false) {
    const Assets& a = assets();
    const float iw = 1.f / float(a.texW()), ih = 1.f / float(a.texH());
    float u0 = sx * iw, u1 = (sx + sw) * iw;
    if (flipX) std::swap(u0, u1);
    const float v0 = sy * ih, v1 = (sy + sh) * ih;
    glTexCoord2f(u0, v0); glVertex2f(x, y);
    glTexCoord2f(u1, v0); glVertex2f(x + w, y);
    glTexCoord2f(u1, v1); glVertex2f(x + w, y + h);
    glTexCoord2f(u0, v1); glVertex2f(x, y + h);
}

void bindAtlas() {
    glEnable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBindTexture(GL_TEXTURE_2D, assets().texture());
}

const AtlasRect* partRect(Sheet s, int part) {
    const SheetData& sd = assets().sheet(s);
    if (!sd.ok || part < 0 || part >= int(sd.partRect.size()) || sd.partRect[size_t(part)].w == 0) return nullptr;
    return &sd.partRect[size_t(part)];
}

int advance(UiFont f, int part) {
    if (part < 0) return f == UiFont::Small ? 4 : f == UiFont::Orange ? 6 : 8;
    if (f == UiFont::Small && part < 1024 && g_smallAdv[part] > 0) return g_smallAdv[part];
    const AtlasRect* r = partRect(Sheet::Font, part);
    return r ? r->w : 8;
}

}  // namespace

bool init(const std::filesystem::path& dataRoot, std::string* err) {
    g_ready = false;
    if (!assets().load(dataRoot, err)) return false;
    if (!assets().upload()) return false;
    // Small HUD font: glyph cells are 8 wide but drawn ~5-6; advance = visible width + 1 (hypothesis).
    std::fill(std::begin(g_smallAdv), std::end(g_smallAdv), 0);
    const SheetData& fs = assets().sheet(Sheet::Font);
    for (int p = 713; p <= 740 && p < int(fs.cells.parts.size()); ++p) {
        Image img;
        if (!renderUiPart(fs.tex, fs.cells.parts[size_t(p)], img)) continue;
        int right = 0;
        for (int y = 0; y < img.h; ++y)
            for (int x = 0; x < img.w; ++x)
                if (img.px[size_t(y) * img.w + x] >> 24) right = std::max(right, x + 1);
        g_smallAdv[p] = std::max(3, right + 1);
    }
    g_ready = true;
    return true;
}

bool ready() { return g_ready && assets().uploaded(); }

void shutdown() {
    assets().release();
    g_ready = false;
}

void fillRect(float x, float y, float w, float h, uint32_t c) {
    glDisable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    setBlend(Blend::Normal);
    setColor(c);
    glBegin(GL_QUADS);
    glVertex2f(x, y); glVertex2f(x + w, y); glVertex2f(x + w, y + h); glVertex2f(x, y + h);
    glEnd();
    glEnable(GL_TEXTURE_2D);
}

void drawPart(Sheet s, int part, float x, float y, uint32_t color, Blend b, bool flipX) {
    if (!ready()) return;
    const AtlasRect* r = partRect(s, part);
    if (!r) return;
    bindAtlas();
    setBlend(b);
    setColor(color, b == Blend::Half ? 0.5f : 1.f);
    glBegin(GL_QUADS);
    quad(float(r->x), float(r->y), float(r->w), float(r->h), snap(x), snap(y), float(r->w), float(r->h), flipX);
    glEnd();
    setBlend(Blend::Normal);
}

void drawPiece(Sheet s, int frameId, float x, float y, uint32_t color) {
    if (!ready()) return;
    const SheetData& sd = assets().sheet(s);
    if (!sd.ok) return;
    const int pi = sd.cells.pieceForFrame(frameId);
    if (pi < 0) return;
    const UiPiece& pc = sd.cells.pieces[size_t(pi)];
    const Blend b = pc.flag == 1 ? Blend::Half : (pc.flag == 2 || pc.flag == 3) ? Blend::Add : Blend::Normal;
    for (const UiLayer& l : pc.layers) drawPart(s, l.part, x + l.dx, y + l.dy, color, b);
}

bool pieceBounds(Sheet s, int frameId, int& x0, int& y0, int& w, int& h) {
    const SheetData& sd = assets().sheet(s);
    if (!sd.ok) return false;
    const int pi = sd.cells.pieceForFrame(frameId);
    if (pi < 0) return false;
    int ax = 1 << 30, ay = 1 << 30, bx = -(1 << 30), by = -(1 << 30);
    for (const UiLayer& l : sd.cells.pieces[size_t(pi)].layers) {
        const UiPart& p = sd.cells.parts[l.part];
        ax = std::min(ax, int(l.dx)); ay = std::min(ay, int(l.dy));
        bx = std::max(bx, l.dx + int(p.w)); by = std::max(by, l.dy + int(p.h));
    }
    if (bx < ax) return false;
    x0 = ax; y0 = ay; w = bx - ax; h = by - ay;
    return true;
}

void drawWindow(float x, float y, float w, float h, WindowStyle style, float alpha) {
    if (!ready()) return;
    const AtlasRect& r = assets().extra(style == WindowStyle::Menu ? Extra::MenuWindow : Extra::BattleWindow);
    if (r.w == 0) return;
    // insets (source pixels kept unscaled): left, right, top, bottom
    int L, R, T, B;
    if (style == WindowStyle::Menu) { L = 16; R = 16; T = 6; B = 6; }
    else { L = 6; R = 12; T = 12; B = 10; }
    x = snap(x); y = snap(y); w = std::max(float(L + R), snap(w)); h = std::max(float(T + B), snap(h));
    const float sx[4] = {float(r.x), float(r.x + L), float(r.x + r.w - R), float(r.x + r.w)};
    const float sy[4] = {float(r.y), float(r.y + T), float(r.y + r.h - B), float(r.y + r.h)};
    const float dx[4] = {x, x + L, x + w - R, x + w};
    const float dy[4] = {y, y + T, y + h - B, y + h};
    bindAtlas();
    setBlend(Blend::Normal);
    setColor(kWhite, alpha);
    glBegin(GL_QUADS);
    for (int j = 0; j < 3; ++j)
        for (int i = 0; i < 3; ++i)
            quad(sx[i], sy[j], sx[i + 1] - sx[i], sy[j + 1] - sy[j], dx[i], dy[j], dx[i + 1] - dx[i], dy[j + 1] - dy[j]);
    glEnd();
}

int lineHeight(UiFont f) { return f == UiFont::White ? 14 : f == UiFont::Orange ? 10 : 9; }

namespace {

// Shared walker for ASCII text: draw = false only measures.
int walkText(float x, float y, const std::string& s, uint32_t color, UiFont f, bool draw) {
    float cx = x, cy = y;
    int widest = 0;
    if (draw) {
        bindAtlas();
        setBlend(Blend::Normal);
        setColor(color);
        glBegin(GL_QUADS);
    }
    for (char ch : s) {
        if (ch == '\n') {
            widest = std::max(widest, int(cx - x));
            cx = x;
            cy += float(lineHeight(f));
            continue;
        }
        const int part = uiGlyphPart(f, ch);
        const int adv = ch == ' ' && f == UiFont::Small ? 4 : advance(f, part);
        if (draw && part >= 0 && ch != ' ')
            if (const AtlasRect* r = partRect(Sheet::Font, part))
                quad(float(r->x), float(r->y), float(r->w), float(r->h), snap(cx), snap(cy), float(r->w), float(r->h));
        cx += float(adv);
    }
    if (draw) glEnd();
    return std::max(widest, int(cx - x));
}

// Encoded walker. Emits glyph parts; args are ASCII.
int walkEncoded(float x, float y, const uint8_t* b, size_t n, uint32_t color, UiFont f, const std::vector<std::string>& args,
                bool draw, int* height) {
    float cx = x, cy = y;
    int widest = 0;
    if (draw) {
        bindAtlas();
        setBlend(Blend::Normal);
        setColor(color);
        glBegin(GL_QUADS);
    }
    auto glyph = [&](int part) {
        const int adv = advance(f, part);
        if (draw && part >= 0)
            if (const AtlasRect* r = partRect(Sheet::Font, part))
                quad(float(r->x), float(r->y), float(r->w), float(r->h), snap(cx), snap(cy), float(r->w), float(r->h));
        cx += float(adv);
    };
    for (size_t i = 0; i < n;) {
        const uint8_t c = b[i];
        if (c == 0) break;
        if (c == 1 && i + 1 < n) {
            const uint8_t sym = b[i + 1];
            if (sym == 1) cx += float(f == UiFont::Small ? 4 : advance(f, uiGlyphPartForCode(f, 1, true)));
            else glyph(uiGlyphPartForCode(f, sym, true));
            i += 2;
            continue;
        }
        if (c == 2 && i + 1 < n) {
            const uint8_t k = b[i + 1];
            if (k == 0) break;
            if (k == 1) {
                widest = std::max(widest, int(cx - x));
                cx = x;
                cy += float(lineHeight(f));
                i += 2;
                continue;
            }
            if (k == 5 && i + 2 < n) {
                const size_t a = size_t(b[i + 2]) - 1;
                if (a < args.size())
                    for (char ch : args[a]) {
                        if (ch == ' ') cx += float(advance(f, uiGlyphPartForCode(f, 1, true)));
                        else glyph(uiGlyphPart(f, ch));
                    }
                i += 3;
                continue;
            }
            i += 2;  // 02 07 speaker tag, 02 02 wait, others: not drawn
            continue;
        }
        if (c == 3) { ++i; continue; }
        glyph(uiGlyphPartForCode(f, c, false));
        ++i;
    }
    if (draw) glEnd();
    if (height) *height = int(cy - y) + lineHeight(f);
    return std::max(widest, int(cx - x));
}

}  // namespace

int drawText(float x, float y, const std::string& s, uint32_t color, UiFont f) {
    if (!ready()) return 0;
    return walkText(x, y, s, color, f, true);
}

int measureText(const std::string& s, UiFont f) {
    if (!ready()) return int(s.size()) * 8;
    return walkText(0, 0, s, 0, f, false);
}

void drawTextRight(float x, float y, const std::string& s, uint32_t color, UiFont f) {
    drawText(x - float(measureText(s, f)), y, s, color, f);
}

int drawEncoded(float x, float y, const uint8_t* b, size_t n, uint32_t color, UiFont f, const std::vector<std::string>& args) {
    if (!ready() || !b) return 0;
    int h = 0;
    walkEncoded(x, y, b, n, color, f, args, true, &h);
    return h;
}

int measureEncoded(const uint8_t* b, size_t n, UiFont f, const std::vector<std::string>& args) {
    if (!ready() || !b) return 0;
    return walkEncoded(0, 0, b, n, 0, f, args, false, nullptr);
}

void drawBar(float x, float y, float w, float h, float frac, BarColor c) {
    frac = std::clamp(frac, 0.f, 1.f);
    const float fw = std::floor(w * frac + 0.5f);
    if (fw <= 0) return;
    x = snap(x); y = snap(y);
    const uint32_t dark = assets().loaded() ? assets().barColor(c, false) : 0xff808080u;
    const uint32_t light = assets().loaded() ? assets().barColor(c, true) : 0xffc0c0c0u;
    fillRect(x, y, fw, h, dark);
    fillRect(x, y, fw, std::max(1.f, std::floor(h / 3.f)), light);
}

void drawCursor(float x, float y, int ticks) {
    // '>' pointer = white font part 111 (filled triangle) in yellow; nudges 1 px every 16 ticks like a pulse.
    const float dx = ((ticks / 16) & 1) ? 1.f : 0.f;
    drawPart(Sheet::Font, 111, x + dx, y, kYellow);
}

void drawPrompt(float x, float y, int ticks) {
    if ((ticks / 20) & 1) return;  // blink
    drawPart(Sheet::Font, 42, x, y);
}

}  // namespace dw3::ui
