// Win32 + OpenGL platform layer: window, input (keyboard, mouse, XInput), fixed 60 Hz loop.
// Command line:
//   dmw3.exe [--data <dir>] [--stage S200]
//   testing: [--screenshot out.bmp --frames N] [--autowalk] [--battle] [--sword] [--autobattle]
#include <windows.h>
#include <shellapi.h>
#include <GL/gl.h>

#include <string>
#include <vector>

#include "core/bytes.h"
#include "game/game.h"

namespace fs = std::filesystem;
using namespace dw3;

namespace {

int g_winW = 960, g_winH = 720;
bool g_running = true;

// ---- XInput, loaded dynamically so the exe runs even without the DLL. ----
struct XGamepad { WORD buttons; BYTE lt, rt; SHORT lx, ly, rx, ry; };
struct XState { DWORD packet; XGamepad pad; };
using XInputGetStateFn = DWORD(WINAPI*)(DWORD, XState*);
XInputGetStateFn g_xinput = nullptr;

void loadXInput() {
    for (const char* dll : {"xinput1_4.dll", "xinput1_3.dll", "xinput9_1_0.dll"})
        if (HMODULE m = LoadLibraryA(dll)) {
            g_xinput = reinterpret_cast<XInputGetStateFn>(reinterpret_cast<void*>(GetProcAddress(m, "XInputGetState")));
            if (g_xinput) return;
        }
}

LRESULT CALLBACK wndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_SIZE: g_winW = LOWORD(lp); g_winH = HIWORD(lp); return 0;
        case WM_CLOSE: g_running = false; return 0;
        case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcA(h, msg, wp, lp);
}

// Held state of logical buttons; edges are computed against the previous tick.
struct Held {
    bool key[256];
    bool up, down, left, right, run, confirm, atk1, atk2, atk3, guard, digivolve;
    bool next, prev, partner, hud, editor, equip, quit, lmb, rmb;
};

Held poll(HWND hwnd, bool focused) {
    Held h{};
    if (focused) {
        for (int vk = 1; vk < 256; ++vk) h.key[vk] = (GetAsyncKeyState(vk) & 0x8000) != 0;
        auto k = [&](int vk) { return h.key[vk]; };
        h.up = k(VK_UP) || k('W');
        h.down = k(VK_DOWN) || k('S');
        h.left = k(VK_LEFT) || k('A');
        h.right = k(VK_RIGHT) || k('D');
        h.run = k(VK_SHIFT);
        h.confirm = k(VK_SPACE) || k(VK_RETURN);
        h.atk1 = k('J') || k('1');
        h.atk2 = k('K') || k('2');
        h.atk3 = k('L') || k('3');
        h.guard = k(VK_SPACE) || k('4');
        h.digivolve = k('U') || k('5');
        h.next = k(VK_NEXT) || k('E');
        h.prev = k(VK_PRIOR) || k('Q');
        h.partner = k(VK_TAB);
        h.hud = k(VK_F1);
        h.editor = k(VK_F2);
        h.equip = k('R');
        h.quit = k(VK_ESCAPE);
        h.lmb = k(VK_LBUTTON);
        h.rmb = k(VK_RBUTTON);
    }
    (void)hwnd;
    XState xs{};
    if (g_xinput && g_xinput(0, &xs) == 0) {
        const WORD w = xs.pad.buttons;
        const int dz = 12000;
        h.up |= (w & 0x0001) || xs.pad.ly > dz;
        h.down |= (w & 0x0002) || xs.pad.ly < -dz;
        h.left |= (w & 0x0004) || xs.pad.lx < -dz;
        h.right |= (w & 0x0008) || xs.pad.lx > dz;
        h.run |= (w & 0x1000) != 0;                // A
        h.confirm |= (w & 0x4000) != 0;            // X
        h.atk1 |= (w & 0x4000) != 0;               // X
        h.atk2 |= (w & 0x2000) != 0;               // B
        h.atk3 |= (w & 0x8000) != 0;               // Y
        h.guard |= (w & 0x0100) != 0;              // LB
        h.prev |= (w & 0x0100) != 0;
        h.digivolve |= (w & 0x0200) != 0;          // RB
        h.next |= (w & 0x0200) != 0;
        h.hud |= (w & 0x0020) != 0;                // Back
        h.equip |= (w & 0x0010) != 0;              // Start
    }
    return h;
}

