// main.cpp - borderless-fullscreen D3D11 host rendering the RetroArch XMB background pipelines
// (ribbon + gradient theme) with a navigable XMB-style menu on top. Menu content is loaded from
// launcher.json (Phase 3); selecting an item launches an app. Layout/zoom/alpha/easing constants
// ported 1:1 from RetroArch xmb.c / gfx_animation.c - see reference/retroarch/XMB_LAYOUT.md.
//
// Controls:
//   Keyboard: Up/Down = move item, Left/Right = switch category, Enter = OK (launch), Esc = quit.
//             'S' or Tab opens the Settings screen. All menus/submenus wrap around at both ends.
//   Gamepad (XInput): D-pad = navigate, A = OK (launch), B = quit/back, Back/Start = Settings.
//   Settings screen: orientation, menu placement (scrolling/fixed), clock size, screensaver
//             (on/off, delay, dim %, corner-clock motion), keep-display-awake, and the menu editor.
//             All changes persist to launcher.json - there are no per-setting dev hotkeys.
//   Idle fades the menu to a dimmed ribbon screensaver (configurable); any input restores it.
//   (bg/ribbon/theme are fixed at the default; those become settings-driven in a later phase.)
//
// GPLv3 (see LICENSE) - contains code ported from RetroArch.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <dxgi1_3.h>
#include <d3dcompiler.h>
#include <d2d1_1.h>
#include <dwrite.h>
#include <DirectXMath.h>
#include <Xinput.h>
#include <shellapi.h>
#include <string>
#include <vector>
#include <utility>
#include <cstdio>
#include <cwchar>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <unordered_map>
#include <wincodec.h>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "xinput9_1_0.lib")
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "advapi32.lib")

// NOTE: <windows.h> defines DrawText -> DrawTextW, and <d2d1.h> is parsed with that macro
// active, so the interface method is compiled as DrawTextW. We therefore call ->DrawText(...)
// WITHOUT undefining the macro, so our calls also expand to DrawTextW and match. (Undefining
// it breaks the call - that was the build error.)

#include "resource.h"
#include "shaders.h"
#include "backgrounds.h"
#include "themes.h"
#include "json.h"

using namespace DirectX;

// ------------------------------- constant buffer -------------------------------
struct Uniform
{
    XMFLOAT4X4 mvp;        // 64
    XMFLOAT2   outputSize; // 8
    float      time;       // 4
    float      alpha;      // 4
};                         // 80 bytes (16-aligned)

// Custom Colour Ribbon params (register b1). Kept separate from the shared b0 uniform so its
// layout is untouched. Settings UI will drive gRibbonColor / gRibbonOpacity later; defaults here.
struct RibbonUniform
{
    float color[4];   // rgb + unused a   (16)
    float opacity;    // sheen->alpha multiplier (opacity curve knob)
    float sheen;      // fold-sheen divisor (lower = stronger/brighter folds); default 13
    float pad[2];     // pad to 32 bytes (16-aligned)
};
static XMFLOAT4 gRibbonColor  = { 1.0f, 0.78f, 0.30f, 1.0f }; // custom-ribbon colour (RGB set in Settings)
static float    gRibbonOpacity = 1.0f;                        // default opacity-curve strength
static float    gRibbonSheen   = 13.0f;                       // fold-sheen divisor (shader default was /13)
// Custom Colour Ribbon: optional slow, continuous hue cycling. When on, the colour fed to the shader is
// an animated full-saturation hue (the user's static gRibbonColor is left untouched, so turning it off
// restores the picked colour). config: appearance.ribbonCycle.
static bool     gRibbonCycle    = false;
static float    gRibbonCycleHue = 0.0f;                       // animated hue in degrees (0..360)
static const float kRibbonCycleDegPerSec = 6.0f;             // ~60s per full spectrum ("slowly")
// Custom-theme gradient colours (RGB 0..1), used when the Theme is "Custom" (index kThemeCount).
static float gThemeTop[3] = { 0.20f, 0.30f, 0.60f };          // top of the gradient
static float gThemeBot[3] = { 0.02f, 0.02f, 0.05f };          // bottom of the gradient
// Background = "Image": a full-screen user image (path); empty = none chosen yet.
static std::wstring gBgImagePath;
// small helpers for RGB channel settings (0..1 stored; shown/stepped as 0-255)
static int  Ch255(float c) { int v = (int)(c * 255.0f + 0.5f); return v < 0 ? 0 : (v > 255 ? 255 : v); }

// ------------------------------- vertex formats --------------------------------
struct VtxPos    { float x, y; };                    // ribbon grid
struct VtxPosTex { float x, y; float u, v; };         // snow/bokeh quad
struct VtxPosCol { float x, y; float r, g, b, a; };   // gradient quad

// ------------------------------- globals ---------------------------------------
static HWND                     gHwnd = nullptr;
static ID3D11Device*            gDev = nullptr;
static ID3D11DeviceContext*     gCtx = nullptr;
static IDXGISwapChain*          gSwap = nullptr;   // base interface (Present/Resize/GetBuffer)
static IDXGISwapChain2*         gSwap2 = nullptr;  // for waitable object + frame latency
static HANDLE                   gWaitable = nullptr;
static ID3D11RenderTargetView*  gRTV = nullptr;
static UINT                     gW = 1920, gH = 1080;

// Swapchain flag reused at create + ResizeBuffers.
static const UINT kSwapFlags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;

// High-resolution timer for per-second (delta-time) animation.
static LARGE_INTEGER            gQpcFreq = {};
static LARGE_INTEGER            gQpcLast = {};

// Direct2D / DirectWrite text layer (on-screen labels).
static ID2D1Factory1*           gD2DFactory = nullptr;
static ID2D1Device*             gD2DDevice = nullptr;
static ID2D1DeviceContext*      gD2DCtx = nullptr;
static ID2D1Bitmap1*            gD2DTarget = nullptr;
static ID2D1SolidColorBrush*    gBrush = nullptr;
static ID2D1SolidColorBrush*    gShadow = nullptr;
static ID2D1SolidColorBrush*    gClearBrush = nullptr;   // fully transparent; used to hide the blinking colon without changing its advance width
static IDWriteFactory*          gDWrite = nullptr;
static IDWriteTextFormat*       gTextFmt = nullptr;

static ID3D11Buffer*            gCB = nullptr;        // Uniform (b0)
static ID3D11Buffer*            gRibbonCB = nullptr;  // RibbonUniform (b1, custom ribbon color/opacity)
static ID3D11Buffer*            gRibbonVB = nullptr;  // grid
static UINT                     gRibbonVerts = 0;
static ID3D11Buffer*            gQuadVB = nullptr;    // pos+tex quad
static ID3D11Buffer*            gGradVB = nullptr;    // dynamic gradient quad

static ID3D11InputLayout*       gRibbonLayout = nullptr;
static ID3D11InputLayout*       gQuadLayout = nullptr;
static ID3D11InputLayout*       gGradLayout = nullptr;

static ID3D11VertexShader*      gGradVS = nullptr;
static ID3D11PixelShader*       gGradPS = nullptr;

static ID3D11BlendState*        gBlendOpaque = nullptr;
static ID3D11BlendState*        gBlendPipeline = nullptr; // DEST_COLOR / ONE (ribbon)
static ID3D11BlendState*        gBlendAlpha = nullptr;    // SRC_ALPHA / INV_SRC_ALPHA
static ID3D11BlendState*        gBlendAdditive = nullptr; // SRC_ALPHA / ONE (sparks/glints)
static ID3D11RasterizerState*   gRaster = nullptr;

