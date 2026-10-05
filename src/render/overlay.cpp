#include "overlay.h"

#include "../core/common.h"
#include "../core/fileutil.h"
#include "../core/log.h"
#include "../core/strutil.h"
#include "input.h"

#if OMM_WINDOWS

#include <d3d11.h>
#include <d3d9.h>
#include <dxgi.h>

#include "MinHook.h"
#include "backends/imgui_impl_dx11.h"
#include "backends/imgui_impl_dx9.h"
#include "backends/imgui_impl_win32.h"
#include "imgui.h"

// imgui_impl_dx11.cpp is compiled with -DD3DCompile=OMM_D3DCompile so the mod
// does not import d3dcompiler_xx.dll (missing on some systems).
extern "C" HRESULT WINAPI OMM_D3DCompile(LPCVOID data, SIZE_T size, LPCSTR name, const void* defines, void* include,
                                         LPCSTR entry, LPCSTR target, UINT flags1, UINT flags2, void** code,
                                         void** errors) {
    using Fn = HRESULT(WINAPI*)(LPCVOID, SIZE_T, LPCSTR, const void*, void*, LPCSTR, LPCSTR, UINT, UINT, void**,
                                void**);
    static Fn fn = nullptr;
    if (!fn) {
        for (const wchar_t* dll : {L"d3dcompiler_47.dll", L"d3dcompiler_46.dll", L"d3dcompiler_43.dll"}) {
            HMODULE m = LoadLibraryW(dll);
            if (m && (fn = reinterpret_cast<Fn>(reinterpret_cast<void*>(GetProcAddress(m, "D3DCompile"))))) break;
        }
    }
    return fn ? fn(data, size, name, defines, include, entry, target, flags1, flags2, code, errors) : E_FAIL;
}

namespace omm::render {

namespace {
SetupCallback g_setup = nullptr;
FrameCallback g_frame = nullptr;
volatile Backend g_backend = Backend::None;
bool g_imgui = false;
unsigned long long g_frames = 0;
float g_fps = 0.f;
LARGE_INTEGER g_lastTick{}, g_freq{};

// D3D9 state
using Present9Fn = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*, const RECT*, const RECT*, HWND, const RGNDATA*);
using Reset9Fn = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*, D3DPRESENT_PARAMETERS*);
Present9Fn g_oPresent9 = nullptr;
Reset9Fn g_oReset9 = nullptr;
IDirect3DDevice9* g_dev9 = nullptr;

// D3D11 state
using Present11Fn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT);
using Resize11Fn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);
Present11Fn g_oPresent11 = nullptr;
Resize11Fn g_oResize11 = nullptr;
IDXGISwapChain* g_swap = nullptr;
ID3D11Device* g_dev11 = nullptr;
ID3D11DeviceContext* g_ctx11 = nullptr;
ID3D11RenderTargetView* g_rtv = nullptr;

HWND CreateDummyWindow() {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"OMM_DummyWindow";
    RegisterClassExW(&wc);
    return CreateWindowExW(0, wc.lpszClassName, L"OMM", WS_OVERLAPPEDWINDOW, 0, 0, 64, 64, nullptr, nullptr,
                           wc.hInstance, nullptr);
}

HMODULE LoadSystemDll(const wchar_t* name) {
    wchar_t path[MAX_PATH] = {};
    UINT n = GetSystemDirectoryW(path, MAX_PATH);
    if (!n || n > MAX_PATH - 32) return nullptr;
    wcscat(path, L"\\");
    wcscat(path, name);
    return LoadLibraryW(path);
}

template <typename Fn>
bool Detour(void* target, void* detour, Fn* original) {
    if (!target) return false;
    MH_STATUS st = MH_CreateHook(target, detour, reinterpret_cast<void**>(original));
    if (st != MH_OK && st != MH_ERROR_ALREADY_CREATED) {
        LOGE("MH_CreateHook(%p) failed: %s", target, MH_StatusToString(st));
        return false;
    }
    st = MH_EnableHook(target);
    return st == MH_OK || st == MH_ERROR_ENABLED;
}

