#include "taskbar_modes.h"

#include <dwmapi.h>
#include <shellapi.h>

#include <cmath>
#include <cstdint>
#include <iterator>
#include <string>

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")

namespace TaskbarModes {
namespace {

// ---------------------------------------------------------------------------
// Undocumented window composition API (user32.dll). These types are declared
// here in full — no stubs, no dependency on a particular SDK version.
// ---------------------------------------------------------------------------
enum class ACCENT_STATE : DWORD {
    ACCENT_DISABLED                 = 0,
    ACCENT_ENABLE_GRADIENT           = 1,
    ACCENT_ENABLE_TRANSPARENTGRADIENT = 2,
    ACCENT_ENABLE_BLURBEHIND         = 3,
    ACCENT_ENABLE_ACRYLICBLURBEHIND  = 4,
    ACCENT_ENABLE_HOSTBACKDROP       = 5,
    ACCENT_INVALID_STATE             = 6,
};

struct ACCENT_POLICY {
    ACCENT_STATE AccentState;
    DWORD        AccentFlags;
    DWORD        GradientColor;   // ABGR (e.g. 0x00FFFFFF = transparent white tint)
    DWORD        AnimationId;
};

enum WINDOWCOMPOSITIONATTRIB {
    WCA_UNDEFINED                              = 0,
    WCA_NCRENDERING_ENABLED                    = 1,
    WCA_NCRENDERING_POLICY                     = 2,
    WCA_TRANSITIONS_FORCEDISABLED              = 3,
    WCA_ALLOW_NCPAINT                          = 4,
    WCA_CAPTION_BUTTON_BOUNDS                  = 5,
    WCA_NONCLIENT_RTL_LAYOUT                   = 6,
    WCA_FORCE_ICONIC_REPRESENTATION            = 7,
    WCA_FLIP3D_POLICY                          = 8,
    WCA_EXTENDED_FRAME_BOUNDS                  = 9,
    WCA_HAS_ICONIC_BITMAP                      = 10,
    WCA_DISALLOW_PEEK                          = 11,
    WCA_CLOAK                                  = 12,
    WCA_CLOAKED                                = 13,
    WCA_FREEZE_REPRESENTATION                  = 14,
    WCA_EASTER_EGG                             = 15,
    WCA_LEGACY_SHADOW_POLICY                   = 16,
    WCA_LEGACY_LIVE_PREVIEW_HOST               = 17,
    WCA_USE_HOSTBACKDROP_BRUSH_POLICY          = 18,
    WCA_ACCENT_POLICY                          = 19,
    WCA_FLIP3D_POLICY_APPLIED                  = 20,
    WCA_FULLSCREEN_OPTIMIZATION                = 21,
    WCA_USE_ATTACHED_WINDOW_MESSAGE_PUMP       = 22,
    WCA_HOSTBACKDROP_BRUSH_POLICY              = 23,
    WCA_USE_DARK_MODE                          = 24,
    WCA_IMMERSIVE_COLOR_PREFERENCE             = 25,
    WCA_INVALID                                = 26,
};

struct WINDOWCOMPOSITIONATTRIBDATA {
    WINDOWCOMPOSITIONATTRIB Attrib;
    PVOID                   pvData;
    DWORD                   cbData;
};

using PFN_SET_WINDOW_COMPOSITION_ATTRIBUTE =
    BOOL(WINAPI*)(HWND, WINDOWCOMPOSITIONATTRIBDATA*);

// ---------------------------------------------------------------------------
// State. All visual interpolation lives in CSS; Win32 only swaps the carrier
// geometry at moments when the capsule's SCREEN position does not change.
// ---------------------------------------------------------------------------
HWND             g_hwnd           = nullptr;
HWINEVENTHOOK    g_hooks[7]       = {};
bool             g_expanded       = false;   // false = A (capsule), true = B (full)
bool             g_transitioning  = false;   // a CSS transition is running
bool             g_flyoutOpen     = false;
bool             g_appBarRegistered = false;
bool             g_toastAdjusting  = false;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
// All constants are expressed in DIPs (96 DPI) to match the CSS pixel grid
// used by WebView2; convert to physical pixels for SetWindowPos.
int ScaleDip(int value) {
    UINT dpi = g_hwnd ? GetDpiForWindow(g_hwnd) : 96;
    if (!dpi) dpi = 96;
    return MulDiv(value, static_cast<int>(dpi), 96);
}

MONITORINFO GetCurrentMonitorInfo() {
    HMONITOR mon = g_hwnd
        ? MonitorFromWindow(g_hwnd, MONITOR_DEFAULTTONEAREST)
        : MonitorFromPoint(POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY);

    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(mon, &mi)) {
        mi.rcMonitor = { 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN) };
        mi.rcWork = mi.rcMonitor;
    }
    return mi;
}