struct Pipeline
{
    const char*            name;
    ID3D11VertexShader*    vs;
    ID3D11PixelShader*     ps;
    ID3D11InputLayout*     layout;
    ID3D11Buffer**         vb;
    UINT                   stride;
    UINT*                  vcount;      // pointer so ribbon count resolves at draw
    UINT                   fixedCount;  // used when vcount == nullptr
    ID3D11BlendState**     blend;
    bool                   isRibbon;    // true = ribbon overlay, false = background
};
static std::vector<Pipeline> gPipes;
// gBgList / gRibbonList hold indices into gPipes; gBgSel/gRibbonSel are the RENDER inputs (a value
// == list size means "None"). These are DERIVED from the user's Background choice below.
static std::vector<int> gBgList;
static std::vector<int> gRibbonList;
static int   gBgSel = 0;       // render: which animated background (== size -> none)
static int   gRibbonSel = 0;   // render: which ribbon overlay      (== size -> none)
static int   gTheme = 0;
static float gTime = 0.0f;
static void UpdateGradientVB(int theme);  // fwd: Settings theme row changes the gradient live
static void BuildCustomTheme();           // fwd: rebuild the "Custom" gradient from gThemeTop/gThemeBot
static bool RibbonIsCustom();             // fwd: the shown ribbon is the user-colourable "Custom Colour Ribbon"

// The user picks ONE "Background": None, an animated background, or "Ribbon". The Ribbon option is
// last. gBgChoice indexes [0 = None, 1..N = animated backgrounds, N+1 = Ribbon]. When Ribbon is
// chosen the user also picks WHICH ribbon (gRibbonChoice) and the theme/gradient. Background and
// ribbon are therefore mutually exclusive; ApplyBgSelection() maps the choice onto gBgSel/gRibbonSel.
static int gBgChoice = 0;
static int gRibbonChoice = 0;                 // remembered ribbon variant (index into gRibbonList)
static int  BgRibbonChoice() { return (int)gBgList.size() + 1; }   // gBgChoice value that means "Ribbon"
static int  BgImageChoice()  { return (int)gBgList.size() + 2; }   // gBgChoice value that means "Image"
static int  BgChoiceCount()  { return (int)gBgList.size() + 3; }   // None + backgrounds + Ribbon + Image
static bool BgIsRibbon()     { return gBgChoice == BgRibbonChoice(); }
static bool BgIsImage()      { return gBgChoice == BgImageChoice(); }
static void ApplyBgSelection()
{
    if (gBgChoice == BgRibbonChoice())        // Ribbon: draw the chosen ribbon, no animated background
    {
        gBgSel     = (int)gBgList.size();      // none
        gRibbonSel = (gRibbonChoice >= 0 && gRibbonChoice < (int)gRibbonList.size())
                   ? gRibbonChoice : (int)gRibbonList.size();
    }
    else                                       // None / animated bg / Image: no ribbon
    {
        gRibbonSel = (int)gRibbonList.size();
        gBgSel = (gBgChoice >= 1 && gBgChoice <= (int)gBgList.size()) ? gBgChoice - 1 : (int)gBgList.size();
        // Image (and None) leave the animated bg off; the image is drawn over the gradient in DrawMenu.
    }
}

// Global menu fade (xmb->alpha): 0 = hidden, 1 = fully shown. Fades in on launch; Phase 4 idle
// will drive it to 0 for the ribbon-only screensaver. Multiplies every menu draw alpha.
static float gMenuAlpha       = 0.0f;
static float gMenuAlphaTarget = 1.0f;
// True once idle has faded the menu to the ribbon-only screensaver (see PollInput). Declared here
// so DrawMenu can decide whether to draw the screensaver clock. The enable + timeout come from the
// config (Phase 4) and are read in LoadConfig, so they live up here too.
static bool  gScreensaver       = false;
static bool  gScreensaverEnabled = true;      // config: screensaver.enabled
static float gIdleTimeoutMs      = 30000.0f;  // config: screensaver.timeoutSeconds * 1000
// Idle clock + focus flag. Declared up here (before Render) because the adaptive-vsync Present logic in
// Render reads them, as well as PollInput/NoteActivity further down.
static float gIdleTimer          = 0.0f;      // ms since the last input (drives screensaver + frame pacing)
static bool  gHasFocus           = true;      // launcher is the foreground window (set in PollInput)
// Held keyboard state, tracked from WM_KEYDOWN/WM_KEYUP (NOT GetAsyncKeyState). Window messages are
// delivered for both physical AND remote-injected input - RustDesk / RDP post key MESSAGES but do not
// update the low-level async key state - so polling this array makes arrow-nav + Enter work over remote
// desktop. Cleared on focus loss so a key can't stick if its KEYUP is missed while we are in the background.
static bool  gKeyDown[256]       = {};
// Tap latch for INJECTED directional input. Tools like the phone remote (keybd_event) and the CEC
// helper inject an arrow as a WM_KEYDOWN + WM_KEYUP pair with ~0 ms between them, so both messages are
// drained in one pump pass and gKeyDown[VK_ARROW] is set true then false BEFORE PollInput samples it -
// the press is never seen as held and nav does nothing. WM_KEYDOWN latches the arrow here (on the
// non-repeat edge); PollInput OR-s the latch into the held state so an injected tap is seen as "down"
// for exactly one frame (fires one nav step), then consumes/clears it. Physical holds still work via
// gKeyDown, so the RA auto-repeat feel is unchanged. Indices: VK_LEFT/UP/RIGHT/DOWN (0x25-0x28).
static bool  gArrowTap[256]      = {};
// How far the screensaver dims the whole screen (0 = ribbon at full brightness, 100 = black).
// Drawn as a black scrim that fades in as the menu fades out. config: screensaver.dimPercent.
static bool  gDimEnabled          = true;     // config: screensaver.dimEnabled (dim on/off)
static int   gSaverDimPercent    = 50;
// Keep the display + system awake (inhibit the OS screensaver and auto-sleep) while the launcher is
// the foreground app. config: keepDisplayAwake. Applied via SetThreadExecutionState.
static bool  gInhibitSleep       = true;
// Return-to-home: while occluded, if the desktop would otherwise be showing (every app closed / hidden /
// minimised), reclaim the foreground so the launcher is the home screen. config: returnHome. Debounced.
static bool  gReturnHome         = true;
static float gHomeIdleMs         = 0.0f;

// Windows integration (Phase 6). Autostart + kiosk on/off state live in the REGISTRY (source of truth,
// read live so the Settings rows never drift from reality). gShellBackup remembers the Winlogon Shell
// value that was in place before kiosk took over, so disabling kiosk can restore it; it is persisted in
// launcher.json (kioskShellBackup). gLaunchedAsShell is set at startup when the launcher IS the Windows
// shell this session, so quitting spawns explorer and the user gets a desktop instead of a black screen.
// gBootFocusMs is a short window after launch during which we keep re-grabbing the foreground, so a cold
// boot (explorer + other startup apps stealing focus) cannot leave the launcher behind another window.
static std::wstring gShellBackup;                 // config: kioskShellBackup (empty = restore system default)
static bool  gFirstRun           = false;         // true when launcher.json was just seeded (no config existed): land in edit mode
static bool  gLaunchedAsShell    = false;         // true when Winlogon Shell == our exe at startup
static float gBootFocusMs        = 5000.0f;       // counts down; while >0 we re-assert foreground each frame

// Menu orientation. false = horizontal XMB; true = vertical (90-degree transpose: categories run
// down the left, the active category's items extend to the right; text stays upright). Chosen via
// settings later; a temp toggle key ('V' / gamepad Y) flips it for now.
static bool gVertical = false;

// Menu placement. false = classic XMB (the list scrolls so the selection stays at a fixed anchor);
// true = "fixed" placement where the item/category elements are fixed on screen and only a
// translucent highlight selector moves. In fixed mode elements stay put until the selector would
// leave the screen, at which point the list scrolls by the minimum to keep it visible. Works in
// both orientations. Config-persisted ("menu.placement"); temp toggle key 'L' for now.
static bool gFixedLayout = false;
// First visible index in fixed placement (per-axis scroll offset; 0 while everything fits).
static int  gFixedItemScroll = 0;
static int  gFixedCatScroll  = 0;