void LoadFonts(ImGuiIO& io, HWND hwnd) {
    RECT rc{};
    GetClientRect(hwnd, &rc);
    float height = static_cast<float>(rc.bottom - rc.top);
    if (height < 480.f) height = 1080.f;
    float size = std::max(15.f, std::min(30.f, 17.f * height / 1080.f));
    wchar_t win[MAX_PATH] = {};
    GetWindowsDirectoryW(win, MAX_PATH);
    std::string fonts = fs::Join(str::WideToUtf8(win), "Fonts");
    ImFontConfig cfg;
    cfg.OversampleH = 2;
    cfg.OversampleV = 1;
    ImFont* body = nullptr;
    for (const char* f : {"segoeui.ttf", "tahoma.ttf", "arial.ttf"}) {
        std::string p = fs::Join(fonts, f);
        if (fs::Exists(p) && (body = io.Fonts->AddFontFromFileTTF(p.c_str(), size, &cfg))) break;
    }
    if (!body) io.Fonts->AddFontDefault();
    // Second font: bold/large for headings (index 1). Falls back to the body font.
    ImFont* heading = nullptr;
    for (const char* f : {"segoeuib.ttf", "tahomabd.ttf", "arialbd.ttf"}) {
        std::string p = fs::Join(fonts, f);
        if (fs::Exists(p) && (heading = io.Fonts->AddFontFromFileTTF(p.c_str(), size * 1.25f, &cfg))) break;
    }
    if (!heading) io.Fonts->AddFontDefault();
}

bool InitImGuiCommon(HWND hwnd) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NoMouseCursorChange;
    LoadFonts(io, hwnd);
    if (!ImGui_ImplWin32_Init(hwnd)) {
        LOGE("ImGui_ImplWin32_Init failed");
        return false;
    }
    input::InstallWndProc(hwnd);
    if (g_setup) g_setup();
    QueryPerformanceFrequency(&g_freq);
    QueryPerformanceCounter(&g_lastTick);
    return true;
}

void TickFps() {
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    double dt = static_cast<double>(now.QuadPart - g_lastTick.QuadPart) / static_cast<double>(g_freq.QuadPart);
    g_lastTick = now;
    if (dt > 0.0) {
        float inst = static_cast<float>(1.0 / dt);
        g_fps = g_fps <= 0.f ? inst : g_fps * 0.95f + inst * 0.05f;
    }
    ++g_frames;
}

void BuildFrame() {
    input::PumpToImGui();
    ImGui_ImplWin32_NewFrame();
    ImGuiIO& io = ImGui::GetIO();
    io.MouseDrawCursor = input::MenuOpen();
    ImGui::NewFrame();
    if (g_frame) g_frame();
    ImGui::Render();
    TickFps();
    if ((g_frames % 120) == 0) input::CheckWndProc();
}

// ---------------------------------------------------------------- D3D9 --------
HWND WindowOf(IDirect3DDevice9* dev) {
    HWND hwnd = nullptr;
    IDirect3DSwapChain9* sc = nullptr;
    if (SUCCEEDED(dev->GetSwapChain(0, &sc)) && sc) {
        D3DPRESENT_PARAMETERS pp{};
        if (SUCCEEDED(sc->GetPresentParameters(&pp))) hwnd = pp.hDeviceWindow;
        sc->Release();
    }
    if (!hwnd) {
        D3DDEVICE_CREATION_PARAMETERS cp{};
        if (SUCCEEDED(dev->GetCreationParameters(&cp))) hwnd = cp.hFocusWindow;
    }
    return hwnd;
}

void RenderD3D9(IDirect3DDevice9* dev) {
    if (g_backend == Backend::D3D11) return;
    if (!g_imgui) {
        HWND hwnd = WindowOf(dev);
        if (!hwnd || !InitImGuiCommon(hwnd)) return;
        if (!ImGui_ImplDX9_Init(dev)) {
            LOGE("ImGui_ImplDX9_Init failed");
            return;
        }
        g_dev9 = dev;
        g_backend = Backend::D3D9;
        g_imgui = true;
        LOGI("Overlay initialised on Direct3D 9 (window %p)", static_cast<void*>(hwnd));
    } else if (dev != g_dev9) {
        return;  // some other device (e.g. a tool's) - leave it alone
    }
    if (dev->TestCooperativeLevel() != D3D_OK) return;
    ImGui_ImplDX9_NewFrame();
    BuildFrame();
    IDirect3DStateBlock9* sb = nullptr;
    if (FAILED(dev->CreateStateBlock(D3DSBT_ALL, &sb)) || !sb) return;
    IDirect3DSurface9* backBuffer = nullptr;
    IDirect3DSurface9* oldTarget = nullptr;
    dev->GetRenderTarget(0, &oldTarget);
    if (SUCCEEDED(dev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &backBuffer)) && backBuffer)
        dev->SetRenderTarget(0, backBuffer);
    dev->SetRenderState(D3DRS_SRGBWRITEENABLE, FALSE);
    if (SUCCEEDED(dev->BeginScene())) {
        ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
        dev->EndScene();
    }
    if (oldTarget) {
        dev->SetRenderTarget(0, oldTarget);
        oldTarget->Release();
    }
    if (backBuffer) backBuffer->Release();
    sb->Apply();
    sb->Release();
}

