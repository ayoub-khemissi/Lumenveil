#include "overlay.h"

#include "belt_lantern.h"
#include "config.h"
#include "draw_log.h"
#include "hooks.h"
#include "log.h"
#include "region_panel.h"
#include "regions.h"
#include "settings_panel.h"
#include "spots.h"
#include "terrain_relief.h"

#include "imgui.h"
#include "imgui_impl_dx9.h"
#include "imgui_impl_win32.h"

#include <windowsx.h>

#include <algorithm>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND window, UINT msg, WPARAM wParam, LPARAM lParam);

namespace
{
constexpr LPARAM kKeyWasDownBit = static_cast<LPARAM>(1) << 30;
constexpr DWORD kMaxRenderTargets = 4;
constexpr DWORD kAllColorChannels =
    D3DCOLORWRITEENABLE_RED | D3DCOLORWRITEENABLE_GREEN | D3DCOLORWRITEENABLE_BLUE | D3DCOLORWRITEENABLE_ALPHA;
constexpr float kPanelFontPixels = 16.0f;
constexpr float kReferenceBackBufferHeight = 1080.0f;
constexpr float kMinFrameSeconds = 1.0f / 1000.0f;
constexpr int kMaxLoggedShortcutKeys = 12;
constexpr float kToastHeightShare = 0.82f;
constexpr float kToastBackgroundAlpha = 0.65f;

enum DrawSkip : unsigned
{
    kDeviceNotReady = 1u << 0,
    kNoBackBuffer = 1u << 1,
    kNoStateCapture = 1u << 2,
};

struct OverlayState
{
    IDirect3DDevice9* device = nullptr;
    bool visible = false;
    bool regionsVisible = false;
    bool failed = false;
    unsigned swallowedKey = 0;
    UINT backBufferWidth = 0;
    UINT backBufferHeight = 0;
    float scale = 0.0f;
    SettingsPanel panel;
    bool keyInputLogged = false;
    bool drawLogged = false;
    int loggedShortcutKeys = 0;
    unsigned loggedDrawSkips = 0;
    unsigned clientHeldButtons = 0;
    unsigned panelHeldButtons = 0;
    bool releaseFocusOnNextFrame = false;
};

struct OverlayFrame
{
    IDirect3DSurface9* backBuffer;
    IDirect3DStateBlock9* state;
    IDirect3DSurface9* targets[kMaxRenderTargets];
    IDirect3DSurface9* depth;
    IDirect3DVertexBuffer9* stream;
    UINT streamOffset;
    UINT streamStride;
    IDirect3DIndexBuffer9* indices;
    D3DVIEWPORT9 viewport;
    RECT scissor;
    bool captured;
    bool sceneOpen;
};

struct SubclassedWindow
{
    HWND window = nullptr;
    WNDPROC clientProc = nullptr;
    bool unicode = false;
};

OverlayState g_overlay;
SubclassedWindow g_subclass;

LRESULT CALLBACK OverlayWindowProc(HWND window, UINT msg, WPARAM wParam, LPARAM lParam);

LONG_PTR CurrentWindowProc(const SubclassedWindow& s)
{
    return s.unicode ? GetWindowLongPtrW(s.window, GWLP_WNDPROC) : GetWindowLongPtrA(s.window, GWLP_WNDPROC);
}

LONG_PTR ReplaceWindowProc(HWND window, bool unicode, LONG_PTR proc)
{
    return unicode ? SetWindowLongPtrW(window, GWLP_WNDPROC, proc) : SetWindowLongPtrA(window, GWLP_WNDPROC, proc);
}

bool RestoreClientWindowProc()
{
    if (!g_subclass.window)
        return true;
    if (CurrentWindowProc(g_subclass) != reinterpret_cast<LONG_PTR>(&OverlayWindowProc))
        return false;
    ReplaceWindowProc(g_subclass.window, g_subclass.unicode, reinterpret_cast<LONG_PTR>(g_subclass.clientProc));
    g_subclass = SubclassedWindow();
    return true;
}

bool ChainOverlayWindowProc(HWND window)
{
    if (g_subclass.window == window)
        return true;
    if (!RestoreClientWindowProc())
        return false;
    const bool unicode = IsWindowUnicode(window) != FALSE;
    const LONG_PTR clientProc = ReplaceWindowProc(window, unicode, reinterpret_cast<LONG_PTR>(&OverlayWindowProc));
    if (!clientProc)
        return false;
    g_subclass.window = window;
    g_subclass.clientProc = reinterpret_cast<WNDPROC>(clientProc);
    g_subclass.unicode = unicode;
    return true;
}

POINT ClientToBackBuffer(HWND window, POINT p)
{
    RECT client;
    if (!g_overlay.backBufferWidth || !GetClientRect(window, &client) || client.right <= 0 || client.bottom <= 0)
        return p;
    return {MulDiv(p.x, static_cast<int>(g_overlay.backBufferWidth), client.right),
            MulDiv(p.y, static_cast<int>(g_overlay.backBufferHeight), client.bottom)};
}

LPARAM InBackBufferPixels(HWND window, UINT msg, LPARAM lParam)
{
    if (msg != WM_MOUSEMOVE)
        return lParam;
    const POINT p = ClientToBackBuffer(window, {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)});
    return MAKELPARAM(p.x, p.y);
}