bool saveScreenshot(const fs::path& p) {
    Image img;
    img.resize(g_winW, g_winH);
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    glReadPixels(0, 0, g_winW, g_winH, GL_RGBA, GL_UNSIGNED_BYTE, img.px.data());
    for (int y = 0; y < g_winH / 2; ++y)  // GL origin is bottom-left
        std::swap_ranges(img.px.begin() + size_t(y) * g_winW, img.px.begin() + size_t(y + 1) * g_winW,
                         img.px.begin() + size_t(g_winH - 1 - y) * g_winW);
    for (auto& c : img.px) c |= 0xff000000u;
    return writeBmp(p, img);
}

std::vector<std::string> args() {
    std::vector<std::string> out;
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    for (int i = 1; i < argc; ++i) {
        int n = WideCharToMultiByte(CP_UTF8, 0, argv[i], -1, nullptr, 0, nullptr, nullptr);
        std::string s(size_t(n > 0 ? n - 1 : 0), '\0');
        WideCharToMultiByte(CP_UTF8, 0, argv[i], -1, s.data(), n, nullptr, nullptr);
        out.push_back(s);
    }
    LocalFree(argv);
    return out;
}

// Our own project data (stage files) lives in <repo>/data/stages; find it by walking up from the exe.
fs::path findStageDir(const fs::path& exeDir) {
    std::error_code ec;
    fs::path p = exeDir;
    for (int i = 0; i < 5 && !p.empty(); ++i) {
        if (fs::is_directory(p / "data" / "stages", ec)) return p / "data" / "stages";
        fs::path parent = p.parent_path();
        if (parent == p) break;
        p = parent;
    }
    return exeDir / "data" / "stages";
}

}  // namespace