HRESULT STDMETHODCALLTYPE HkPresent9(IDirect3DDevice9* dev, const RECT* src, const RECT* dst, HWND wnd,
                                     const RGNDATA* dirty) {
    RenderD3D9(dev);
    return g_oPresent9(dev, src, dst, wnd, dirty);
}

HRESULT STDMETHODCALLTYPE HkReset9(IDirect3DDevice9* dev, D3DPRESENT_PARAMETERS* pp) {
    bool ours = g_imgui && g_backend == Backend::D3D9 && dev == g_dev9;
    if (ours) ImGui_ImplDX9_InvalidateDeviceObjects();
    HRESULT hr = g_oReset9(dev, pp);
    if (ours && SUCCEEDED(hr)) ImGui_ImplDX9_CreateDeviceObjects();
    return hr;
}

bool HookD3D9() {
    HMODULE d3d9 = LoadSystemDll(L"d3d9.dll");
    if (!d3d9) return false;
    using CreateFn = IDirect3D9*(WINAPI*)(UINT);
    auto create = reinterpret_cast<CreateFn>(reinterpret_cast<void*>(GetProcAddress(d3d9, "Direct3DCreate9")));
    if (!create) return false;
    IDirect3D9* d3d = create(D3D_SDK_VERSION);
    if (!d3d) return false;
    HWND wnd = CreateDummyWindow();
    D3DPRESENT_PARAMETERS pp{};
    pp.Windowed = TRUE;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.BackBufferFormat = D3DFMT_UNKNOWN;
    pp.BackBufferWidth = 2;
    pp.BackBufferHeight = 2;
    pp.hDeviceWindow = wnd;
    IDirect3DDevice9* dev = nullptr;
    HRESULT hr = d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, wnd,
                                   D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_DISABLE_DRIVER_MANAGEMENT, &pp, &dev);
    if (FAILED(hr)) hr = d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_NULLREF, wnd,
                                           D3DCREATE_SOFTWARE_VERTEXPROCESSING, &pp, &dev);
    bool ok = false;
    if (SUCCEEDED(hr) && dev) {
        void** vt = *reinterpret_cast<void***>(dev);
        ok = Detour(vt[17], reinterpret_cast<void*>(&HkPresent9), &g_oPresent9);
        ok = Detour(vt[16], reinterpret_cast<void*>(&HkReset9), &g_oReset9) && ok;
        dev->Release();
    } else {
        LOGW("Could not create a temporary D3D9 device (0x%08lX)", static_cast<unsigned long>(hr));
    }
    d3d->Release();
    if (wnd) DestroyWindow(wnd);
    return ok;
}

// --------------------------------------------------------------- D3D11 --------
void ReleaseRtv() {
    if (g_rtv) {
        g_rtv->Release();
        g_rtv = nullptr;
    }
}

void RenderD3D11(IDXGISwapChain* sc) {
    if (g_backend == Backend::D3D9) return;
    if (!g_imgui) {
        ID3D11Device* dev = nullptr;
        if (FAILED(sc->GetDevice(__uuidof(ID3D11Device), reinterpret_cast<void**>(&dev))) || !dev) return;
        DXGI_SWAP_CHAIN_DESC desc{};
        sc->GetDesc(&desc);
        if (!desc.OutputWindow || !InitImGuiCommon(desc.OutputWindow)) {
            dev->Release();
            return;
        }
        dev->GetImmediateContext(&g_ctx11);
        if (!ImGui_ImplDX11_Init(dev, g_ctx11)) {
            LOGE("ImGui_ImplDX11_Init failed");
            return;
        }
        g_dev11 = dev;
        g_swap = sc;
        g_backend = Backend::D3D11;
        g_imgui = true;
        LOGI("Overlay initialised on Direct3D 11 (window %p)", static_cast<void*>(desc.OutputWindow));
    } else if (sc != g_swap) {
        return;
    }
    if (!g_rtv) {
        ID3D11Texture2D* bb = nullptr;
        if (FAILED(sc->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&bb))) || !bb) return;
        g_dev11->CreateRenderTargetView(bb, nullptr, &g_rtv);
        bb->Release();
        if (!g_rtv) return;
    }
    ImGui_ImplDX11_NewFrame();
    BuildFrame();
    ID3D11RenderTargetView* prevRtv = nullptr;
    ID3D11DepthStencilView* prevDsv = nullptr;
    g_ctx11->OMGetRenderTargets(1, &prevRtv, &prevDsv);
    g_ctx11->OMSetRenderTargets(1, &g_rtv, nullptr);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    g_ctx11->OMSetRenderTargets(1, &prevRtv, prevDsv);
    if (prevRtv) prevRtv->Release();
    if (prevDsv) prevDsv->Release();
}