void FeedCursorPosition(HWND window)
{
    POINT p;
    if (!window || !GetCursorPos(&p) || !ScreenToClient(window, &p))
        return;
    p = ClientToBackBuffer(window, p);
    ImGui::GetIO().AddMousePosEvent(static_cast<float>(p.x), static_cast<float>(p.y));
}

bool ModifierDown(int virtualKey)
{
    return GetKeyState(virtualKey) < 0;
}

void FeedModifierState(ImGuiIO& io)
{
    io.AddKeyEvent(ImGuiMod_Ctrl, ModifierDown(VK_CONTROL));
    io.AddKeyEvent(ImGuiMod_Shift, ModifierDown(VK_SHIFT));
    io.AddKeyEvent(ImGuiMod_Alt, ModifierDown(VK_MENU));
    io.AddKeyEvent(ImGuiMod_Super, ModifierDown(VK_LWIN) || ModifierDown(VK_RWIN));
}

void SetPanels(bool settings, bool regions)
{
    if ((settings == g_overlay.visible && regions == g_overlay.regionsVisible) || !ImGui::GetCurrentContext())
        return;
    const bool anyBefore = g_overlay.visible || g_overlay.regionsVisible;
    if (settings != g_overlay.visible)
        VF_LOG_INFO("overlay %s", settings ? "shown" : "hidden");
    if (regions != g_overlay.regionsVisible)
        VF_LOG_INFO("region panel %s", regions ? "shown" : "hidden");
    g_overlay.visible = settings;
    g_overlay.regionsVisible = regions;
    g_overlay.releaseFocusOnNextFrame = true;
    ImGuiIO& io = ImGui::GetIO();
    io.ClearInputKeys();
    io.ClearInputMouse();
    if (!anyBefore && (settings || regions))
        FeedCursorPosition(g_subclass.window);
}

void SetVisible(bool visible)
{
    SetPanels(visible, g_overlay.regionsVisible);
}

void HidePanels()
{
    SetPanels(false, false);
}

bool IsKeyDownMessage(UINT msg)
{
    return msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN;
}

bool IsKeyUpMessage(UINT msg)
{
    return msg == WM_KEYUP || msg == WM_SYSKEYUP;
}

bool IsCharacterMessage(UINT msg)
{
    return msg == WM_CHAR || msg == WM_SYSCHAR || msg == WM_DEADCHAR || msg == WM_SYSDEADCHAR;
}

bool IsMouseClickOrWheelMessage(UINT msg)
{
    return msg > WM_MOUSEMOVE && msg <= WM_MOUSELAST;
}

bool IsImeMessage(UINT msg)
{
    return msg == WM_IME_COMPOSITION || msg == WM_IME_CHAR;
}