// =============================================================================
// Phase 2: XMB-style menu (static data for now; JSON loader is Phase 3).
// Constants ported 1:1 from RetroArch xmb.c - see reference/retroarch/XMB_LAYOUT.md.
// =============================================================================


#include "menu_model.inc"
#include "menu_activelist.inc"
#include "menu_icons.inc"
#include "config_io.inc"
#include "launch.inc"
#include "input_actions.inc"
#include "menu_draw.inc"
#include "edit_mode.inc"
#include "settings.inc"

// ------------------------------- helpers ---------------------------------------
static void Fatal(const char* msg)
{
    MessageBoxA(gHwnd, msg, "Launcher - fatal", MB_OK | MB_ICONERROR);
    ExitProcess(1);
}

static ID3DBlob* Compile(const std::string& src, const char* entry, const char* target)
{
    ID3DBlob* code = nullptr;
    ID3DBlob* err = nullptr;
    UINT flags = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3;
    HRESULT hr = D3DCompile(src.c_str(), src.size(), nullptr, nullptr, nullptr,
                            entry, target, flags, 0, &code, &err);
    if (FAILED(hr))
    {
        std::string m = "Shader compile failed (";
        m += entry; m += "):\n";
        if (err) m += (const char*)err->GetBufferPointer();
        Fatal(m.c_str());
    }
    if (err) err->Release();
    return code;
}

static void MakeVSPS(const std::string& src, ID3D11VertexShader** vs,
                     ID3D11PixelShader** ps, ID3DBlob** vsBlobOut)
{
    ID3DBlob* vsb = Compile(src, "VSMain", "vs_5_0");
    ID3DBlob* psb = Compile(src, "PSMain", "ps_5_0");
    gDev->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), nullptr, vs);
    gDev->CreatePixelShader(psb->GetBufferPointer(), psb->GetBufferSize(), nullptr, ps);
    psb->Release();
    if (vsBlobOut) *vsBlobOut = vsb; else vsb->Release();
}

// pipeline VS entry differs for the quad effects; compile with explicit entry names
static void MakeVSPSEntry(const std::string& src, const char* vsEntry, const char* psEntry,
                          ID3D11VertexShader** vs, ID3D11PixelShader** ps, ID3DBlob** vsBlobOut)
{
    ID3DBlob* vsb = Compile(src, vsEntry, "vs_5_0");
    ID3DBlob* psb = Compile(src, psEntry, "ps_5_0");
    gDev->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), nullptr, vs);
    gDev->CreatePixelShader(psb->GetBufferPointer(), psb->GetBufferSize(), nullptr, ps);
    psb->Release();
    if (vsBlobOut) *vsBlobOut = vsb; else vsb->Release();
}

static std::string Replace(std::string s, const std::string& a, const std::string& b)
{
    size_t p;
    while ((p = s.find(a)) != std::string::npos) s.replace(p, a.size(), b);
    return s;
}

// ------------------------------- geometry --------------------------------------
static void BuildRibbonGrid()
{
    const int ROWS = 64, COLS = 64;
    std::vector<VtxPos> verts;
    verts.reserve(ROWS * 2 * COLS);
    auto V = [&](int row, int col) {
        VtxPos v;
        v.x = (float)col / (COLS - 1) * 2.0f - 1.0f;
        v.y = (float)row / (ROWS - 1) * 2.0f - 1.0f;
        verts.push_back(v);
    };
    for (int r = 0; r < ROWS - 1; r++)
        for (int c = 0; c < COLS; c++)
        {
            int col = (r % 2) ? (COLS - c - 1) : c; // serpentine strip
            V(r, col);
            V(r + 1, col);
        }
    gRibbonVerts = (UINT)verts.size(); // 8064

    D3D11_BUFFER_DESC bd = {};
    bd.Usage = D3D11_USAGE_IMMUTABLE;
    bd.ByteWidth = (UINT)(verts.size() * sizeof(VtxPos));
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA sd = {}; sd.pSysMem = verts.data();
    gDev->CreateBuffer(&bd, &sd, &gRibbonVB);
}

static void BuildQuad()
{
    // TRIANGLESTRIP covering NDC [-1,1]
    VtxPosTex q[4] = {
        { -1, -1, 0, 1 }, { -1, 1, 0, 0 }, { 1, -1, 1, 1 }, { 1, 1, 1, 0 },
    };
    D3D11_BUFFER_DESC bd = {};
    bd.Usage = D3D11_USAGE_IMMUTABLE;
    bd.ByteWidth = sizeof(q);
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA sd = {}; sd.pSysMem = q;
    gDev->CreateBuffer(&bd, &sd, &gQuadVB);
}

// The "Custom" theme (index kThemeCount): a vertical gradient built from gThemeTop / gThemeBot.
static GradientTheme gCustomTheme = { "Custom", {{0,0,0,1},{0,0,0,1},{0,0,0,1},{0,0,0,1}} };
static void BuildCustomTheme()
{
    // corner order c[0]=BL c[1]=BR c[2]=TL c[3]=TR (see UpdateGradientVB). Vertical gradient:
    // bottom colour on BL/BR, top colour on TL/TR.
    for (int k = 0; k < 3; ++k) { gCustomTheme.c[0][k] = gThemeBot[k]; gCustomTheme.c[1][k] = gThemeBot[k]; }
    for (int k = 0; k < 3; ++k) { gCustomTheme.c[2][k] = gThemeTop[k]; gCustomTheme.c[3][k] = gThemeTop[k]; }
    gCustomTheme.c[0][3] = gCustomTheme.c[1][3] = gCustomTheme.c[2][3] = gCustomTheme.c[3][3] = 1.0f;
}
static bool RibbonIsCustom()
{
    if (!BgIsRibbon() || gRibbonChoice < 0 || gRibbonChoice >= (int)gRibbonList.size()) return false;
    const char* n = gPipes[gRibbonList[gRibbonChoice]].name;
    return n && strcmp(n, "Custom Colour Ribbon") == 0;
}
static void UpdateGradientVB(int theme)
{
    const GradientTheme& t = (theme >= 0 && theme < kThemeCount) ? kThemes[theme] : gCustomTheme;
    // RetroArch corner order (xmb.c: "vertex coords bottom-up: BL BR TL TR"):
    // c[0]=BL  c[1]=BR  c[2]=TL  c[3]=TR. NDC y=+1 is top.
    VtxPosCol q[4] = {
        { -1, -1, t.c[0][0], t.c[0][1], t.c[0][2], t.c[0][3] }, // BL
        { -1,  1, t.c[2][0], t.c[2][1], t.c[2][2], t.c[2][3] }, // TL
        {  1, -1, t.c[1][0], t.c[1][1], t.c[1][2], t.c[1][3] }, // BR
        {  1,  1, t.c[3][0], t.c[3][1], t.c[3][2], t.c[3][3] }, // TR
    };
    if (!gGradVB)
    {
        D3D11_BUFFER_DESC bd = {};
        bd.Usage = D3D11_USAGE_DYNAMIC;
        bd.ByteWidth = sizeof(q);
        bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        D3D11_SUBRESOURCE_DATA sd = {}; sd.pSysMem = q;
        gDev->CreateBuffer(&bd, &sd, &gGradVB);
    }
    else
    {
        D3D11_MAPPED_SUBRESOURCE m;
        gCtx->Map(gGradVB, 0, D3D11_MAP_WRITE_DISCARD, 0, &m);
        memcpy(m.pData, q, sizeof(q));
        gCtx->Unmap(gGradVB, 0);
    }
}

// ------------------------------- device / states -------------------------------
static void InitTextLayer(IDXGIDevice* dxgiDev)
{
    D2D1_FACTORY_OPTIONS opts = {};
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
            __uuidof(ID2D1Factory1), &opts, (void**)&gD2DFactory)))
        Fatal("D2D1CreateFactory failed.");
    if (FAILED(gD2DFactory->CreateDevice(dxgiDev, &gD2DDevice)))
        Fatal("ID2D1Factory1::CreateDevice failed.");
    gD2DDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &gD2DCtx);

    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
            __uuidof(IDWriteFactory), (IUnknown**)&gDWrite)))
        Fatal("DWriteCreateFactory failed.");
    gDWrite->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 26.0f, L"en-us", &gTextFmt);

    gD2DCtx->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 0.92f), &gBrush);
    gD2DCtx->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0, 0.6f), &gShadow);
    gD2DCtx->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 1.0f), &gIconBrush); // icon tint (recoloured per draw)
    gD2DCtx->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0, 0.0f), &gClearBrush); // transparent (blink colon)
}