int WINAPI WinMain(HINSTANCE inst, HINSTANCE, LPSTR, int) {
    std::string dataArg, stage = "S200", shotPath;
    int shotFrames = 0;
    bool autowalk = false, autobattle = false;
    DebugOptions dbg;
    auto a = args();
    for (size_t i = 0; i < a.size(); ++i) {
        if (a[i] == "--data" && i + 1 < a.size()) dataArg = a[++i];
        else if (a[i] == "--stage" && i + 1 < a.size()) stage = a[++i];
        else if (a[i] == "--screenshot" && i + 1 < a.size()) shotPath = a[++i];
        else if (a[i] == "--frames" && i + 1 < a.size()) shotFrames = std::stoi(a[++i]);
        else if (a[i] == "--autowalk") autowalk = true;
        else if (a[i] == "--autobattle") autobattle = true;
        else if (a[i] == "--battle") dbg.startBattle = true;
        else if (a[i] == "--boss" && i + 1 < a.size()) { dbg.startBattle = true; dbg.battleSprite = a[++i]; }
        else if (a[i] == "--sword") dbg.giveSword = true;
    }
    if (!shotPath.empty() && shotFrames <= 0) shotFrames = 120;

    char exe[MAX_PATH];
    GetModuleFileNameA(nullptr, exe, MAX_PATH);
    const fs::path exeDir = fs::path(exe).parent_path();
    fs::path root = findDataRoot(dataArg, exeDir);
    if (root.empty()) {
        MessageBoxA(nullptr,
                    "Game data not found.\n\nPut your extracted Digimon World 3 disc (the folder containing DAT\\) "
                    "next to the game as 'game_data', set DW3_DATA, or pass --data <folder>.",
                    "DMW3 PC Edition", MB_ICONERROR);
        return 1;
    }

    WNDCLASSA wc{};
    wc.style = CS_OWNDC;
    wc.lpfnWndProc = wndProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = "DMW3PC";
    RegisterClassA(&wc);
    RECT r{0, 0, g_winW, g_winH};
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
    HWND hwnd = CreateWindowA("DMW3PC", "Digimon World 3 - PC Edition (demo)", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                              CW_USEDEFAULT, CW_USEDEFAULT, r.right - r.left, r.bottom - r.top, nullptr, nullptr,
                              inst, nullptr);
    HDC dc = GetDC(hwnd);
    PIXELFORMATDESCRIPTOR pfd{sizeof(pfd), 1, PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER,
                              PFD_TYPE_RGBA, 32};
    pfd.cAlphaBits = 8;
    SetPixelFormat(dc, ChoosePixelFormat(dc, &pfd), &pfd);
    HGLRC glrc = wglCreateContext(dc);
    wglMakeCurrent(dc, glrc);
    using SwapFn = BOOL(WINAPI*)(int);
    if (auto swap = reinterpret_cast<SwapFn>(reinterpret_cast<void*>(wglGetProcAddress("wglSwapIntervalEXT"))))
        swap(1);
    gfx::initText(dc);
    loadXInput();

    int exitCode = 0;
    {
        Game game;
        std::string err;
        if (!game.init(root, findStageDir(exeDir), stage, dbg, err)) {
            MessageBoxA(hwnd, err.c_str(), "DMW3 PC Edition - load error", MB_ICONERROR);
            g_running = false;
            exitCode = 1;
        }

        LARGE_INTEGER freq, last, now;
        QueryPerformanceFrequency(&freq);
        QueryPerformanceCounter(&last);
        const double tickSec = 1.0 / 60.0;
        double acc = 0;
        Held prev{};
        long long frame = 0;

        while (g_running) {
            MSG m;
            while (PeekMessageA(&m, nullptr, 0, 0, PM_REMOVE)) {
                if (m.message == WM_QUIT) g_running = false;
                TranslateMessage(&m);
                DispatchMessageA(&m);
            }
            QueryPerformanceCounter(&now);
            acc += double(now.QuadPart - last.QuadPart) / double(freq.QuadPart);
            last = now;
            if (acc > 0.25) acc = 0.25;  // avoid spiral of death after a stall
            if (!shotPath.empty()) acc = tickSec;  // deterministic: one tick per frame

            while (acc >= tickSec) {
                acc -= tickSec;
                const bool focused = GetForegroundWindow() == hwnd && shotPath.empty();
                Held h = poll(hwnd, focused);
                if (autowalk) { h.right = (frame / 90) % 2 == 0; h.down = (frame / 90) % 2 == 1; h.run = frame > 120; }
                if (autobattle) {
                    h.up = (frame / 50) % 4 == 1;
                    h.right = (frame / 50) % 4 == 0;
                    h.atk1 = frame % 45 == 0;
                    h.atk2 = frame % 45 == 20;
                    h.atk3 = frame % 120 == 60;
                    h.digivolve = frame % 30 == 0;
                }
                if (h.quit) g_running = false;

                Input in;
                in.up = h.up; in.down = h.down; in.left = h.left; in.right = h.right; in.run = h.run;
                in.confirm = h.confirm; in.atk1 = h.atk1; in.atk2 = h.atk2; in.atk3 = h.atk3;
                in.guard = h.guard; in.digivolve = h.digivolve;
                in.confirmP = h.confirm && !prev.confirm;
                in.atk1P = h.atk1 && !prev.atk1;
                in.atk2P = h.atk2 && !prev.atk2;
                in.atk3P = h.atk3 && !prev.atk3;
                in.digivolveP = h.digivolve && !prev.digivolve;
                in.nextStageP = h.next && !prev.next;
                in.prevStageP = h.prev && !prev.prev;
                in.partnerP = h.partner && !prev.partner;
                in.hudP = h.hud && !prev.hud;
                in.editorP = h.editor && !prev.editor;
                in.equipP = h.equip && !prev.equip;
                for (int vk = 0; vk < 256; ++vk) { in.key[vk] = h.key[vk]; in.keyP[vk] = h.key[vk] && !prev.key[vk]; }
                in.lmb = h.lmb; in.rmb = h.rmb;
                in.lmbP = h.lmb && !prev.lmb;
                in.rmbP = h.rmb && !prev.rmb;
                in.lmbR = !h.lmb && prev.lmb;
                POINT pt;
                if (GetCursorPos(&pt) && ScreenToClient(hwnd, &pt)) gfx::toVirtual(pt.x, pt.y, in.mx, in.my);
                prev = h;
                game.update(in);
            }

            game.render(g_winW, g_winH);
            ++frame;
            if (!shotPath.empty() && frame >= shotFrames) {
                glFinish();
                exitCode = saveScreenshot(shotPath) ? 0 : 1;
                g_running = false;
            }
            SwapBuffers(dc);
        }
    }  // Game (and its textures) destroyed while the GL context is still current

    wglMakeCurrent(nullptr, nullptr);
    wglDeleteContext(glrc);
    ReleaseDC(hwnd, dc);
    DestroyWindow(hwnd);
    return exitCode;
}