unsigned MouseButtonBit(UINT msg, WPARAM wParam)
{
    switch (msg)
    {
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_LBUTTONDBLCLK:
        return 1u << 0;
    case WM_RBUTTONDOWN:
    case WM_RBUTTONUP:
    case WM_RBUTTONDBLCLK:
        return 1u << 1;
    case WM_MBUTTONDOWN:
    case WM_MBUTTONUP:
    case WM_MBUTTONDBLCLK:
        return 1u << 2;
    case WM_XBUTTONDOWN:
    case WM_XBUTTONUP:
    case WM_XBUTTONDBLCLK:
        return GET_XBUTTON_WPARAM(wParam) == XBUTTON1 ? 1u << 3 : 1u << 4;
    default:
        return 0;
    }
}

bool IsMouseButtonUp(UINT msg)
{
    return msg == WM_LBUTTONUP || msg == WM_RBUTTONUP || msg == WM_MBUTTONUP || msg == WM_XBUTTONUP;
}

bool KeepsButtonWithItsDown(UINT msg, WPARAM wParam, bool takenByPanel)
{
    const unsigned button = MouseButtonBit(msg, wParam);
    if (!button)
        return takenByPanel;
    if (IsMouseButtonUp(msg))
    {
        const bool clientOwned = (g_overlay.clientHeldButtons & button) != 0;
        const bool panelOwned = (g_overlay.panelHeldButtons & button) != 0;
        g_overlay.clientHeldButtons &= ~button;
        g_overlay.panelHeldButtons &= ~button;
        return clientOwned ? false : panelOwned || takenByPanel;
    }
    if (takenByPanel)
    {
        g_overlay.panelHeldButtons |= button;
        g_overlay.clientHeldButtons &= ~button;
    }
    else
    {
        g_overlay.clientHeldButtons |= button;
        g_overlay.panelHeldButtons &= ~button;
    }
    return takenByPanel;
}

bool IsHotkeyKey(const Hotkey& key, WPARAM wParam)
{
    if (wParam == key.virtualKey)
        return true;
    return key.ctrl && wParam == VK_CANCEL && (key.virtualKey == VK_PAUSE || key.virtualKey == VK_SCROLL);
}

bool HotkeyPressed(const Hotkey& key, UINT msg, WPARAM wParam)
{
    return IsKeyDownMessage(msg) && IsHotkeyKey(key, wParam) && ModifierDown(VK_CONTROL) == key.ctrl &&
           ModifierDown(VK_SHIFT) == key.shift && ModifierDown(VK_MENU) == key.alt;
}

bool IsTypedKey(unsigned virtualKey)
{
    return (virtualKey >= '0' && virtualKey <= '9') || (virtualKey >= 'A' && virtualKey <= 'Z') ||
           (virtualKey >= VK_NUMPAD0 && virtualKey <= VK_DIVIDE) ||
           (virtualKey >= VK_OEM_1 && virtualKey <= VK_OEM_102) || virtualKey == VK_SPACE;
}

bool IsModifierKey(unsigned virtualKey)
{
    return virtualKey == VK_CONTROL || virtualKey == VK_SHIFT || virtualKey == VK_MENU ||
           (virtualKey >= VK_LSHIFT && virtualKey <= VK_RMENU) || virtualKey == VK_LWIN || virtualKey == VK_RWIN;
}

void LogShortcutKeyArrival(const Hotkey& hotkey, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (!IsKeyDownMessage(msg) || (lParam & kKeyWasDownBit))
        return;
    if (!g_overlay.keyInputLogged)
    {
        g_overlay.keyInputLogged = true;
        VF_LOG_INFO("overlay: key presses reach the game window");
    }
    const unsigned virtualKey = static_cast<unsigned>(wParam);
    const Hotkey pressed = {virtualKey, ModifierDown(VK_CONTROL), ModifierDown(VK_SHIFT), ModifierDown(VK_MENU)};
    const bool shortcut = (pressed.ctrl || pressed.alt || pressed.shift) && !IsTypedKey(virtualKey);
    if ((virtualKey != hotkey.virtualKey && !shortcut) || IsModifierKey(virtualKey) ||
        g_overlay.loggedShortcutKeys >= kMaxLoggedShortcutKeys)
        return;
    ++g_overlay.loggedShortcutKeys;
    VF_LOG_INFO("overlay: %s pressed (OverlayKey is %s)", HotkeyName(pressed).c_str(), HotkeyName(hotkey).c_str());
}