// The carrier is a constant, full-width, bottom-anchored transparent surface.
// The capsule/flyout are drawn by CSS inside it; clicks outside them fall
// through to the desktop. NEVER resize this HWND for the calendar: that made
// DWM briefly blit the old bottom strip to the top of the window.
RECT ComputeCarrier(bool expanded) {
    (void)expanded;
    (void)g_flyoutOpen;
    RECT mon = GetCurrentMonitorInfo().rcMonitor;
    int width = mon.right - mon.left;
    if (width <= 0) {                       // defensive: never use content width
        mon = { 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN) };
        width = mon.right - mon.left;
    }
    const int h = ScaleDip(CARRIER_HEIGHT_DIP);
    return RECT{ mon.left, mon.bottom - h, mon.left + width, mon.bottom };
}

void MoveTo(const RECT& r) {
    if (!g_hwnd) return;
    SetWindowPos(g_hwnd, HWND_TOPMOST, r.left, r.top,
                 r.right - r.left, r.bottom - r.top,
                 SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOCOPYBITS | SWP_NOREDRAW);
}

// Tell WebView which CSS state to animate to. main.cpp forwards this to the
// page as a web message; the page toggles body.expanded (280 ms transition).
void NotifyWebMode(bool expanded) {
    if (g_hwnd) PostMessageW(g_hwnd, WM_TASKBAR_MODE, expanded ? TRUE : FALSE, 0);
}

// Kill the DWM drop shadow that Windows paints around the glass rectangle.
// CS_DROPSHADOW is not set on the window class either (see main.cpp).
void DisableSystemShadow() {
    if (!g_hwnd) return;
    DWMNCRENDERINGPOLICY policy = DWMNCRP_DISABLED;
    DwmSetWindowAttribute(g_hwnd, DWMWA_NCRENDERING_POLICY,
                          &policy, sizeof(policy));
    // The extended glass is assigned in WM_CREATE; re-assert it after the
    // policy change so per-pixel WebView alpha keeps working.
    const MARGINS margins{ -1, -1, -1, -1 };
    DwmExtendFrameIntoClientArea(g_hwnd, &margins);
}

// ---------------------------------------------------------------------------
// Composition accent. The ORIGINAL project used only DwmExtendFrameIntoClientArea
// (set once in WM_CREATE) plus a transparent WebView surface — no region, no
// DWM attributes. Keep that pipeline; only state B gets the acrylic accent.
// IMPORTANT: call on state changes only, never on every animation tick —
// repeated DWM invalidation is what made the window jitter.
// ---------------------------------------------------------------------------
bool SetAccent(ACCENT_STATE state, DWORD flags, DWORD color) {
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (!user32) return false;
    auto pfn = reinterpret_cast<PFN_SET_WINDOW_COMPOSITION_ATTRIBUTE>(
        GetProcAddress(user32, "SetWindowCompositionAttribute"));
    if (!pfn) return false;

    ACCENT_POLICY policy{};
    policy.AccentState   = state;
    policy.AccentFlags   = flags;
    policy.GradientColor = color; // AABBGGRR
    policy.AnimationId   = 0;

    WINDOWCOMPOSITIONATTRIBDATA data{};
    data.Attrib = WCA_ACCENT_POLICY;
    data.pvData = &policy;
    data.cbData = sizeof(policy);
    return pfn(g_hwnd, &data) != FALSE;
}

// System backdrop/accent is permanently OFF. The frosted-glass material lives
// entirely in CSS and never changes between capsule and full-width states.

// Rounding is intentionally NOT done here. The original app relied solely on
// CSS border-radius. The clipping region is applied to the top-level HWND in
// main.cpp; this module never changes it during a mode animation.

// ---------------------------------------------------------------------------
// Appbar reservation is done ONCE at startup and never touched again. Calling
// ABM_SETPOS on every mode switch forces Windows to recompute the work area
// and re-layout every maximized window — that was the full-screen jerk.
// The reserved strip is a constant 48 px, independent of the visual state.
// ---------------------------------------------------------------------------
constexpr int kAppBarReserveDip = 48;