// Create the D3D render target and the D2D target that shares the same back buffer.
static void CreateTargets()
{
    ID3D11Texture2D* bb = nullptr;
    gSwap->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&bb);
    gDev->CreateRenderTargetView(bb, nullptr, &gRTV);
    bb->Release();

    IDXGISurface* surf = nullptr;
    gSwap->GetBuffer(0, __uuidof(IDXGISurface), (void**)&surf);
    D2D1_BITMAP_PROPERTIES1 props = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE));
    gD2DCtx->CreateBitmapFromDxgiSurface(surf, &props, &gD2DTarget);
    surf->Release();
    gD2DCtx->SetTarget(gD2DTarget);
}

static void CreateDevice()
{
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT; // required for Direct2D interop
#ifdef _DEBUG
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
    D3D_FEATURE_LEVEL fl = D3D_FEATURE_LEVEL_11_0;
    if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
            &fl, 1, D3D11_SDK_VERSION, &gDev, nullptr, &gCtx)))
        Fatal("D3D11CreateDevice failed.");

    // Reach the DXGI factory that owns our adapter.
    IDXGIDevice* dxgiDev = nullptr;
    IDXGIAdapter* adapter = nullptr;
    IDXGIFactory2* factory = nullptr;
    gDev->QueryInterface(__uuidof(IDXGIDevice), (void**)&dxgiDev);
    dxgiDev->GetAdapter(&adapter);
    adapter->GetParent(__uuidof(IDXGIFactory2), (void**)&factory);

    // Flip-model swapchain with the frame-latency-waitable flag.
    DXGI_SWAP_CHAIN_DESC1 scd = {};
    scd.Width = gW;
    scd.Height = gH;
    scd.Format = DXGI_FORMAT_B8G8R8A8_UNORM; // BGRA for Direct2D compatibility
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.BufferCount = 2;
    scd.SampleDesc.Count = 1;
    scd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    scd.Flags = kSwapFlags;

    IDXGISwapChain1* sc1 = nullptr;
    if (FAILED(factory->CreateSwapChainForHwnd(gDev, gHwnd, &scd, nullptr, nullptr, &sc1)))
        Fatal("CreateSwapChainForHwnd failed.");
    gSwap = sc1; // base interface (keeps the create ref)
    sc1->QueryInterface(__uuidof(IDXGISwapChain2), (void**)&gSwap2);

    // Queue at most one frame ahead, and grab the waitable object.
    gSwap2->SetMaximumFrameLatency(1);
    gWaitable = gSwap2->GetFrameLatencyWaitableObject();

    InitTextLayer(dxgiDev);

    factory->Release();
    adapter->Release();
    dxgiDev->Release();

    CreateTargets();
}

static void CreateStates()
{
    D3D11_BLEND_DESC bd = {};
    bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

    // opaque
    bd.RenderTarget[0].BlendEnable = FALSE;
    gDev->CreateBlendState(&bd, &gBlendOpaque);

    // ribbon pipeline: DEST_COLOR / ONE
    bd.RenderTarget[0].BlendEnable = TRUE;
    bd.RenderTarget[0].SrcBlend = D3D11_BLEND_DEST_COLOR;
    bd.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;
    bd.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    // The alpha channel must NOT use a color-based factor (D3D forbids it and
    // CreateBlendState fails). RetroArch overrides only the RGB Src/Dest here and
    // leaves alpha as standard SRC_ALPHA / INV_SRC_ALPHA.
    bd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_SRC_ALPHA;
    bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    bd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    if (FAILED(gDev->CreateBlendState(&bd, &gBlendPipeline)))
        Fatal("CreateBlendState (pipeline) failed.");

    // standard alpha
    bd.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    bd.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    bd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_SRC_ALPHA;
    bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    gDev->CreateBlendState(&bd, &gBlendAlpha);

    // additive (glints add up): SRC_ALPHA / ONE
    bd.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    bd.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;
    bd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;
    gDev->CreateBlendState(&bd, &gBlendAdditive);

    D3D11_RASTERIZER_DESC rd = {};
    rd.FillMode = D3D11_FILL_SOLID;
    rd.CullMode = D3D11_CULL_NONE;
    rd.DepthClipEnable = FALSE;
    gDev->CreateRasterizerState(&rd, &gRaster);

    D3D11_BUFFER_DESC cbd = {};
    cbd.Usage = D3D11_USAGE_DYNAMIC;
    cbd.ByteWidth = sizeof(Uniform);
    cbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    gDev->CreateBuffer(&cbd, nullptr, &gCB);

    D3D11_BUFFER_DESC rcbd = {};
    rcbd.Usage = D3D11_USAGE_DYNAMIC;
    rcbd.ByteWidth = sizeof(RibbonUniform);
    rcbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    rcbd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    gDev->CreateBuffer(&rcbd, nullptr, &gRibbonCB);
}

