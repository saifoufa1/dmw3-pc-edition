// Win32 + OpenGL platform layer: window, input (keyboard, mouse, XInput), fixed 60 Hz loop.
// Command line:
//   dmw3.exe [--data <dir>] [--stage S200]
//   testing: [--screenshot out.bmp --frames N] [--autowalk] [--battle] [--boss <sprite>] [--autobattle]
//            [--dv] (full digivolve gauge) [--form 0..3] (start digivolved) [--digivolve-at FRAME] [--attack 1..3]
//            [--arena 1..55] (battle arena MEFT10xx) [--cam-yaw DEG] (battle camera angle)
//   PC flow (default when no test flag is given): title -> menu -> Digimon Online sign-up -> starter pack -> lobby
//            [--flow title|menu|options|slots|newslot|signup|pack|newgame|continue] (start at / drive to that screen) [--slot N]
//            [--esc-at FRAME] (press Esc)  [--select-set N] (pack 1..3)  [--account NAME]  [--save-dir DIR] (tests: keep saves out of APPDATA)
//            [--lobby STAGE] (default S210, Asuka City Main Lobby)  [--flow-event NAME] [--flow-pos X Y] (once playing)
//   story:   [--new-game] [--no-story] [--story-flags a,b] [--story-event NAME] [--pos X Y]
//            [--confirm-at FRAME]... [--confirm-every N] [--confirm-from FRAME] [--walk lrud FROM TO]
//   video:   [--window W H] (client size; with --screenshot the frame is composed off-screen at W x H, so the
//            capture does not depend on the window being visible or fitting the monitor)
//            [--scale fit|stretch|integer] [--no-wide] [--lowres] [--nearest] [--msaa N] [--crt]
//            [--mouse X Y] [--click-at FRAME] (tests: mouse in window pixels, left click on that frame)
#include <windows.h>
#include <shellapi.h>
#include <GL/gl.h>

#include <cmath>
#include <memory>
#include <string>
#include <vector>

#include "core/bytes.h"
#include "game/app.h"
#include "game/game.h"
#include "ui/ui_draw.h"
#include "ui/ui_preview.h"

namespace fs = std::filesystem;
using namespace dw3;

namespace {

int g_winW = 960, g_winH = 720;
bool g_running = true;
int g_wheel = 0;  // mouse wheel notches since the last tick (+ = toward the user)
bool g_fixedSize = false;  // screenshot tests: the frame size comes from --window, not from the window

// Real pixels on high-DPI monitors (otherwise Windows scales the window bitmap and fullscreen gets blurry).
void makeDpiAware() {
    if (HMODULE u = GetModuleHandleA("user32.dll")) {
        using CtxFn = BOOL(WINAPI*)(HANDLE);
        if (auto f = reinterpret_cast<CtxFn>(reinterpret_cast<void*>(GetProcAddress(u, "SetProcessDpiAwarenessContext"))))
            if (f(reinterpret_cast<HANDLE>(static_cast<intptr_t>(-4)))) return;  // per-monitor v2
        using AwareFn = BOOL(WINAPI*)();
        if (auto f = reinterpret_cast<AwareFn>(reinterpret_cast<void*>(GetProcAddress(u, "SetProcessDPIAware")))) f();
    }
}

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
        case WM_SIZE:
            if (!g_fixedSize) { g_winW = LOWORD(lp); g_winH = HIWORD(lp); }
            return 0;
        case WM_ERASEBKGND: return 1;  // the game repaints the whole client area
        case WM_MOUSEWHEEL: g_wheel += GET_WHEEL_DELTA_WPARAM(wp) > 0 ? -1 : 1; return 0;  // battle camera zoom
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
    float camTurn, camTilt;  // right stick (battle camera)
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
        auto axis = [](SHORT v) { const float f = float(v) / 32767.f; return std::fabs(f) < 0.25f ? 0.f : f; };
        h.camTurn = axis(xs.pad.rx);
        h.camTilt = -axis(xs.pad.ry);
    }
    return h;
}