void RegisterAppBarOnce() {
    if (!g_hwnd || g_appBarRegistered) return;

    APPBARDATA abd{};
    abd.cbSize = sizeof(APPBARDATA);
    abd.hWnd = g_hwnd;
    abd.uCallbackMessage = WM_APPBAR_MSG;
    SHAppBarMessage(ABM_NEW, &abd);
    g_appBarRegistered = true;

    const RECT mon = GetCurrentMonitorInfo().rcMonitor;
    abd.uEdge = ABE_BOTTOM;
    abd.rc.left   = mon.left;
    abd.rc.top    = mon.bottom - ScaleDip(kAppBarReserveDip);
    abd.rc.right  = mon.right;
    abd.rc.bottom = mon.bottom;
    SHAppBarMessage(ABM_SETPOS, &abd);
    // ABM_SETPOS may have resized our HWND; the caller re-applies its geometry.
}

void UnregisterAppBar() {
    if (!g_hwnd || !g_appBarRegistered) return;
    APPBARDATA abd{};
    abd.cbSize = sizeof(APPBARDATA);
    abd.hWnd = g_hwnd;
    SHAppBarMessage(ABM_REMOVE, &abd);
    g_appBarRegistered = false;
}

// ---------------------------------------------------------------------------
// TextInputHost.exe owns a visible "Microsoft Text Input Application" window
// with no taskbar button. It must not appear in FlyStack or influence the
// capsule/expanded decision.
// ---------------------------------------------------------------------------
static bool IsSystemInputWindowImpl(HWND hwnd) {
    if (!hwnd || !IsWindow(hwnd)) return false;

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid) {
        HANDLE proc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (proc) {
            wchar_t path[MAX_PATH]{};
            DWORD len = MAX_PATH;
            if (QueryFullProcessImageNameW(proc, 0, path, &len) && len > 0) {
                const wchar_t* slash = wcsrchr(path, L'\\');
                const wchar_t* name = slash ? slash + 1 : path;
                if (!_wcsicmp(name, L"TextInputHost.exe") ||
                    !_wcsicmp(name, L"ctfmon.exe")) {
                    CloseHandle(proc);
                    return true;
                }
            }
            CloseHandle(proc);
        }
    }

    wchar_t cls[128]{};
    wchar_t title[512]{};
    GetClassNameW(hwnd, cls, static_cast<int>(std::size(cls)));
    GetWindowTextW(hwnd, title, static_cast<int>(std::size(title)));
    std::wstring low;
    low.reserve(wcslen(title));
    for (const wchar_t* p = title; *p; ++p) low.push_back(static_cast<wchar_t>(towlower(*p)));

    const bool inputTitle =
        low.find(L"microsoft text input") != std::wstring::npos ||
        low.find(L"text input application") != std::wstring::npos ||
        low.find(L"input application") != std::wstring::npos ||
        low.find(L"Microsoft Text Input") != std::wstring::npos ||
        low.find(L"текстовый ввод") != std::wstring::npos;
    const bool coreWindow = wcsstr(cls, L"CoreWindow") != nullptr ||
                            wcsstr(cls, L"ApplicationFrame") != nullptr;
    return inputTitle && coreWindow;
}

// ---------------------------------------------------------------------------
// Window enumeration.
// "Maximized" means: IsZoomed() OR the window covers the whole current monitor
// / work area. Small windowed apps must NOT pull the taskbar down.
// ---------------------------------------------------------------------------
struct EnumContext {
    bool anyWindow = false;
    bool maximized = false;
    RECT monitor{};
    int  monitorW = 0;
    int  workH    = 0;
};

bool IsEligibleWindow(HWND hwnd) {
    if (hwnd == g_hwnd) return false;                    // our own taskbar
    if (IsSystemInputWindow(hwnd)) return false;         // TextInputHost pseudo-window
    if (!IsWindowVisible(hwnd)) return false;
    if (IsIconic(hwnd)) return false;                    // minimized
    if (GetWindowLongW(hwnd, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) return false;

    RECT rc{};
    if (!GetWindowRect(hwnd, &rc)) return false;
    if (rc.right - rc.left <= 0 || rc.bottom - rc.top <= 0) return false;

    wchar_t cls[64]{};
    GetClassNameW(hwnd, cls, static_cast<int>(std::size(cls)));
    if (!_wcsicmp(cls, L"Progman") ||
        !_wcsicmp(cls, L"WorkerW") ||
        !_wcsicmp(cls, L"Shell_TrayWnd") ||
        !_wcsicmp(cls, L"Shell_SecondaryTrayWnd")) return false;

    // Hidden UWP / virtual desktops: DWMWA_CLOAKED (14).
    BOOL cloaked = FALSE;
    if (SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) &&
        cloaked) return false;

    return true;
}

BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam) {
    EnumContext* ctx = reinterpret_cast<EnumContext*>(lParam);
    if (ctx->maximized) return FALSE;                    // found what we need

    if (!IsEligibleWindow(hwnd)) return TRUE;
    ctx->anyWindow = true;

    RECT rc{};
    if (!GetWindowRect(hwnd, &rc)) return TRUE;

    RECT overlap{};
    if (!IntersectRect(&overlap, &rc, &ctx->monitor)) return TRUE; // other monitor

    const bool coversScreen = (rc.right - rc.left >= ctx->monitorW) &&
                              (rc.bottom - rc.top >= ctx->workH);
    if (IsZoomed(hwnd) || coversScreen) {
        ctx->maximized = true;
        return FALSE;
    }
    return TRUE;
}

EnumContext MakeEnumContext() {
    const MONITORINFO mi = GetCurrentMonitorInfo();
    EnumContext ctx{};
    ctx.monitor = mi.rcMonitor;
    ctx.monitorW = mi.rcMonitor.right - mi.rcMonitor.left;
    ctx.workH = mi.rcWork.bottom - mi.rcWork.top;
    return ctx;
}

// ---------------------------------------------------------------------------
// WinEvent hook callback. Runs on our own thread (WINEVENT_OUTOFCONTEXT), but
// we only post a message: keep hook code tiny and debounce the flood of
// EVENT_OBJECT_LOCATIONCHANGE notifications in the message loop.
// ---------------------------------------------------------------------------
// Windows toast notifications live in CoreWindow objects. Keep their bottom
// edge aligned with the capsule (desktop) or just above the full-width bar.
bool IsToastWindow(HWND hwnd) {
    wchar_t title[256]{};
    GetWindowTextW(hwnd, title, static_cast<int>(std::size(title)));
    return wcsstr(title, L"notification") || wcsstr(title, L"Notification") ||
           wcsstr(title, L"уведомление") || wcsstr(title, L"Уведомление");
}

void AdjustToast(HWND hwnd) {
    if (g_toastAdjusting || !IsWindow(hwnd) || !::IsWindowVisible(hwnd)) return;
    if (!IsToastWindow(hwnd)) return;
    RECT rc{};
    if (!GetWindowRect(hwnd, &rc)) return;
    const int w = rc.right - rc.left;
    const int h = rc.bottom - rc.top;
    if (w <= 0 || h <= 0) return;

    const RECT mon = GetCurrentMonitorInfo().rcMonitor;
    const int targetBottom = g_expanded
        ? mon.bottom - ScaleDip(BAR_HEIGHT) - 8
        : mon.bottom - ScaleDip(DESKTOP_BOTTOM_MARGIN);
    if (std::abs(rc.bottom - targetBottom) <= 2) return;

    g_toastAdjusting = true;
    SetWindowPos(hwnd, nullptr, rc.left, targetBottom - h, w, h,
                 SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOSIZE);
    g_toastAdjusting = false;
}

void CALLBACK WinEventProc(HWINEVENTHOOK, DWORD event, HWND hwnd,
                           LONG idObject, LONG, DWORD, DWORD) {
    if (!g_hwnd || !::IsWindow(g_hwnd)) return;
    if (idObject != OBJID_WINDOW) return;  // ignore OBJID_CURSOR etc.
    if (event == EVENT_OBJECT_SHOW || event == EVENT_OBJECT_LOCATIONCHANGE) {
        AdjustToast(hwnd);
    }
    if (hwnd == g_hwnd) return;            // ignore our own animation
    PostMessageW(g_hwnd, WM_TASKBAR_WINEVENT,
                 static_cast<WPARAM>(event), reinterpret_cast<LPARAM>(hwnd));
}