HRESULT STDMETHODCALLTYPE HkPresent11(IDXGISwapChain* sc, UINT sync, UINT flags) {
    if (!(flags & DXGI_PRESENT_TEST)) RenderD3D11(sc);
    return g_oPresent11(sc, sync, flags);
}

HRESULT STDMETHODCALLTYPE HkResize11(IDXGISwapChain* sc, UINT count, UINT w, UINT h, DXGI_FORMAT fmt, UINT flags) {
    if (sc == g_swap) ReleaseRtv();
    return g_oResize11(sc, count, w, h, fmt, flags);
}

bool HookD3D11() {
    HMODULE d3d11 = LoadSystemDll(L"d3d11.dll");
    if (!d3d11) return false;
    using CreateFn = HRESULT(WINAPI*)(IDXGIAdapter*, D3D_DRIVER_TYPE, HMODULE, UINT, const D3D_FEATURE_LEVEL*, UINT,
                                      UINT, const DXGI_SWAP_CHAIN_DESC*, IDXGISwapChain**, ID3D11Device**,
                                      D3D_FEATURE_LEVEL*, ID3D11DeviceContext**);
    auto create = reinterpret_cast<CreateFn>(
        reinterpret_cast<void*>(GetProcAddress(d3d11, "D3D11CreateDeviceAndSwapChain")));
    if (!create) return false;
    HWND wnd = CreateDummyWindow();
    DXGI_SWAP_CHAIN_DESC scd{};
    scd.BufferCount = 1;
    scd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    scd.BufferDesc.Width = 2;
    scd.BufferDesc.Height = 2;
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.OutputWindow = wnd;
    scd.SampleDesc.Count = 1;
    scd.Windowed = TRUE;
    scd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    IDXGISwapChain* sc = nullptr;
    ID3D11Device* dev = nullptr;
    ID3D11DeviceContext* ctx = nullptr;
    D3D_FEATURE_LEVEL got;
    HRESULT hr = E_FAIL;
    for (D3D_DRIVER_TYPE type : {D3D_DRIVER_TYPE_HARDWARE, D3D_DRIVER_TYPE_WARP}) {
        hr = create(nullptr, type, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &scd, &sc, &dev, &got, &ctx);
        if (SUCCEEDED(hr)) break;
    }
    bool ok = false;
    if (SUCCEEDED(hr) && sc) {
        void** vt = *reinterpret_cast<void***>(sc);
        ok = Detour(vt[8], reinterpret_cast<void*>(&HkPresent11), &g_oPresent11);
        ok = Detour(vt[13], reinterpret_cast<void*>(&HkResize11), &g_oResize11) && ok;
    }
    if (ctx) ctx->Release();
    if (dev) dev->Release();
    if (sc) sc->Release();
    if (wnd) DestroyWindow(wnd);
    return ok;
}
}  // namespace

void SetCallbacks(SetupCallback setup, FrameCallback frame) {
    g_setup = setup;
    g_frame = frame;
}

bool InstallHooks() {
    MH_STATUS st = MH_Initialize();
    if (st != MH_OK && st != MH_ERROR_ALREADY_INITIALIZED) {
        LOGE("MinHook init failed: %s", MH_StatusToString(st));
        return false;
    }
    bool d9 = HookD3D9();
    bool d11 = HookD3D11();
    LOGI("Present hooks: D3D9 %s, D3D11 %s", d9 ? "ok" : "unavailable", d11 ? "ok" : "unavailable");
    return d9 || d11;
}

void Shutdown() {
    // Hooks stay installed until the process exits: unhooking while the
    // render thread may be inside Present is not safe.
}

Backend ActiveBackend() { return g_backend; }
const char* BackendName() {
    switch (g_backend) {
        case Backend::D3D9: return "Direct3D 9";
        case Backend::D3D11: return "Direct3D 11";
        default: return "waiting for first frame";
    }
}
bool ImGuiReady() { return g_imgui; }
unsigned long long FrameCount() { return g_frames; }
float FrameRate() { return g_fps; }

}  // namespace omm::render

#else

namespace omm::render {
void SetCallbacks(SetupCallback, FrameCallback) {}
bool InstallHooks() { return false; }
void Shutdown() {}
Backend ActiveBackend() { return Backend::None; }
const char* BackendName() { return "none"; }
bool ImGuiReady() { return false; }
unsigned long long FrameCount() { return 0; }
float FrameRate() { return 0.f; }
}  // namespace omm::render

#endif