static void InitPipelines()
{
    // --- gradient (background) ---
    {
        ID3DBlob* vsb = nullptr;
        MakeVSPS(kGradientShader, &gGradVS, &gGradPS, &vsb);
        D3D11_INPUT_ELEMENT_DESC e[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        };
        gDev->CreateInputLayout(e, 2, vsb->GetBufferPointer(), vsb->GetBufferSize(), &gGradLayout);
        vsb->Release();
    }

    // --- ribbon input layout (POSITION float2 only) ---
    D3D11_INPUT_ELEMENT_DESC ribbonEl[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };
    // --- quad input layout (POSITION + TEXCOORD) ---
    D3D11_INPUT_ELEMENT_DESC quadEl[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };

    ID3D11VertexShader* vs; ID3D11PixelShader* ps; ID3DBlob* vsb;

    // 0: ribbon
    MakeVSPS(std::string(kRibbonCommon) + kRibbonBody, &vs, &ps, &vsb);
    if (!gRibbonLayout)
        gDev->CreateInputLayout(ribbonEl, 1, vsb->GetBufferPointer(), vsb->GetBufferSize(), &gRibbonLayout);
    vsb->Release();
    gPipes.push_back({ "Ribbon", vs, ps, gRibbonLayout, &gRibbonVB, sizeof(VtxPos), &gRibbonVerts, 0, &gBlendPipeline, true });

    // 1: simple ribbon
    MakeVSPS(std::string(kRibbonCommon) + kRibbonSimpleBody, &vs, &ps, &vsb);
    vsb->Release();
    gPipes.push_back({ "Simple Ribbon", vs, ps, gRibbonLayout, &gRibbonVB, sizeof(VtxPos), &gRibbonVerts, 0, &gBlendPipeline, true });

    // 2b: custom colour ribbon (translucent, alpha blend) - colour/opacity from b1 (settings later)
    MakeVSPS(std::string(kRibbonCommon) + kRibbonCustomBody, &vs, &ps, &vsb);
    vsb->Release();
    gPipes.push_back({ "Custom Colour Ribbon", vs, ps, gRibbonLayout, &gRibbonVB, sizeof(VtxPos), &gRibbonVerts, 0, &gBlendAlpha, true });

    // 2: simple snow (baseScale 1.25, density 0.5, speed 0.15)
    {
        std::string s = std::string(kQuadCommon) + kSnowBody;
        s = Replace(s, "%BASESCALE%", "1.25"); s = Replace(s, "%DENSITY%", "0.5"); s = Replace(s, "%SPEED%", "0.15");
        MakeVSPSEntry(s, "VSMainTex", "PSMain", &vs, &ps, &vsb);
        if (!gQuadLayout)
            gDev->CreateInputLayout(quadEl, 2, vsb->GetBufferPointer(), vsb->GetBufferSize(), &gQuadLayout);
        vsb->Release();
        gPipes.push_back({ "Simple Snow", vs, ps, gQuadLayout, &gQuadVB, sizeof(VtxPosTex), nullptr, 4, &gBlendAlpha });
    }
    // 3: snow (baseScale 3.5, density 0.7, speed 0.25)
    {
        std::string s = std::string(kQuadCommon) + kSnowBody;
        s = Replace(s, "%BASESCALE%", "3.5"); s = Replace(s, "%DENSITY%", "0.7"); s = Replace(s, "%SPEED%", "0.25");
        MakeVSPSEntry(s, "VSMainTex", "PSMain", &vs, &ps, &vsb); vsb->Release();
        gPipes.push_back({ "Snow", vs, ps, gQuadLayout, &gQuadVB, sizeof(VtxPosTex), nullptr, 4, &gBlendAlpha });
    }
    // 4: bokeh
    {
        std::string s = std::string(kQuadCommon) + kBokehBody;
        MakeVSPSEntry(s, "VSMainPos", "PSMain", &vs, &ps, &vsb); vsb->Release();
        gPipes.push_back({ "Bokeh", vs, ps, gQuadLayout, &gQuadVB, sizeof(VtxPosTex), nullptr, 4, &gBlendAlpha });
    }

    // --- original background pipelines (backgrounds.h) ---
    auto addBg = [&](const char* name, const char* body)
    {
        ID3D11VertexShader* v; ID3D11PixelShader* p; ID3DBlob* b;
        std::string src = std::string(kQuadCommon) + kNoise + body;
        MakeVSPSEntry(src, "VSMainPos", "PSMain", &v, &p, &b);
        b->Release();
        gPipes.push_back({ name, v, p, gQuadLayout, &gQuadVB, sizeof(VtxPosTex), nullptr, 4, &gBlendAlpha });
    };
    addBg("Caustics",         kBgCaustics);
    addBg("Synthwave Road",   kBgSynthRoad);
    addBg("Orbs",             kBgOrbs);

    // Split pipelines into independently-selectable ribbon vs background lists.
    for (int i = 0; i < (int)gPipes.size(); i++)
        (gPipes[i].isRibbon ? gRibbonList : gBgList).push_back(i);
    // Default look: Background = Ribbon (Custom Colour, the last ribbon) over the Dark theme gradient.
    gRibbonChoice = (int)gRibbonList.size() - 1;  // Custom Colour Ribbon
    gBgChoice     = BgRibbonChoice();             // "Ribbon" option
    gTheme = 0;                                   // default to RetroArch's near-black "Dark" theme
    for (int i = 0; i < kThemeCount; i++)
        if (strcmp(kThemes[i].name, "Dark") == 0) { gTheme = i; break; }
    ApplyBgSelection();                           // derive gBgSel / gRibbonSel from the choice
    UpdateGradientVB(gTheme);                     // refresh gradient VB for the chosen default theme
}

// ------------------------------- render ----------------------------------------
static void Render(float dtMs)
{
    // Animation clock. 0.01/frame at 60 Hz == 0.6/sec, so seconds*0.6 stays pixel-identical to
    // RetroArch at 60 Hz while being correct at any refresh rate. dt is clamped in the main loop.
    float dt = dtMs * 0.001f;
    gTime += dt * 0.6f;
    if (gTime > 65536.0f) gTime -= 65536.0f;

    // Advance menu tweens + global fade.
    UpdateListSwitch(dtMs);   // fixed-mode deferred list animation: fire it once the selector glide settles
    gCatTween.update(dtMs);
    for (Tween& t : gItemYT) t.update(dtMs);
    for (Tween& t : gItemZT) t.update(dtMs);
    for (Tween& t : gItemAT) t.update(dtMs);
    for (Tween& t : gItemXT) t.update(dtMs);
    gListFade.update(dtMs);
    gFixItemScrollT.update(dtMs); gFixItemSelT.update(dtMs);   // fixed-placement slide
    gFixCatScrollT.update(dtMs);  gFixCatSelT.update(dtMs);
    {
        float step = dtMs / 250.0f; // ~250 ms menu fade
        if      (gMenuAlpha < gMenuAlphaTarget) gMenuAlpha = (gMenuAlpha + step > gMenuAlphaTarget) ? gMenuAlphaTarget : gMenuAlpha + step;
        else if (gMenuAlpha > gMenuAlphaTarget) gMenuAlpha = (gMenuAlpha - step < gMenuAlphaTarget) ? gMenuAlphaTarget : gMenuAlpha - step;
    }

    // constant buffer
    {
        Uniform u;
        XMStoreFloat4x4(&u.mvp, XMMatrixIdentity());
        u.outputSize = XMFLOAT2((float)gW, (float)gH);
        u.time = gTime;
        u.alpha = 1.0f;
        D3D11_MAPPED_SUBRESOURCE m;
        gCtx->Map(gCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &m);
        memcpy(m.pData, &u, sizeof(u));
        gCtx->Unmap(gCB, 0);
    }

    // custom-ribbon constant buffer (b1): color + opacity, bound for all PS (unused by others)
    {
        RibbonUniform ru = {};
        float rcr = gRibbonColor.x, rcg = gRibbonColor.y, rcb = gRibbonColor.z;
        if (gRibbonCycle && RibbonIsCustom())          // slow continuous hue drift (leaves gRibbonColor intact)
        {
            gRibbonCycleHue += dt * kRibbonCycleDegPerSec;
            if (gRibbonCycleHue >= 360.0f) gRibbonCycleHue -= 360.0f;
            HsvToRgb(gRibbonCycleHue, 1.0f, 1.0f, rcr, rcg, rcb);
        }
        ru.color[0] = rcr; ru.color[1] = rcg;
        ru.color[2] = rcb; ru.color[3] = gRibbonColor.w;
        ru.opacity = gRibbonOpacity;
        ru.sheen   = (gRibbonSheen < 0.1f) ? 0.1f : gRibbonSheen;   // avoid divide-by-zero in the shader
        D3D11_MAPPED_SUBRESOURCE m;
        gCtx->Map(gRibbonCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &m);
        memcpy(m.pData, &ru, sizeof(ru));
        gCtx->Unmap(gRibbonCB, 0);
        gCtx->PSSetConstantBuffers(1, 1, &gRibbonCB);
    }

    float black[4] = { 0, 0, 0, 1 };
    gCtx->OMSetRenderTargets(1, &gRTV, nullptr);
    gCtx->ClearRenderTargetView(gRTV, black);
    D3D11_VIEWPORT vp = { 0, 0, (float)gW, (float)gH, 0, 1 };
    gCtx->RSSetViewports(1, &vp);
    gCtx->RSSetState(gRaster);
    gCtx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

    UINT off = 0;

    // 1) gradient background (opaque)
    {
        gCtx->OMSetBlendState(gBlendOpaque, nullptr, 0xffffffff);
        gCtx->IASetInputLayout(gGradLayout);
        UINT stride = sizeof(VtxPosCol);
        gCtx->IASetVertexBuffers(0, 1, &gGradVB, &stride, &off);
        gCtx->VSSetShader(gGradVS, nullptr, 0);
        gCtx->PSSetShader(gGradPS, nullptr, 0);
        gCtx->Draw(4, 0);
    }

    // helper: draw one pipeline (background or ribbon) over whatever is already in the target
    auto drawPipe = [&](const Pipeline& p)
    {
        gCtx->OMSetBlendState(*p.blend, nullptr, 0xffffffff);
        gCtx->IASetInputLayout(p.layout);
        gCtx->IASetVertexBuffers(0, 1, p.vb, &p.stride, &off);
        gCtx->VSSetShader(p.vs, nullptr, 0);
        gCtx->PSSetShader(p.ps, nullptr, 0);
        gCtx->VSSetConstantBuffers(0, 1, &gCB);
        gCtx->PSSetConstantBuffers(0, 1, &gCB);
        UINT vc = p.vcount ? *p.vcount : p.fixedCount;
        gCtx->Draw(vc, 0);
    };

    // 2) selected background (if not "None")
    if (gBgSel < (int)gBgList.size())
        drawPipe(gPipes[gBgList[gBgSel]]);

    // 3) selected ribbon overlay (if not "None") - brightens whatever is beneath it
    if (gRibbonSel < (int)gRibbonList.size())
        drawPipe(gPipes[gRibbonList[gRibbonSel]]);

    // 4) menu + edit overlay drawn as D2D over the composite
    {
        MenuLayout L = ComputeLayout(gW, gH);
        DrawMenu(L);
        DrawEditOverlay(L);   // no-op unless edit mode is active
    }

    // Adaptive vsync to save CPU. Full refresh while the user is interacting (smooth nav), but pace DOWN
    // when nothing needs it: half rate once the menu has settled, slower still once fully in the ribbon
    // screensaver, and slowest when we are not even the foreground window. The frame-latency-waitable loop
    // follows Present, so a higher sync interval cuts how often we render (and the CPU cost) without
    // touching any menu logic. Any input resets gIdleTimer -> we snap back to full refresh next frame.
    UINT sync = 1;
    if (!gHasFocus)                              sync = 4;   // occluded behind a launched app: minimal (~15fps @60Hz)
    else if (gScreensaver && gMenuAlpha < 0.02f) sync = 2;   // fully in the screensaver: half refresh (~30fps @60Hz) - was 3 (~20fps), bumped for a smoother ribbon
    else if (gIdleTimer > 1500.0f)               sync = 2;   // settled menu: half refresh
    gSwap->Present(sync, 0);
}