bool saveScreenshot(const fs::path& p) {
    Image img;
    gfx::readFrame(img);  // the composed frame (off-screen target in screenshot runs)
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
    int digivolveAt = -1;  // testing: press digivolve on this frame
    int autoAttack = 0;    // testing: use only this move (1..3) every 100 frames
    std::vector<int> confirmAt;  // testing: press Space on these frames
    std::vector<int> escAt;      // testing: press Esc on these frames (PC flow pause menu)
    int confirmEvery = 0;        // testing: press Space every N frames (from frame confirmFrom on)
    int confirmFrom = 0;
    std::string walkDir;         // testing: hold a direction (l/r/u/d, combinable) from frame A to B
    int walkFrom = 0, walkTo = 0;
    DebugOptions dbg;
    int winReqW = 0, winReqH = 0;      // --window W H
    std::string scaleArg;              // --scale fit|stretch|integer
    bool noWide = false, lowRes = false, nearest = false, crt = false;
    int msaaArg = -1;
    int mouseX = -1, mouseY = -1;      // --mouse X Y (window pixels, tests)
    std::vector<int> clickAt;          // --click-at FRAME (tests)
    std::string uiPreview;  // --ui-preview MODE: original-UI component preview (src/ui/ui_preview.h)
    FlowOptions flow;
    bool legacy = false;    // any old test flag: start the field/battle directly (no title flow)
    auto a = args();
    for (size_t i = 0; i < a.size(); ++i) {
        if (a[i] == "--data" && i + 1 < a.size()) dataArg = a[++i];
        else if (a[i] == "--stage" && i + 1 < a.size()) { stage = a[++i]; dbg.stageGiven = true; }
        else if (a[i] == "--new-game") dbg.newGame = true;
        else if (a[i] == "--no-story") dbg.noStory = true;
        else if (a[i] == "--story-event" && i + 1 < a.size()) dbg.storyEvent = a[++i];
        else if (a[i] == "--story-flags" && i + 1 < a.size()) {
            std::string f = a[++i], cur;
            for (char ch : f + ",") {
                if (ch == ',') { if (!cur.empty()) dbg.storyFlags.push_back(cur); cur.clear(); }
                else cur += ch;
            }
        }
        else if (a[i] == "--pos" && i + 2 < a.size()) { dbg.posX = std::stoi(a[i + 1]); dbg.posY = std::stoi(a[i + 2]); i += 2; }
        else if (a[i] == "--confirm-at" && i + 1 < a.size()) confirmAt.push_back(std::stoi(a[++i]));
        else if (a[i] == "--esc-at" && i + 1 < a.size()) escAt.push_back(std::stoi(a[++i]));
        else if (a[i] == "--confirm-every" && i + 1 < a.size()) confirmEvery = std::stoi(a[++i]);
        else if (a[i] == "--confirm-from" && i + 1 < a.size()) confirmFrom = std::stoi(a[++i]);
        else if (a[i] == "--walk" && i + 3 < a.size()) { walkDir = a[i + 1]; walkFrom = std::stoi(a[i + 2]); walkTo = std::stoi(a[i + 3]); i += 3; }
        else if (a[i] == "--screenshot" && i + 1 < a.size()) shotPath = a[++i];
        else if (a[i] == "--frames" && i + 1 < a.size()) shotFrames = std::stoi(a[++i]);
        else if (a[i] == "--autowalk") autowalk = true;
        else if (a[i] == "--autobattle") autobattle = true;
        else if (a[i] == "--dv") dbg.fullDv = true;
        else if (a[i] == "--attack" && i + 1 < a.size()) autoAttack = std::stoi(a[++i]);
        else if (a[i] == "--form" && i + 1 < a.size()) { dbg.startBattle = true; dbg.startForm = std::stoi(a[++i]); }
        else if (a[i] == "--digivolve-at" && i + 1 < a.size()) digivolveAt = std::stoi(a[++i]);
        else if (a[i] == "--battle") dbg.startBattle = true;
        else if (a[i] == "--sizes") { dbg.startBattle = true; dbg.sizePanel = true; }
        else if (a[i] == "--dev") dbg.devTools = true;
        else if (a[i] == "--arena" && i + 1 < a.size()) { dbg.startBattle = true; dbg.arena = std::stoi(a[++i]); }
        else if (a[i] == "--cam-yaw" && i + 1 < a.size()) dbg.camYaw = std::stof(a[++i]);
        else if (a[i] == "--boss" && i + 1 < a.size()) { dbg.startBattle = true; dbg.battleSprite = a[++i]; }
        else if (a[i] == "--ui-preview" && i + 1 < a.size()) uiPreview = a[++i];  // hud|hud2|result|levelup|window|font|sheets
        else if (a[i] == "--flow" && i + 1 < a.size()) flow.stopAt = a[++i];
        else if (a[i] == "--select-set" && i + 1 < a.size()) flow.selectPack = std::stoi(a[++i]);
        else if (a[i] == "--account" && i + 1 < a.size()) flow.account = a[++i];
        else if (a[i] == "--password" && i + 1 < a.size()) flow.password = a[++i];
        else if (a[i] == "--lobby" && i + 1 < a.size()) flow.lobbyStage = a[++i];
        else if (a[i] == "--slot" && i + 1 < a.size()) flow.slot = std::stoi(a[++i]);
        else if (a[i] == "--flow-event" && i + 1 < a.size()) dbg.flowEvent = a[++i];
        else if (a[i] == "--flow-pos" && i + 2 < a.size()) { dbg.flowX = std::stoi(a[i + 1]); dbg.flowY = std::stoi(a[i + 2]); i += 2; }
        else if (a[i] == "--save-dir" && i + 1 < a.size()) Story::setSaveDir(fs::path(a[++i]));
        else if (a[i] == "--window" && i + 2 < a.size()) { winReqW = std::stoi(a[i + 1]); winReqH = std::stoi(a[i + 2]); i += 2; }
        else if (a[i] == "--scale" && i + 1 < a.size()) scaleArg = a[++i];
        else if (a[i] == "--no-wide") noWide = true;
        else if (a[i] == "--lowres") lowRes = true;
        else if (a[i] == "--nearest") nearest = true;
        else if (a[i] == "--crt") crt = true;
        else if (a[i] == "--msaa" && i + 1 < a.size()) msaaArg = std::stoi(a[++i]);
        else if (a[i] == "--mouse" && i + 2 < a.size()) { mouseX = std::stoi(a[i + 1]); mouseY = std::stoi(a[i + 2]); i += 2; }
        else if (a[i] == "--click-at" && i + 1 < a.size()) clickAt.push_back(std::stoi(a[++i]));
    }
    legacy = dbg.stageGiven || dbg.newGame || dbg.noStory || dbg.startBattle || !dbg.storyEvent.empty() ||
             !dbg.storyFlags.empty() || dbg.posX >= 0;
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

    makeDpiAware();
    const bool shotRun = !shotPath.empty();
    if (winReqW > 0 && winReqH > 0) { g_winW = winReqW; g_winH = winReqH; }
    if (shotRun) g_fixedSize = true;  // compose off-screen at exactly g_winW x g_winH
    WNDCLASSA wc{};
    wc.style = CS_OWNDC;
    wc.lpfnWndProc = wndProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = "DMW3PC";
    RegisterClassA(&wc);
    RECT r{0, 0, shotRun ? 320 : g_winW, shotRun ? 240 : g_winH};
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
    HWND hwnd = CreateWindowA("DMW3PC", "Digimon World 3 - PC Edition (demo)", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                              CW_USEDEFAULT, CW_USEDEFAULT, r.right - r.left, r.bottom - r.top, nullptr, nullptr,
                              inst, nullptr);
    HDC dc = GetDC(hwnd);
    PIXELFORMATDESCRIPTOR pfd{sizeof(pfd), 1, PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER,
                              PFD_TYPE_RGBA, 32};
    pfd.cAlphaBits = 8;
    pfd.cDepthBits = 24;  // 3D battle models
    SetPixelFormat(dc, ChoosePixelFormat(dc, &pfd), &pfd);
    HGLRC glrc = wglCreateContext(dc);
    wglMakeCurrent(dc, glrc);
    using SwapFn = BOOL(WINAPI*)(int);
    if (auto swap = reinterpret_cast<SwapFn>(reinterpret_cast<void*>(wglGetProcAddress("wglSwapIntervalEXT"))))
        swap(1);
    gfx::initText(dc);
    gfx::setOffscreenPresent(shotRun);
    loadXInput();

    int exitCode = 0;
    {
        Game game;
        App app;
        std::string err;
        std::unique_ptr<ui::Preview> preview;
        if (!uiPreview.empty()) {
            preview = std::make_unique<ui::Preview>();
            if (!preview->init(root, uiPreview, err)) {
                MessageBoxA(hwnd, err.c_str(), "DMW3 PC Edition - UI preview", MB_ICONERROR);
                g_running = false;
                exitCode = 1;
            }
        } else if (legacy) {
            if (ui::init(root), !game.init(root, findStageDir(exeDir), stage, dbg, err)) {  // original UI (font, windows, HUD)
                MessageBoxA(hwnd, err.c_str(), "DMW3 PC Edition - load error", MB_ICONERROR);
                g_running = false;
                exitCode = 1;
            }
        } else if (!app.init(root, findStageDir(exeDir), dbg, flow, err)) {
            MessageBoxA(hwnd, err.c_str(), "DMW3 PC Edition - load error", MB_ICONERROR);
            g_running = false;
            exitCode = 1;
        }
        {  // command-line video overrides (tests); the Options screen saves the player's own choice
            VideoOptions vo = gfx::videoOptions();
            if (scaleArg == "fit") vo.scale = ScaleMode::Fit;
            else if (scaleArg == "stretch") vo.scale = ScaleMode::Stretch;
            else if (scaleArg == "integer") vo.scale = ScaleMode::Integer;
            if (noWide) vo.wide = false;
            if (lowRes) vo.hiRes = false;
            if (nearest) vo.sharp = false;
            if (crt) vo.crt = true;
            if (msaaArg >= 0) vo.msaa = msaaArg;
            gfx::setVideoOptions(vo);
        }
        bool isFullscreen = false;
        WINDOWPLACEMENT savedPlace{sizeof(WINDOWPLACEMENT)};

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
                if (digivolveAt >= 0 && (frame == digivolveAt || frame == digivolveAt + 1)) h.digivolve = frame == digivolveAt;
                if (autoAttack) {
                    h.atk1 = autoAttack == 1 && frame % 100 == 80;
                    h.atk2 = autoAttack == 2 && frame % 100 == 80;
                    h.atk3 = autoAttack == 3 && frame % 100 == 80;
                }
                for (int f : confirmAt) if (frame == f) h.confirm = true;
                for (int f : escAt) if (frame == f) h.key[VK_ESCAPE] = true;
                if (confirmEvery > 0 && frame % confirmEvery == 0 && frame > confirmFrom) h.confirm = true;
                if (!walkDir.empty() && frame >= walkFrom && frame < walkTo) {
                    h.left |= walkDir.find('l') != std::string::npos;
                    h.right |= walkDir.find('r') != std::string::npos;
                    h.up |= walkDir.find('u') != std::string::npos;
                    h.down |= walkDir.find('d') != std::string::npos;
                }
                if (h.quit && (legacy || preview)) g_running = false;  // the PC flow handles Esc itself

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
                in.camTurn = h.camTurn; in.camTilt = h.camTilt;
                in.wheel = g_wheel; g_wheel = 0;
                POINT pt;
                if (mouseX >= 0) {
                    gfx::toVirtual(mouseX, mouseY, in.mx, in.my);
                    for (int f : clickAt) {
                        if (frame == f) { in.lmb = in.lmbP = true; }
                        if (frame == f + 1) in.lmbR = true;
                    }
                } else if (focused && GetCursorPos(&pt) && ScreenToClient(hwnd, &pt)) {
                    gfx::toVirtual(pt.x, pt.y, in.mx, in.my);
                }
                prev = h;
                if (preview) preview->update(in.confirmP);
                else if (legacy) game.update(in);
                else {
                    app.update(in);
                    if (app.quitRequested()) g_running = false;
                }
            }
            if (!preview && !legacy && app.fullscreen() != isFullscreen && shotPath.empty()) {  // Options: borderless fullscreen
                isFullscreen = app.fullscreen();
                if (isFullscreen) {
                    GetWindowPlacement(hwnd, &savedPlace);
                    MONITORINFO mi{sizeof(mi)};
                    GetMonitorInfoA(MonitorFromWindow(hwnd, MONITOR_DEFAULTTOPRIMARY), &mi);
                    SetWindowLongA(hwnd, GWL_STYLE, WS_POPUP | WS_VISIBLE);
                    SetWindowPos(hwnd, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right - mi.rcMonitor.left,
                                 mi.rcMonitor.bottom - mi.rcMonitor.top, SWP_FRAMECHANGED);
                } else {
                    SetWindowLongA(hwnd, GWL_STYLE, WS_OVERLAPPEDWINDOW | WS_VISIBLE);
                    SetWindowPlacement(hwnd, &savedPlace);
                    SetWindowPos(hwnd, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
                }
            }

            if (preview) preview->render(g_winW, g_winH);
            else if (legacy) game.render(g_winW, g_winH);
            else app.render(g_winW, g_winH);
            gfx::endFrame();
            ++frame;
            if (!shotPath.empty() && frame >= shotFrames) {
                glFinish();
                exitCode = saveScreenshot(shotPath) ? 0 : 1;
                g_running = false;
            }
            SwapBuffers(dc);
        }
        if (legacy) ui::shutdown();
        else if (!preview) app.shutdown();
        gfx::shutdown();
    }  // Game (and its textures) destroyed while the GL context is still current

    wglMakeCurrent(nullptr, nullptr);
    wglDeleteContext(glrc);
    ReleaseDC(hwnd, dc);
    DestroyWindow(hwnd);
    return exitCode;
}
