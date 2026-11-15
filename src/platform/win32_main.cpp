// Win32 + OpenGL platform layer: window, input (keyboard + XInput), fixed 60 Hz loop.
// Command line:
//   dmw3.exe [--data <dir>] [--stage S200] [--screenshot out.bmp --frames N] [--autowalk]
#include <windows.h>
#include <shellapi.h>
#include <GL/gl.h>

#include <string>
#include <vector>

#include "core/bytes.h"
#include "game/field.h"

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

struct Buttons {  // raw "held" state, edges computed per tick
    bool up, down, left, right, run, next, prev, partner, hud, quit;
};

Buttons pollButtons(bool focused) {
    Buttons b{};
    if (focused) {
        auto k = [](int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; };
        b.up = k(VK_UP) || k('W');
        b.down = k(VK_DOWN) || k('S');
        b.left = k(VK_LEFT) || k('A');
        b.right = k(VK_RIGHT) || k('D');
        b.run = k(VK_SHIFT);
        b.next = k(VK_NEXT) || k('E');
        b.prev = k(VK_PRIOR) || k('Q');
        b.partner = k(VK_TAB);
        b.hud = k(VK_F1);
        b.quit = k(VK_ESCAPE);
    }
    XState xs{};
    if (g_xinput && g_xinput(0, &xs) == 0) {
        const WORD w = xs.pad.buttons;
        const int dz = 12000;
        b.up |= (w & 0x0001) || xs.pad.ly > dz;
        b.down |= (w & 0x0002) || xs.pad.ly < -dz;
        b.left |= (w & 0x0004) || xs.pad.lx < -dz;
        b.right |= (w & 0x0008) || xs.pad.lx > dz;
        b.run |= (w & 0x1000) != 0;      // A
        b.prev |= (w & 0x0100) != 0;     // LB
        b.next |= (w & 0x0200) != 0;     // RB
        b.partner |= (w & 0x8000) != 0;  // Y
        b.hud |= (w & 0x0020) != 0;      // Back
    }
    return b;
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

}  // namespace

int WINAPI WinMain(HINSTANCE inst, HINSTANCE, LPSTR, int) {
    std::string dataArg, stage = "S200", shotPath;
    int shotFrames = 0;
    bool autowalk = false;
    auto a = args();
    for (size_t i = 0; i < a.size(); ++i) {
        if (a[i] == "--data" && i + 1 < a.size()) dataArg = a[++i];
        else if (a[i] == "--stage" && i + 1 < a.size()) stage = a[++i];
        else if (a[i] == "--screenshot" && i + 1 < a.size()) shotPath = a[++i];
        else if (a[i] == "--frames" && i + 1 < a.size()) shotFrames = std::stoi(a[++i]);
        else if (a[i] == "--autowalk") autowalk = true;
    }
    if (!shotPath.empty() && shotFrames <= 0) shotFrames = 120;

    char exe[MAX_PATH];
    GetModuleFileNameA(nullptr, exe, MAX_PATH);
    fs::path root = findDataRoot(dataArg, fs::path(exe).parent_path());
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
        Field field;
        std::string err;
        if (!field.init(root, stage, err)) {
            MessageBoxA(hwnd, err.c_str(), "DMW3 PC Edition - load error", MB_ICONERROR);
            g_running = false;
            exitCode = 1;
        }

        LARGE_INTEGER freq, last, now;
        QueryPerformanceFrequency(&freq);
        QueryPerformanceCounter(&last);
        const double tickSec = 1.0 / 60.0;
        double acc = 0;
        Buttons prev{};
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
                Buttons b = pollButtons(GetForegroundWindow() == hwnd);
                if (autowalk) { b.right = (frame / 90) % 2 == 0; b.down = (frame / 90) % 2 == 1; b.run = frame > 120; }
                if (b.quit) g_running = false;
                Input in;
                in.up = b.up; in.down = b.down; in.left = b.left; in.right = b.right; in.run = b.run;
                in.nextStage = b.next && !prev.next;
                in.prevStage = b.prev && !prev.prev;
                in.nextPartner = b.partner && !prev.partner;
                in.toggleHud = b.hud && !prev.hud;
                prev = b;
                field.update(in);
            }

            gfx::beginFrame(g_winW, g_winH);
            field.render();
            gfx::beginHud(g_winW, g_winH);
            field.renderHud(g_winW, g_winH);
            ++frame;
            if (!shotPath.empty() && frame >= shotFrames) {
                glFinish();
                exitCode = saveScreenshot(shotPath) ? 0 : 1;
                g_running = false;
            }
            SwapBuffers(dc);
        }
    }  // Field (and its textures) destroyed while the GL context is still current

    wglMakeCurrent(nullptr, nullptr);
    wglDeleteContext(glrc);
    ReleaseDC(hwnd, dc);
    DestroyWindow(hwnd);
    return exitCode;
}