// ------------------------------- input -----------------------------------------
// Directional nav uses RA's auto-repeat state machine (menu_driver.c): the first press fires
// immediately, then after an initial delay (menu_scroll_delay ~256 ms) it repeats every 33.33 ms
// while held. Keyboard arrows and the XInput D-pad both feed it, polled each frame so the OS key-
// repeat rate is bypassed. OK/Cancel/quit stay discrete (edge). bg/ribbon/theme are fixed at the
// InitPipelines default (dev-cycling dropped).
static const float kNavInitialDelay = 256.0f; // menu_scroll_delay default (ms)
static const float kNavRepeat       = 33.33f; // RA repeat interval (ms)
static MenuAction  gHeldNav    = ACT_NONE;
static float       gNavAccum   = 0.0f;
static bool        gNavInitial = true;         // next threshold uses the initial delay

// --- idle / screensaver (Phase 2.6 stub; full impl Phase 4) ---
// No input for kIdleTimeout ms -> the menu fades out (gMenuAlphaTarget=0) leaving the ribbon-only
// screensaver; any input restores it. Timeout is fixed for now (becomes a setting in Phase 4).
// (gIdleTimer, gHasFocus, gScreensaver, gScreensaverEnabled, gIdleTimeoutMs, gSaverClock* declared earlier)

// Register user activity: reset the idle clock and, if the screensaver is up, wake the menu.
static void NoteActivity()
{
    gIdleTimer = 0.0f;
    if (gScreensaver) { gScreensaver = false; gMenuAlphaTarget = 1.0f; }
}

// Quit request from top-level browse. RetroArch-style: first press arms a prompt, a second press within
// the window exits. (Called instead of PostQuitMessage in the browse quit paths.)
static void RequestQuit()
{
    if (gQuitPromptTimer > 0.0f) PostQuitMessage(0);   // second press while armed -> exit
    else gQuitPromptTimer = 2000.0f;                   // first press: arm + show the prompt (~2s)
}

static void UpdateNavRepeat(MenuAction cur, float dtMs)
{
    if (cur == ACT_NONE) { gHeldNav = ACT_NONE; gNavAccum = 0.0f; gNavInitial = true; return; }
    if (cur != gHeldNav)                       // fresh press: fire now, start initial delay
    {
        gHeldNav = cur; gNavAccum = 0.0f; gNavInitial = true;
        Dispatch(cur);
        return;
    }
    gNavAccum += dtMs;                          // same direction held
    float threshold = gNavInitial ? kNavInitialDelay : kNavRepeat;
    if (gNavAccum >= threshold)
    {
        Dispatch(cur);
        gNavAccum   = 0.0f;
        gNavInitial = false;
    }
}

static void PollInput(float dtMs)
{
    // Only act on input while the launcher is the foreground window. XInput reads GLOBAL pad state, so
    // without this the menu would react to the pad while another app runs on top (keyboard is message-based
    // via gKeyDown, which is inherently per-window). Reset the idle timer so the screensaver does not build
    // up while another window has focus.
    if (GetForegroundWindow() != gHwnd) { gIdleTimer = 0.0f; gHasFocus = false; return; }
    gHasFocus = true;

    // Held directional state from keyboard + D-pad (single direction; vertical takes priority). The
    // gArrowTap latch (set on the WM_KEYDOWN edge) is OR-ed in so an injected tap whose KEYUP already
    // cleared gKeyDown is still seen as "down" this frame; it is consumed (cleared) right after so the
    // tap lasts exactly one frame - one nav step, no runaway repeat. Physical holds keep gKeyDown set
    // across frames, so their RetroArch auto-repeat is unaffected.
    bool up = gKeyDown[VK_UP]    || gArrowTap[VK_UP];
    bool dn = gKeyDown[VK_DOWN]  || gArrowTap[VK_DOWN];
    bool lf = gKeyDown[VK_LEFT]  || gArrowTap[VK_LEFT];
    bool rt = gKeyDown[VK_RIGHT] || gArrowTap[VK_RIGHT];
    gArrowTap[VK_UP] = gArrowTap[VK_DOWN] = gArrowTap[VK_LEFT] = gArrowTap[VK_RIGHT] = false;

    static WORD prev = 0;
    WORD b = 0;
    XINPUT_STATE st;
    // Calling XInputGetState on a DISCONNECTED slot every frame is expensive (documented). While a pad is
    // connected we poll every frame; when none is present we re-probe only about once a second.
    static bool  padConnected = true;    // probe on the very first frame
    static float padProbeMs   = 0.0f;
    padProbeMs -= dtMs;
    if (padConnected || padProbeMs <= 0.0f)
    {
        if (XInputGetState(0, &st) == ERROR_SUCCESS)
        {
            padConnected = true;
            b = st.Gamepad.wButtons;
            up |= (b & XINPUT_GAMEPAD_DPAD_UP)    != 0;
            dn |= (b & XINPUT_GAMEPAD_DPAD_DOWN)  != 0;
            lf |= (b & XINPUT_GAMEPAD_DPAD_LEFT)  != 0;
            rt |= (b & XINPUT_GAMEPAD_DPAD_RIGHT) != 0;
        }
        else { padConnected = false; padProbeMs = 1000.0f; }
    }
    MenuAction nav = up ? ACT_UP : dn ? ACT_DOWN : lf ? ACT_LEFT : rt ? ACT_RIGHT : ACT_NONE;
    WORD pressed   = b & ~prev;

    if (gQuitPromptTimer > 0.0f) { gQuitPromptTimer -= dtMs; if (gQuitPromptTimer < 0.0f) gQuitPromptTimer = 0.0f; }
    if (gSettingsHintMs  > 0.0f) { gSettingsHintMs  -= dtMs; if (gSettingsHintMs  < 0.0f) gSettingsHintMs  = 0.0f; }

    // Idle / screensaver: any nav (held) or fresh button press counts as activity. When the
    // screensaver is up, the first input only wakes the menu (it is not also acted on), so a
    // wake press does not accidentally navigate or launch.
    bool activity = (nav != ACT_NONE) || (pressed != 0);
    bool wasSaver = gScreensaver;
    if (activity) NoteActivity();
    else          gIdleTimer += dtMs;
    // Idle-fade to the screensaver from ANY view (browse, Settings, editor...); the overlay fades
    // out with the menu and comes back on wake, restoring whatever mode the user was in.
    if (gScreensaverEnabled && !gScreensaver && gIdleTimer >= gIdleTimeoutMs)
        { gScreensaver = true; gMenuAlphaTarget = 0.0f; }
    if (wasSaver && activity) { prev = b; return; }   // consume the wake input this frame

    UpdateNavRepeat(nav, dtMs);

    // OK = gamepad A or keyboard Enter. In the plain browse view a SHORT press launches and a LONG press
    // (>= kOkLongPressMs) opens Settings - TV-remote friendly, since many remotes only have a D-pad + OK.
    // In every other UI mode OK acts on press, as before. (Enter in other modes is dispatched from WndProc;
    // here we watch the held state so a long hold in browse can be detected.)
    {
        bool okDown = (b & XINPUT_GAMEPAD_A) != 0;   // keyboard Enter is dispatched from WndProc (WM_KEYDOWN edge)
        bool browseLaunch = (gUi == UI_BROWSE && !gEditMode && !gPosMode);
        static bool okPrev = false, okFromBrowse = false, okLong = false;
        static float okHoldMs = 0.0f;
        if (okDown && !okPrev)                            // press
        {
            okHoldMs = 0.0f; okLong = false; okFromBrowse = browseLaunch;
            if (!browseLaunch) Dispatch(ACT_OK);         // other UI modes act immediately on press
        }
        else if (okDown && okPrev && okFromBrowse)       // held in the browse view
        {
            okHoldMs += dtMs;
            if (!okLong && okHoldMs >= kOkLongPressMs) { OpenSettings(); okLong = true; }
        }
        else if (!okDown && okPrev)                      // release
        {
            if (okFromBrowse && !okLong) Dispatch(ACT_OK);   // short press in browse -> launch the item
            okFromBrowse = false; okLong = false;
        }
        okPrev = okDown;
    }
    if (pressed & XINPUT_GAMEPAD_B)
    { if (gUi != UI_BROWSE) Dispatch(ACT_CANCEL);               // close settings popup / action / files / gallery
      else if (gEditMode || gPosMode) Dispatch(ACT_CANCEL);     // cancel move/position mode, else leave edit mode
      else RequestQuit(); }                                     // top level: two-press exit
    // Shoulders switch menu tabs (categories), RetroArch-style - so on the Settings tab, where dpad
    // Left/Right change a value, you can still move between tabs.
    if (gUi == UI_BROWSE && !gEditMode && !gPosMode)
    {
        if (pressed & XINPUT_GAMEPAD_LEFT_SHOULDER)  MoveCategory(-1);
        if (pressed & XINPUT_GAMEPAD_RIGHT_SHOULDER) MoveCategory(+1);
    }
    // Back/Select and Start open the Settings popup (and close it). In edit mode they leave edit mode.
    if (pressed & (XINPUT_GAMEPAD_BACK | XINPUT_GAMEPAD_START))
    { if (gUi != UI_BROWSE) Dispatch(ACT_CANCEL); else if (gEditMode || gPosMode) Dispatch(ACT_CANCEL);
      else OpenSettings(); }
    prev = b;
}