void LogDrawSkip(DrawSkip reason, const char* what, HRESULT hr)
{
    if (g_overlay.loggedDrawSkips & reason)
        return;
    g_overlay.loggedDrawSkips |= reason;
    VF_LOG_INFO("overlay not drawn: %s (0x%08X)", what, static_cast<unsigned>(hr));
}

enum class HotkeyAction
{
    Settings,
    Regions,
    PlainGame,
};

void RunHotkeyAction(HotkeyAction action)
{
    if (action == HotkeyAction::Regions)
        SetPanels(g_overlay.visible, !g_overlay.regionsVisible);
    else if (action == HotkeyAction::Settings)
        SetVisible(!g_overlay.visible);
    else
        PressToggleKey();
}

bool TakesHotkey(const Hotkey& key, HotkeyAction action, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (HotkeyPressed(key, msg, wParam))
    {
        if (!(lParam & kKeyWasDownBit))
            RunHotkeyAction(action);
        g_overlay.swallowedKey = static_cast<unsigned>(wParam);
        return true;
    }
    if (!g_overlay.swallowedKey)
        return false;
    if (IsKeyUpMessage(msg) && (wParam == g_overlay.swallowedKey || IsHotkeyKey(key, wParam)))
    {
        g_overlay.swallowedKey = 0;
        return true;
    }
    return IsCharacterMessage(msg);
}

bool PanelWantsMessage(UINT msg)
{
    const ImGuiIO& io = ImGui::GetIO();
    if (IsMouseClickOrWheelMessage(msg))
        return io.WantCaptureMouse;
    if (IsKeyDownMessage(msg) || IsCharacterMessage(msg))
        return io.WantCaptureKeyboard;
    return false;
}

bool PanelTakesMessage(HWND window, UINT msg, WPARAM wParam, LPARAM lParam)
{
    const Config& cfg = GlobalConfig().Get();
    if (TakesHotkey(cfg.toggleKey, HotkeyAction::PlainGame, msg, wParam, lParam))
        return true;
    if (!cfg.overlay)
    {
        HidePanels();
        return false;
    }
    LogShortcutKeyArrival(cfg.overlayKey, msg, wParam, lParam);
    if (cfg.drawLog != kDrawLogOff && HotkeyPressed(cfg.drawLogKey, msg, wParam) && !(lParam & kKeyWasDownBit))
        DrawLogArm();
    if (HotkeyPressed(cfg.lanternKey, msg, wParam) && !(lParam & kKeyWasDownBit))
        PressLanternKey();
    if (HotkeyPressed(cfg.spotKey, msg, wParam) && !(lParam & kKeyWasDownBit))
        ArmSpot();
    if (HotkeyPressed(cfg.reliefKey, msg, wParam) && !(lParam & kKeyWasDownBit))
        PressReliefKey();
    if (TakesHotkey(cfg.overlayKey, HotkeyAction::Settings, msg, wParam, lParam) ||
        TakesHotkey(cfg.regionKey, HotkeyAction::Regions, msg, wParam, lParam))
        return true;
    if (!g_overlay.visible && !g_overlay.regionsVisible)
        return false;
    if (IsImeMessage(msg) && !ImGui::GetIO().WantTextInput)
        return false;
    const LPARAM scaled = InBackBufferPixels(window, msg, lParam);
    const LRESULT handled = ImGui_ImplWin32_WndProcHandler(window, msg, wParam, scaled);
    if (IsImeMessage(msg))
        return handled != 0;
    return PanelWantsMessage(msg);
}

bool OverlayTakesMessage(HWND window, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (g_overlay.failed || !g_overlay.device)
        return false;
    return KeepsButtonWithItsDown(msg, wParam, PanelTakesMessage(window, msg, wParam, lParam));
}