void InstallHooks() {
    constexpr DWORD kEvents[] = {
        EVENT_SYSTEM_FOREGROUND,
        EVENT_SYSTEM_MINIMIZESTART,
        EVENT_SYSTEM_MINIMIZEEND,
        EVENT_OBJECT_LOCATIONCHANGE,
        EVENT_OBJECT_DESTROY,
        EVENT_OBJECT_SHOW,
        EVENT_OBJECT_HIDE,
    };
    static_assert(std::size(kEvents) == std::size(g_hooks));

    for (size_t i = 0; i < std::size(kEvents); ++i) {
        g_hooks[i] = SetWinEventHook(kEvents[i], kEvents[i], nullptr,
                                     WinEventProc, 0, 0,
                                     WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
    }
}

void RemoveHooks() {
    for (HWINEVENTHOOK h : g_hooks) {
        if (h) UnhookWinEvent(h);
        h = nullptr;
    }
}

void BeginModeSwitch(bool expanded) {
    if (!g_hwnd) return;

    g_expanded = expanded;
    g_transitioning = true;

    if (expanded) {
        // The HWND is a transparent carrier; only CSS animates the material.
        NotifyWebMode(true);
    } else {
        // No HWND resize: the carrier is already tall enough. One CSS element
        // shrinks, rounds and lifts in a single synchronized transition.
        NotifyWebMode(false);
    }

    // One-shot settle timer, not a per-frame animation timer.
    KillTimer(g_hwnd, TIMER_SETTLE);
    SetTimer(g_hwnd, TIMER_SETTLE, CSS_TRANSITION_MS + 20, nullptr);
}

} // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
bool IsSystemInputWindow(HWND hwnd) {
    return IsSystemInputWindowImpl(hwnd);
}

bool IsDesktopActive() {
    EnumContext ctx = MakeEnumContext();
    EnumWindows(EnumWindowsProc, reinterpret_cast<LPARAM>(&ctx));
    return !ctx.anyWindow;
}

bool HasMaximizedWindow() {
    EnumContext ctx = MakeEnumContext();
    EnumWindows(EnumWindowsProc, reinterpret_cast<LPARAM>(&ctx));
    return ctx.maximized;
}

bool IsExpanded() {
    return g_expanded;
}

void Init(HWND hwnd) {
    g_hwnd = hwnd;
    g_expanded = HasMaximizedWindow();

    // Defensive: guaranteed borderless top-level popup (no white DWM frame).
    LONG style = GetWindowLongW(g_hwnd, GWL_STYLE);
    style &= ~(WS_BORDER | WS_THICKFRAME | WS_DLGFRAME);
    style |= WS_POPUP;
    SetWindowLongW(g_hwnd, GWL_STYLE, style);

    DisableSystemShadow();

    const DWORD noRound = 1;         // DWMWCP_DONOTROUND: strict rectangle
    DwmSetWindowAttribute(g_hwnd, 33, &noRound, sizeof(noRound));

    // Disable the system accent ONCE and never touch it again. All glass is CSS.
    SetAccent(ACCENT_STATE::ACCENT_DISABLED, 0, 0);

    RegisterAppBarOnce();               // work area fixed for the whole session
    MoveTo(ComputeCarrier(g_expanded)); // ABM_SETPOS may have resized us

    InstallHooks();
}

void Shutdown() {
    RemoveHooks();
    if (g_hwnd) {
        KillTimer(g_hwnd, TIMER_SETTLE);
        KillTimer(g_hwnd, TIMER_DEBOUNCE);
        UnregisterAppBar();          // only on exit
    }
    g_hwnd = nullptr;
    g_transitioning = false;
}

void Refresh() {
    if (!g_hwnd) return;
    // Windowed apps (even several of them) keep the capsule on the desktop.
    // Only a real full-screen/maximized window on our monitor expands the bar.
    const bool desired = HasMaximizedWindow();
    // g_expanded is the TARGET state from the moment a transition starts, so
    // repeated LOCATIONCHANGE events in the same state are always ignored.
    if (desired == g_expanded || g_transitioning) return;
    BeginModeSwitch(desired);
}

void FinishTransition() {
    if (!g_hwnd || !g_transitioning) return;
    KillTimer(g_hwnd, TIMER_SETTLE);
    g_transitioning = false;

    Refresh();
}

void OnSettleTimer() { FinishTransition(); }

void OnCssTransitionDone() { FinishTransition(); }

void OnSize() {
    // Nothing to do: the WebView bounds are updated in main.cpp and the CSS
    // material is state-independent.
}

bool NotifyFlyout(bool open) {
    if (g_flyoutOpen == open) return false; // avoid a C++ <-> JS ready loop
    g_flyoutOpen = open;
    // No SetWindowPos here: the carrier is already tall enough for the panel.
    // Resizing it was what made the taskbar blink and duplicate at the top.
    return true;
}

void ApplyGeometryNow() {
    if (!g_hwnd) return;
    MoveTo(ComputeCarrier(g_expanded));
}

} // namespace TaskbarModes