static LRESULT CALLBACK WndProc(HWND h, UINT msg, WPARAM w, LPARAM l)
{
    switch (msg)
    {
    case WM_CHAR:
        // While the on-screen keyboard is open, hardware typing goes straight into the field.
        if (gUi == UI_KEYBOARD)     { NoteActivity(); KbChar((wchar_t)w); return 0; }
        return 0;
    case WM_KEYDOWN:
    {
        gKeyDown[w & 0xFF] = true;             // track held state for PollInput (arrow-nav + Enter); works remotely
        bool repeat = (l & 0x40000000) != 0;   // bit 30: key was already down (OS auto-repeat)
        // Injected-tap latch (see gArrowTap): on a fresh (non-repeat) arrow press, mark the direction so
        // PollInput registers one nav step even if the matching WM_KEYUP arrives in the same pump pass
        // (before the next frame). Only the four arrows; OS auto-repeats are ignored (physical holds are
        // handled by the gKeyDown poll, which keeps the RetroArch repeat cadence).
        if (!repeat && (w == VK_LEFT || w == VK_UP || w == VK_RIGHT || w == VK_DOWN))
            gArrowTap[w & 0xFF] = true;
        // Any key is activity. If the screensaver was up, the first press only wakes the menu
        // (see PollInput) - swallow the discrete action so a wake press does not also act.
        bool saver = gScreensaver;
        NoteActivity();
        if (saver) return 0;

        // Esc = exit: close the open popup / menu / mode; from the plain main menu it exits the app.
        if (w == VK_ESCAPE)
        {
            if (gUi == UI_KEYBOARD)      { KbCancelEdit(); return 0; }        // close the on-screen keyboard
            if (gUi == UI_FILES)         { gEditField = EF_NONE; gUi = gFilesReturn; return 0; } // Esc EXITS the browser (does not go up a dir)
            if (gUi != UI_BROWSE)        { Dispatch(ACT_CANCEL); return 0; } // close settings popup / action / browser
            if (gEditMode || gPosMode)   { Dispatch(ACT_CANCEL); return 0; } // leave edit / cancel move / cancel position
            RequestQuit(); return 0;                                         // main menu: exit the app (two-press)
        }
        // Backspace = BACK: pop the current sub-mode / leave edit / leave the Settings tab. On the main
        // menu (nothing to back out of) it behaves like Esc and exits. In text entry (on-screen keyboard)
        // it stays a character delete (handled via WM_CHAR).
        if (w == VK_BACK && gUi != UI_KEYBOARD)
        {
            if (gUi != UI_BROWSE)        { Dispatch(ACT_CANCEL); return 0; }
            if (gEditMode || gPosMode)   { Dispatch(ACT_CANCEL); return 0; }
            RequestQuit(); return 0;     // main menu: Backspace == Esc == exit
        }
        // Enter (OK) is dispatched HERE, on the key-down edge (once per press - OS auto-repeats are
        // ignored). This is required for remote-desktop tools (RustDesk/RDP) that inject Enter as a
        // WM_KEYDOWN + WM_KEYUP pair with ~0ms between them: the once-per-frame gKeyDown poll in PollInput
        // would set and clear the flag within a single message-pump cycle and never sample it as pressed.
        // The gamepad A button keeps its press/hold logic in PollInput (short = OK, long = Settings);
        // opening Settings from the keyboard is still available via Tab / 'S'.
        if (w == VK_RETURN) { if (!repeat) Dispatch(ACT_OK); return 0; }
        // Open the Settings screen from browse (everything - orientation, placement, clock,
        // screensaver, and the menu editor - is changed there, not via stray hotkeys). 'S' or Tab.
        if ((w == 'S' || w == VK_TAB) && !repeat && gUi == UI_BROWSE && !gPosMode) { OpenSettings(); return 0; }
        // '[' / ']' switch menu tabs (categories), the keyboard equivalent of the gamepad shoulders.
        if ((w == VK_OEM_4 || w == VK_OEM_6) && !repeat && gUi == UI_BROWSE && !gEditMode && !gPosMode)
        { MoveCategory(w == VK_OEM_4 ? -1 : +1); return 0; }
        return 0;
    }
    case WM_KEYUP:
        gKeyDown[w & 0xFF] = false;            // release: clear held state (mirrors WM_KEYDOWN)
        return 0;
    case WM_KILLFOCUS:
        memset(gKeyDown, 0, sizeof(gKeyDown)); // lost focus mid-hold: drop all keys so none stick
        memset(gArrowTap, 0, sizeof(gArrowTap)); // and drop any un-consumed tap latch
        return 0;
    case WM_SIZE:
        if (gSwap && w != SIZE_MINIMIZED)
        {
            gW = LOWORD(l); gH = HIWORD(l);
            if (gD2DCtx) gD2DCtx->SetTarget(nullptr);
            if (gD2DTarget) { gD2DTarget->Release(); gD2DTarget = nullptr; }
            if (gRTV) { gRTV->Release(); gRTV = nullptr; }
            gSwap->ResizeBuffers(0, gW, gH, DXGI_FORMAT_UNKNOWN, kSwapFlags);
            CreateTargets();
            CreateMenuFormats(); // font sizes scale with resolution
        }
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, msg, w, l);   // MUST be the W version: this is a Unicode window (RegisterClassExW).
                                           // The unsuffixed DefWindowProc = DefWindowProcA (UNICODE is not
                                           // defined) mangled the wide caption L"Launcher" to "L" (it read the
                                           // UTF-16 bytes 4C 00.. as ANSI and stopped at the NUL after 'L').
}