int OverlayFaultFilter(unsigned code, const char* where)
{
    VF_LOG_ERROR("exception 0x%08X in the overlay %s; overlay disabled for this session", code, where);
    return EXCEPTION_EXECUTE_HANDLER;
}

bool OverlayTakesMessageGuarded(HWND window, UINT msg, WPARAM wParam, LPARAM lParam)
{
    __try
    {
        return OverlayTakesMessage(window, msg, wParam, lParam);
    }
    __except (OverlayFaultFilter(GetExceptionCode(), "window message"))
    {
        g_overlay.failed = true;
        g_overlay.visible = false;
        g_overlay.regionsVisible = false;
        return false;
    }
}

LRESULT CALLBACK OverlayWindowProc(HWND window, UINT msg, WPARAM wParam, LPARAM lParam)
{
    const SubclassedWindow subclass = g_subclass;
    if (window == subclass.window && OverlayTakesMessageGuarded(window, msg, wParam, lParam))
        return 0;
    if (msg == WM_NCDESTROY && window == subclass.window)
        g_subclass = SubclassedWindow();
    return subclass.unicode ? CallWindowProcW(subclass.clientProc, window, msg, wParam, lParam)
                            : CallWindowProcA(subclass.clientProc, window, msg, wParam, lParam);
}

void ConfigureImGui()
{
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
    ImFontConfig font;
    font.SizePixels = kPanelFontPixels;
    io.Fonts->AddFontDefaultVector(&font);
    g_overlay.scale = 0.0f;
}

void ApplyScale(float scale)
{
    if (scale == g_overlay.scale)
        return;
    g_overlay.scale = scale;
    ImGuiStyle style;
    ImGui::StyleColorsDark(&style);
    style.ScaleAllSizes(scale);
    style.FontScaleDpi = scale;
    ImGui::GetStyle() = style;
}

