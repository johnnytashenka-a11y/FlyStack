#define NOMINMAX
#include "tray_host.h"

#include <algorithm>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

// ---------------------------------------------------------------------------
// Presentation-only host for the real notification area.
//
// The real window tree (Shell_TrayWnd -> TrayNotifyWnd -> SysPager ->
// ToolbarWindow32) is left completely alone: nothing is scanned, no icon is
// re-rendered, no click is synthesized. We only change WHERE TrayNotifyWnd
// lives on screen, then paint a rounded "island" behind it so it matches the
// rest of the FlyStack bar.
// ---------------------------------------------------------------------------
namespace {

constexpr int kMinHeightDip = 34;
constexpr int kRadiusDip    = 12;
constexpr int kFallbackWDip = 88;

HWND g_carrier    = nullptr;
HWND g_webView    = nullptr;
HWND g_host       = nullptr;
HWND g_notify     = nullptr;
HWND g_shellTray  = nullptr;
HWND g_clock      = nullptr;
HWND g_origParent = nullptr;

LONG_PTR g_origStyle   = 0;
LONG_PTR g_origExStyle = 0;
RECT     g_origRect{};
bool     g_clockWasVisible = false;

bool g_native      = false;
bool g_modeAck     = false;   // page acknowledged the last trayMode message
bool g_slotVisible = true;
RECT g_slotDip{ 0, 0, 0, 0 };

int g_hostWpx       = 0;
int g_hostHpx       = 0;
int g_lastSentWDip  = -1;

// Native icon children are hidden only while FlyStack owns TrayNotifyWnd;
// their original visibility is restored on shutdown.
std::vector<std::pair<HWND, bool>> g_hiddenIcons;

constexpr wchar_t kHostClass[] = L"FlyStackTrayHostClass";

int Dpi() {
    UINT d = g_carrier ? GetDpiForWindow(g_carrier) : 96;
    return d ? static_cast<int>(d) : 96;
}
int Px(int dip)  { return MulDiv(dip, Dpi(), 96); }
int Dip(int px)  { return MulDiv(px, 96, Dpi()); }

bool HasVisibleStyle(HWND w) {
    return (GetWindowLongPtrW(w, GWL_STYLE) & WS_VISIBLE) != 0;
}

// ---------------------------------------------------------------------------
// The island is painted by the WebView CSS placeholder (#traySlot), not here.
// GDI pixels would pass through DwmExtendFrameIntoClientArea(-1) and come out
// tinted violet/blue — exactly the artefact we are removing. The host still
// validates its paint region so it does not spin on WM_PAINT.
// ---------------------------------------------------------------------------
void PaintIsland(HWND hwnd) {
    PAINTSTRUCT ps{};
    HDC dc = BeginPaint(hwnd, &ps);
    if (dc) EndPaint(hwnd, &ps);
}

LRESULT CALLBACK HostProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_PAINT:
        PaintIsland(hwnd);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_NCHITTEST:
        // Let clicks fall through to the WebView icons painted in the CSS
        // island underneath this transparent HWND host.
        return HTTRANSPARENT;
    case WM_COMMAND:
        // The reparented tray still talks to its original command target
        // (the shell taskbar). Re-sending the command keeps the chevron and
        // the tray's own menu plumbing alive without touching tray logic.
        if (g_origParent && IsWindow(g_origParent) && g_origParent != hwnd) {
            return SendMessageW(g_origParent, msg, wp, lp);
        }
        break;
    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// The reparented shell window brings its own Windows-styled icons with it.
// Hide every visible child so the island keeps FlyStack's capsule styling;
// the shell window itself stays reparented and untouched otherwise.
void HideNativeIcons() {
    if (!g_notify || !IsWindow(g_notify)) return;
    for (HWND c = GetWindow(g_notify, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT)) {
        if (c == g_clock) continue;          // already hidden by Embed
        if (!HasVisibleStyle(c)) continue;
        bool tracked = false;
        for (const auto& e : g_hiddenIcons) {
            if (e.first == c) { tracked = true; break; }
        }
        if (!tracked) g_hiddenIcons.emplace_back(c, true);
        ShowWindow(c, SW_HIDE);
    }
}

void RestoreNativeIcons() {
    for (const auto& e : g_hiddenIcons) {
        if (IsWindow(e.first)) ShowWindow(e.first, e.second ? SW_SHOW : SW_HIDE);
    }
    g_hiddenIcons.clear();
}

void ReportWidth() {
    if (!g_native || g_hostWpx <= 0) return;
    int w = Dip(g_hostWpx);
    if (w == g_lastSentWDip) return;
    g_lastSentWDip = w;
    PostJsonToWebView(L"{\"type\":\"traySlotWidth\",\"w\":" + std::to_wstring(w) + L"}");
}

// ---------------------------------------------------------------------------
// Geometry: the island is anchored to the RIGHT edge of the CSS placeholder,
// so a wrong width estimate can never push it over the clock.
// ---------------------------------------------------------------------------
void ApplySlot() {
    if (!g_host) return;

    if (!g_native || !g_slotVisible ||
        g_slotDip.right <= g_slotDip.left || g_slotDip.bottom <= g_slotDip.top ||
        !g_notify || !IsWindow(g_notify) || !HasVisibleStyle(g_notify)) {
        ShowWindow(g_host, SW_HIDE);
        return;
    }

    const int dpi = Dpi();
    const int slotRight = MulDiv(g_slotDip.right, dpi, 96);
    const int slotTop   = MulDiv(g_slotDip.top,    dpi, 96);
    const int slotH     = MulDiv(g_slotDip.bottom - g_slotDip.top, dpi, 96);

    int w = g_hostWpx > 0 ? g_hostWpx : Px(kFallbackWDip);
    int h = g_hostHpx > 0 ? g_hostHpx : Px(kMinHeightDip);
    int x = slotRight - w;
    int y = slotTop + (slotH - h) / 2;

    RECT client{};
    GetClientRect(g_carrier, &client);
    x = std::max(0, std::min(x, static_cast<int>(client.right) - w));
    y = std::max(0, std::min(y, static_cast<int>(client.bottom) - h));

    SetWindowPos(g_host, HWND_TOP, x, y, w, h, SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

// ---------------------------------------------------------------------------
// Embedding / restoring the real notification area.
// ---------------------------------------------------------------------------
bool Embed() {
    if (g_native) return true;
    if (!g_carrier || !g_host) return false;

    HWND shell = FindWindowW(L"Shell_TrayWnd", nullptr);
    if (!shell) return false;

    HWND notify = FindWindowExW(shell, nullptr, L"TrayNotifyWnd", nullptr);
    if (!notify) return false;

    // Without the pager there are no icons to show; let the CSS tray handle it.
    if (!FindWindowExW(notify, nullptr, L"SysPager", nullptr)) return false;

    RECT nr{};
    if (!GetWindowRect(notify, &nr)) return false;
    const int nh = nr.bottom - nr.top;
    if (nr.right <= nr.left || nh <= 0) return false;

    g_notify     = notify;
    g_shellTray  = shell;
    g_origParent = GetParent(notify);
    g_origStyle  = GetWindowLongPtrW(notify, GWL_STYLE);
    g_origExStyle = GetWindowLongPtrW(notify, GWL_EXSTYLE);
    g_origRect   = nr;
    g_clock      = FindWindowExW(notify, nullptr, L"TrayClockWClass", nullptr);
    g_clockWasVisible = g_clock && HasVisibleStyle(g_clock);

    // Native icons are hidden, so size the island to the compact capsule
    // instead of measuring shell tray content.
    g_hostWpx = Px(kFallbackWDip);
    g_hostHpx = Px(kMinHeightDip);
    SetWindowPos(g_host, nullptr, 0, 0, g_hostWpx, g_hostHpx,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);

    if (!SetParent(notify, g_host)) {
        g_notify = nullptr;
        g_clock = nullptr;
        g_shellTray = nullptr;
        g_origParent = nullptr;
        return false;
    }
    SetWindowLongPtrW(notify, GWL_STYLE, (g_origStyle | WS_CHILD) & ~WS_POPUP);
    SetWindowLongPtrW(notify, GWL_EXSTYLE, g_origExStyle & ~WS_EX_APPWINDOW);

    if (g_clock) ShowWindow(g_clock, SW_HIDE);   // FlyStack draws its own clock
    ShowWindow(notify, SW_SHOW);
    HideNativeIcons();                           // only FlyStack styling is visible

    g_native = true;
    g_lastSentWDip = -1;
    ApplySlot();
    return true;
}

void ReleaseState(bool restore) {
    if (restore && g_notify && IsWindow(g_notify)) {
        ShowWindow(g_notify, SW_HIDE);
        if (g_origParent && IsWindow(g_origParent)) {
            SetParent(g_notify, g_origParent);
        }
        SetWindowLongPtrW(g_notify, GWL_STYLE, g_origStyle);
        SetWindowLongPtrW(g_notify, GWL_EXSTYLE, g_origExStyle);

        POINT p{ g_origRect.left, g_origRect.top };
        if (g_origParent && IsWindow(g_origParent)) ScreenToClient(g_origParent, &p);
        SetWindowPos(g_notify, nullptr, p.x, p.y,
                     g_origRect.right - g_origRect.left,
                     g_origRect.bottom - g_origRect.top,
                     SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);

        if (g_clock && IsWindow(g_clock)) {
            ShowWindow(g_clock, g_clockWasVisible ? SW_SHOW : SW_HIDE);
        }
        ShowWindow(g_notify, SW_SHOW);
        RestoreNativeIcons();
    } else {
        g_hiddenIcons.clear();
    }

    g_notify     = nullptr;
    g_clock      = nullptr;
    g_shellTray  = nullptr;
    g_origParent = nullptr;
    g_native     = false;
    g_lastSentWDip = -1;
}

void EnsureAboveWebView() {
    if (!g_host || !IsWindow(g_host)) return;
    if (g_webView && IsWindow(g_webView)) {
        SetWindowPos(g_host, g_webView, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    } else {
        SetWindowPos(g_host, HWND_TOP, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
}

} // namespace

namespace TrayHost {

void Init(HWND carrier, HWND webViewChild) {
    if (g_carrier) return;
    g_carrier = carrier;
    g_webView = webViewChild;

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = HostProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512)); // IDC_ARROW
    wc.hbrBackground = nullptr;
    wc.lpszClassName = kHostClass;
    RegisterClassExW(&wc);

    // WS_EX_TRANSPARENT is essential: the host must not paint over the WebView
    // CSS island underneath it. The real TrayNotifyWnd remains a normal child
    // and keeps receiving mouse input and tray messages.
    g_host = CreateWindowExW(WS_EX_TRANSPARENT, kHostClass, L"",
                             WS_CHILD | WS_CLIPSIBLINGS,
                             0, 0, 10, 10, carrier, nullptr,
                             wc.hInstance, nullptr);
    if (!g_host) return;

    // The host is only a container. An empty region makes it fully transparent
    // to mouse input, so clicks land on the WebView icons drawn in the CSS
    // island underneath instead of being swallowed by this HWND.
    HRGN empty = CreateRectRgn(0, 0, 0, 0);
    if (empty && SetWindowRgn(g_host, empty, TRUE) == 0) DeleteObject(empty);

    if (Embed()) {
        EnsureAboveWebView();
        PushState();
    } else {
        PushState();
    }
}

void SetWebViewChild(HWND child) {
    g_webView = child;
    EnsureAboveWebView();
}

void OnSlotRect(int x, int y, int w, int h, bool visible) {
    g_slotDip.left   = x;
    g_slotDip.top    = y;
    g_slotDip.right  = x + (w > 0 ? w : 0);
    g_slotDip.bottom = y + (h > 0 ? h : 0);
    g_slotVisible = visible;
    ApplySlot();
}

void OnCarrierResized() {
    EnsureAboveWebView();
    ApplySlot();
}

void Tick() {
    if (!g_carrier) return;

    // The shell taskbar is replaced by our bar; re-hide it if Explorer
    // restarted and brought it back.
    HWND shell = FindWindowW(L"Shell_TrayWnd", nullptr);
    if (shell && IsWindowVisible(shell)) ShowWindow(shell, SW_HIDE);
    if (!shell) return;
    if (!g_shellTray && !g_native) g_shellTray = shell;

    if (!g_native) {
        // Retry: Explorer may have started later, or the lazy tray appeared.
        if (Embed()) {
            EnsureAboveWebView();
            PushState();
            ApplySlot();
        }
        return;
    }

    if (!g_notify || !IsWindow(g_notify)) {
        ReleaseState(false);
        PushState();               // falls back to the CSS tray if re-embed fails
        return;
    }

    if (GetParent(g_notify) != g_host) {
        if (!SetParent(g_notify, g_host)) {
            ReleaseState(false);
            PushState();
            return;
        }
        ShowWindow(g_notify, SW_SHOW);
    }

    HideNativeIcons();   // Explorer can re-create icon children; keep them hidden

    EnsureAboveWebView();
    ApplySlot();

    // The first trayMode can be posted before the page installs its listener.
    // Re-assert it until the page acknowledges; stop once it has, so this is
    // not a permanent timer-driven repost.
    if (!g_modeAck) {
        static DWORD s_lastPush = 0;
        const DWORD now = GetTickCount();
        if (now - s_lastPush >= 500) {
            s_lastPush = now;
            PushState();
        }
    }
}

void OnModeAck(bool native) {
    if (native == g_native) g_modeAck = true;
}

bool IsNativeActive() {
    return g_native;
}

void PushState() {
    if (!g_host) return;
    g_modeAck = false;
    PostJsonToWebView(g_native
        ? L"{\"type\":\"trayMode\",\"native\":1}"
        : L"{\"type\":\"trayMode\",\"native\":0}");
    if (g_native) {
        g_lastSentWDip = -1;
        ReportWidth();
        ApplySlot();
    }
}

void Shutdown() {
    ReleaseState(true);
    if (g_host && IsWindow(g_host)) {
        DestroyWindow(g_host);
    }
    g_host = nullptr;
    g_carrier = nullptr;
    g_webView = nullptr;
}

} // namespace TrayHost