// ------------------------------- entry -----------------------------------------
int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int)
{
    srand((unsigned)GetTickCount64());   // seed the random screensaver-clock corner picker

    // Single instance: an autostart entry and a manual launch (or a double-click on top of the shell
    // instance) can race. Keep only the first; the handle stays open for the process lifetime.
    HANDLE inst = CreateMutexW(nullptr, TRUE, L"LauncherSingleInstanceMutex");
    if (inst && GetLastError() == ERROR_ALREADY_EXISTS) return 0;

    // (No explicit AppUserModelID: it grouped the process under a "Launcher" app node in Task Manager.
    // The taskbar/Alt-Tab caption issue was actually the DefWindowProcA bug, now fixed, so the AUMID is
    // not needed - dropping it leaves a single plain process entry.)

    // Per-monitor-v2 DPI (robust across SDKs)
    {
        typedef BOOL(WINAPI * FnSetCtx)(DPI_AWARENESS_CONTEXT);
        HMODULE u = GetModuleHandleW(L"user32.dll");
        auto fn = (FnSetCtx)GetProcAddress(u, "SetProcessDpiAwarenessContext");
        if (fn) fn(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    }

    WNDCLASSEXW wc = { sizeof(wc) };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = L"LauncherWnd";
    // Application icon (ribbon wave, from app.rc). hIcon = large (Alt-Tab / task view), hIconSm = title bar.
    wc.hIcon   = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(IDI_APPICON), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE | LR_SHARED);
    wc.hIconSm = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(IDI_APPICON), IMAGE_ICON, 16, 16, LR_SHARED);
    RegisterClassExW(&wc);

    // Borderless fullscreen on the primary monitor
    HMONITOR mon = MonitorFromPoint({ 0, 0 }, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi = { sizeof(mi) };
    GetMonitorInfo(mon, &mi);
    int mx = mi.rcMonitor.left, my = mi.rcMonitor.top;
    gW = mi.rcMonitor.right - mi.rcMonitor.left;
    gH = mi.rcMonitor.bottom - mi.rcMonitor.top;

    // WS_EX_APPWINDOW: force this borderless WS_POPUP to be treated as a first-class taskbar application
    // window, so the taskbar/Alt-Tab use the real window caption ("Launcher") instead of a placeholder
    // /truncated label.
    gHwnd = CreateWindowExW(WS_EX_APPWINDOW, wc.lpszClassName, L"Launcher",
        WS_POPUP | WS_VISIBLE, mx, my, gW, gH, nullptr, nullptr, hInst, nullptr);
    SetWindowTextW(gHwnd, L"Launcher");   // belt-and-suspenders: ensure the caption the taskbar reads

    CreateDevice();
    CreateStates();
    BuildRibbonGrid();
    BuildQuad();
    UpdateGradientVB(gTheme);
    InitPipelines();
    CreateJob();        // process-tracking job (Phase 3 launching)
    BuildMenu();        // loads launcher.json (creates a default on first run)
    UpdateGradientVB(gTheme);   // config may have selected a different theme than the InitPipelines default
    CreateMenuFormats();
    InitIcons();

    ShowWindow(gHwnd, SW_SHOW);
    SetForegroundWindow(gHwnd);
    SetFocus(gHwnd);        // ensure keyboard focus so WM_CHAR is delivered (icon search / on-screen keyboard typing)
    KeepAwake(true);        // inhibit the OS screensaver + auto-sleep while we're foreground

    // Cold-boot focus: bump to the TOP of the Z-order without STAYING topmost (staying topmost would sit
    // over launched apps). The gBootFocusMs loop below keeps re-asserting foreground for a few seconds so
    // explorer + other startup apps racing us at sign-in cannot leave the launcher stuck behind them.
    SetWindowPos(gHwnd, HWND_TOPMOST,   0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    SetWindowPos(gHwnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    gLaunchedAsShell = KioskEnabled();   // if we ARE the Windows shell this session, restore a desktop on exit
    if (gFirstRun) EnterEditMode();      // fresh install (no launcher.json): land in edit mode on the blank starter category

    QueryPerformanceFrequency(&gQpcFreq);
    QueryPerformanceCounter(&gQpcLast);

    MSG m = {};
    for (;;)
    {
        while (PeekMessage(&m, nullptr, 0, 0, PM_REMOVE))
        {
            if (m.message == WM_QUIT) { SetThreadExecutionState(ES_CONTINUOUS);
                if (gLaunchedAsShell) LaunchExplorer();   // we were the shell: give the user a desktop back
                return 0; }
            TranslateMessage(&m);
            DispatchMessage(&m);
        }

        // A tracked app (waitForExit) may be running on top; detect its exit so we can regain focus.
        PumpRunningProcess();

        // Occluded (not the foreground window - e.g. a launched app is on top, or the user alt-tabbed
        // away) and past the boot-focus grab window: STOP rendering entirely. Nothing we draw is visible,
        // so drawing the ribbon behind another window is pure wasted CPU/GPU. Idle the thread instead and
        // keep pumping messages + the process watch so we still wake when the app exits. On return we reset
        // the frame clock so dt does not spike - the ribbon resumes from the exact phase it paused at (no
        // jump), and gTime only advances inside Render so it is simply paused, not reset. Reset the idle
        // timer too so the screensaver starts a fresh countdown when we come back, not mid-way.
        if (GetForegroundWindow() != gHwnd && gBootFocusMs <= 0.0f)
        {
            gHasFocus = false; gIdleTimer = 0.0f;
            // Return-to-home watchdog: when we are NOT tracking an app we launched (that has its own
            // return logic), and the desktop would otherwise be showing (an app closed, hid to tray, or
            // minimised), reclaim the foreground after a short debounce so brief app-switch transitions
            // do not bounce us up.
            if (gReturnHome && gTrackMode == TRACK_NONE && DesktopWouldShow(gHwnd))
            {
                gHomeIdleMs += 60.0f;                        // this branch sleeps ~60ms per iteration
                if (gHomeIdleMs >= 450.0f) { ForceForeground(gHwnd); gHomeIdleMs = 0.0f; }
            }
            else gHomeIdleMs = 0.0f;
            Sleep(60);
            QueryPerformanceCounter(&gQpcLast);
            continue;
        }
        gHomeIdleMs = 0.0f;   // foreground again: reset the watchdog debounce

        // Block until the present queue is ready for a new frame, so we render
        // just-in-time (lower, steadier latency than blocking only in Present).
        if (gWaitable)
            WaitForSingleObjectEx(gWaitable, 1000, TRUE);
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        float dt = (float)(now.QuadPart - gQpcLast.QuadPart) / (float)gQpcFreq.QuadPart;
        gQpcLast = now;
        if (dt > 0.1f) dt = 0.1f;      // clamp a stall so nothing jumps
        float dtMs = dt * 1000.0f;
        // Cold-boot foreground grab: for the first few seconds re-assert the foreground each frame if
        // something else stole it (startup apps at sign-in). Reuses the launch-side ForceForeground dance.
        if (gBootFocusMs > 0.0f)
        {
            if (GetForegroundWindow() != gHwnd) ForceForeground(gHwnd);
            gBootFocusMs -= dtMs;
        }
        PollInput(dtMs);
        Render(dtMs);
    }
}