void DrawSpotToast(const D3DSURFACE_DESC& backBuffer)
{
    std::string text;
    if (!SpotToast(text))
        return;
    ImGui::SetNextWindowPos(ImVec2(0.5f * static_cast<float>(backBuffer.Width),
                                   kToastHeightShare * static_cast<float>(backBuffer.Height)),
                            ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowBgAlpha(kToastBackgroundAlpha);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
                                   ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
                                   ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;
    if (ImGui::Begin("##spot", nullptr, flags))
        ImGui::TextUnformatted(text.c_str());
    ImGui::End();
}

void BuildPanelFrame(const D3DSURFACE_DESC& backBuffer)
{
    ApplyScale(std::max(1.0f, static_cast<float>(backBuffer.Height) / kReferenceBackBufferHeight));
    ImGui_ImplDX9_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGuiIO& io = ImGui::GetIO();
    FeedModifierState(io);
    io.DisplaySize = ImVec2(static_cast<float>(backBuffer.Width), static_cast<float>(backBuffer.Height));
    io.DeltaTime = std::max(io.DeltaTime, kMinFrameSeconds);
    ImGui::NewFrame();
    if (g_overlay.releaseFocusOnNextFrame)
    {
        g_overlay.releaseFocusOnNextFrame = false;
        ImGui::SetWindowFocus(nullptr);
    }
    bool open = true;
    if (g_overlay.visible)
        g_overlay.panel.Draw(GlobalConfig(), LastFogFrameStatus(), open);
    bool regionsOpen = true;
    if (g_overlay.regionsVisible)
        DrawRegionPanel(regionsOpen);
    DrawSpotToast(backBuffer);
    ImGui::Render();
    if (!open || !regionsOpen)
        SetPanels(g_overlay.visible && open, g_overlay.regionsVisible && regionsOpen);
}

bool CaptureDeviceState(IDirect3DDevice9* device, OverlayFrame& frame)
{
    if (FAILED(device->CreateStateBlock(D3DSBT_ALL, &frame.state)) || FAILED(frame.state->Capture()))
        return false;
    for (DWORD i = 0; i < kMaxRenderTargets; ++i)
        device->GetRenderTarget(i, &frame.targets[i]);
    device->GetDepthStencilSurface(&frame.depth);
    device->GetStreamSource(0, &frame.stream, &frame.streamOffset, &frame.streamStride);
    device->GetIndices(&frame.indices);
    device->GetViewport(&frame.viewport);
    device->GetScissorRect(&frame.scissor);
    frame.captured = frame.targets[0] != nullptr;
    return frame.captured;
}

void RestoreDeviceState(IDirect3DDevice9* device, const OverlayFrame& frame)
{
    for (DWORD i = 0; i < kMaxRenderTargets; ++i)
        if (frame.targets[i])
            device->SetRenderTarget(i, frame.targets[i]);
    device->SetDepthStencilSurface(frame.depth);
    frame.state->Apply();
    device->SetStreamSource(0, frame.stream, frame.streamOffset, frame.streamStride);
    device->SetIndices(frame.indices);
    device->SetViewport(&frame.viewport);
    device->SetScissorRect(&frame.scissor);
}

template <typename T>
void ReleaseReference(T*& object)
{
    if (object)
        object->Release();
    object = nullptr;
}

void EndOverlayFrame(IDirect3DDevice9* device, OverlayFrame& frame)
{
    if (frame.sceneOpen)
        device->EndScene();
    if (frame.captured)
        RestoreDeviceState(device, frame);
    frame.sceneOpen = false;
    frame.captured = false;
    for (IDirect3DSurface9*& target : frame.targets)
        ReleaseReference(target);
    ReleaseReference(frame.depth);
    ReleaseReference(frame.stream);
    ReleaseReference(frame.indices);
    ReleaseReference(frame.state);
    ReleaseReference(frame.backBuffer);
}

void SetStateImGuiLeavesAlone(IDirect3DDevice9* device)
{
    device->SetRenderState(D3DRS_COLORWRITEENABLE, kAllColorChannels);
    device->SetRenderState(D3DRS_SRGBWRITEENABLE, FALSE);
    device->SetRenderState(D3DRS_CLIPPLANEENABLE, 0);
    device->SetRenderState(D3DRS_WRAP0, 0);
    device->SetRenderState(D3DRS_VERTEXBLEND, D3DVBF_DISABLE);
    device->SetRenderState(D3DRS_INDEXEDVERTEXBLENDENABLE, FALSE);
    device->SetRenderState(D3DRS_BLENDOPALPHA, D3DBLENDOP_ADD);
    device->SetTextureStageState(0, D3DTSS_TEXCOORDINDEX, 0);
    device->SetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
    device->SetTextureStageState(0, D3DTSS_RESULTARG, D3DTA_CURRENT);
    device->SetSamplerState(0, D3DSAMP_SRGBTEXTURE, FALSE);
    device->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
    device->SetSamplerState(0, D3DSAMP_MAXMIPLEVEL, 0);
}

void RenderPanel(IDirect3DDevice9* device, OverlayFrame& frame)
{
    if (!CaptureDeviceState(device, frame))
    {
        LogDrawSkip(kNoStateCapture, "the device state could not be captured", E_FAIL);
        return;
    }
    frame.sceneOpen = SUCCEEDED(device->BeginScene());
    device->SetRenderTarget(0, frame.backBuffer);
    for (DWORD i = 1; i < kMaxRenderTargets; ++i)
        if (frame.targets[i])
            device->SetRenderTarget(i, nullptr);
    device->SetDepthStencilSurface(nullptr);
    SetStateImGuiLeavesAlone(device);
    ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
}

void DrawOverlayFrame(IDirect3DDevice9* device, OverlayFrame& frame)
{
    if (!GlobalConfig().Get().overlay)
    {
        HidePanels();
        return;
    }
    const HRESULT cooperativeLevel = device->TestCooperativeLevel();
    if (cooperativeLevel != D3D_OK)
    {
        LogDrawSkip(kDeviceNotReady, "the device is not ready", cooperativeLevel);
        return;
    }
    const HRESULT backBufferResult = device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &frame.backBuffer);
    if (FAILED(backBufferResult))
    {
        LogDrawSkip(kNoBackBuffer, "no back buffer", backBufferResult);
        return;
    }
    D3DSURFACE_DESC desc = {};
    frame.backBuffer->GetDesc(&desc);
    g_overlay.backBufferWidth = desc.Width;
    g_overlay.backBufferHeight = desc.Height;
    BuildPanelFrame(desc);
    RenderPanel(device, frame);
    if (!g_overlay.drawLogged)
    {
        g_overlay.drawLogged = true;
        VF_LOG_INFO("overlay drawn over the %ux%u back buffer", desc.Width, desc.Height);
    }
}

void EndOverlayFrameGuarded(IDirect3DDevice9* device, OverlayFrame& frame)
{
    __try
    {
        EndOverlayFrame(device, frame);
    }
    __except (OverlayFaultFilter(GetExceptionCode(), "frame cleanup"))
    {
        g_overlay.failed = true;
        g_overlay.visible = false;
        g_overlay.regionsVisible = false;
    }
}

void DrawOverlayGuarded(IDirect3DDevice9* device)
{
    OverlayFrame frame = {};
    __try
    {
        DrawOverlayFrame(device, frame);
    }
    __except (OverlayFaultFilter(GetExceptionCode(), "frame"))
    {
        g_overlay.failed = true;
        g_overlay.visible = false;
        g_overlay.regionsVisible = false;
    }
    EndOverlayFrameGuarded(device, frame);
}

int RegionsFaultFilter(unsigned code)
{
    VF_LOG_ERROR("exception 0x%08X in the region controls; they are off for this session", code);
    return EXCEPTION_EXECUTE_HANDLER;
}

bool g_regionsFailed = false;

void RegionsAtPresentGuarded()
{
    if (g_regionsFailed)
        return;
    __try
    {
        RegionsAtPresent();
    }
    __except (RegionsFaultFilter(GetExceptionCode()))
    {
        g_regionsFailed = true;
    }
}

void ShutDownImGui()
{
    ImGui_ImplDX9_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
}
}

void AttachOverlay(IDirect3DDevice9* device, HWND window)
{
    if (g_overlay.failed || !device || !window)
        return;
    if (g_overlay.device)
        DetachOverlay(g_overlay.device);
    if (!ChainOverlayWindowProc(window))
    {
        VF_LOG_ERROR("overlay: window %p could not be subclassed; overlay off", static_cast<void*>(window));
        return;
    }
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ConfigureImGui();
    if (!ImGui_ImplWin32_Init(window))
    {
        ImGui::DestroyContext();
        RestoreClientWindowProc();
        VF_LOG_ERROR("overlay: the ImGui Win32 backend failed to start; overlay off");
        return;
    }
    ImGui_ImplDX9_Init(device);
    g_overlay.device = device;
    VF_LOG_INFO("overlay attached to window %p; %s opens it", static_cast<void*>(window),
                HotkeyName(GlobalConfig().Get().overlayKey).c_str());
}

void DetachOverlay(IDirect3DDevice9* device)
{
    if (!device || device != g_overlay.device)
        return;
    HidePanels();
    ShutDownImGui();
    const bool failed = g_overlay.failed;
    g_overlay = OverlayState();
    g_overlay.failed = failed;
    if (!RestoreClientWindowProc())
        VF_LOG_INFO("overlay detached; the window procedure stays chained because another hook follows it");
}

void ReleaseOverlayDeviceObjects(IDirect3DDevice9* device)
{
    if (device && device == g_overlay.device)
        ImGui_ImplDX9_InvalidateDeviceObjects();
}

void DrawOverlay(IDirect3DDevice9* device)
{
    RegionsAtPresentGuarded();
    std::string toast;
    const bool wanted = g_overlay.visible || g_overlay.regionsVisible || SpotToast(toast);
    if (wanted && !g_overlay.failed && device && device == g_overlay.device)
        DrawOverlayGuarded(device);
}

bool OverlayVisible()
{
    return g_overlay.visible;
}

bool RegionPanelVisible()
{
    return g_overlay.regionsVisible;
}
