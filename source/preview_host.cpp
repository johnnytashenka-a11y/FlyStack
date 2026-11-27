#define NOMINMAX
#include "preview_host.h"

#include <dwmapi.h>

#include <algorithm>
#include <string>

#pragma comment(lib, "dwmapi.lib")

namespace {

constexpr wchar_t kPreviewClass[] = L"FlyStackPreviewClass";
constexpr int kPreviewWDip = 320;
constexpr int kPreviewHDip = 190;
constexpr int kCaptionDip = 28;
constexpr int kGapDip = 8;

HWND       g_owner   = nullptr;
HWND       g_preview = nullptr;
HTHUMBNAIL g_thumb   = nullptr;
HWND       g_source  = nullptr;
std::wstring g_title;
int        g_dpi     = 96;

int Px(int dip) {
    return MulDiv(dip, g_dpi > 0 ? g_dpi : 96, 96);
}

int Clamp(int v, int lo, int hi) {
    return std::max(lo, std::min(v, hi));
}

LRESULT CALLBACK PreviewProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT: {
        PAINTSTRUCT ps{};
        HDC dc = BeginPaint(hwnd, &ps);
        if (!dc) return 0;

        RECT rc{};
        GetClientRect(hwnd, &rc);

        HBRUSH body = CreateSolidBrush(RGB(23, 23, 28));
        FillRect(dc, &rc, body);
        DeleteObject(body);

        HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
        HPEN border = CreatePen(PS_SOLID, 1, RGB(60, 60, 70));
        HGDIOBJ oldPen = SelectObject(dc, border);
        Rectangle(dc, rc.left, rc.top, rc.right - 1, rc.bottom - 1);
        SelectObject(dc, oldPen);
        SelectObject(dc, oldBrush);
        DeleteObject(border);

        RECT caption = rc;
        caption.bottom = caption.top + Px(kCaptionDip);
        HBRUSH capBrush = CreateSolidBrush(RGB(31, 31, 38));
        FillRect(dc, &caption, capBrush);
        DeleteObject(capBrush);

        if (!g_title.empty()) {
            HFONT font = CreateFontW(Px(12), 0, 0, 0, FW_SEMIBOLD,
                                     FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                     OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                                     CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                                     L"Segoe UI");
            HGDIOBJ oldFont = SelectObject(dc, font);
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, RGB(240, 240, 245));
            RECT text = caption;
            text.left += 10;
            text.right -= 10;
            DrawTextW(dc, g_title.c_str(), -1, &text,
                      DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
            SelectObject(dc, oldFont);
            DeleteObject(font);
        }

        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_NCHITTEST:
        // A preview is informational only; never swallow clicks meant for the
        // taskbar or the application underneath it.
        return HTTRANSPARENT;

    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void Unregister() {
    if (g_thumb) {
        DwmUnregisterThumbnail(g_thumb);
        g_thumb = nullptr;
    }
    g_source = nullptr;
}

} // namespace

namespace PreviewHost {

void Init(HWND owner) {
    if (g_preview) return;
    g_owner = owner;
    UINT dpi = owner ? GetDpiForWindow(owner) : 0;
    g_dpi = dpi ? static_cast<int>(dpi) : 96;

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = PreviewProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512)); // IDC_ARROW
    wc.hbrBackground = nullptr;
    wc.lpszClassName = kPreviewClass;
    RegisterClassExW(&wc);

    g_preview = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TOPMOST | WS_EX_TRANSPARENT,
        kPreviewClass, L"", WS_POPUP | WS_DISABLED,
        0, 0, 10, 10, nullptr, nullptr, wc.hInstance, nullptr);
}

void Hide() {
    Unregister();
    g_title.clear();
    if (g_preview && IsWindow(g_preview)) ShowWindow(g_preview, SW_HIDE);
}

void Show(HWND source, const wchar_t* title,
          int anchorX, int anchorY, int /*anchorW*/, int /*anchorH*/) {
    Hide();
    if (!g_owner || !g_preview || !source || !IsWindow(source) ||
        !IsWindowVisible(source) || IsIconic(source)) {
        return;
    }

    // CSS pixels -> physical pixels -> screen coordinates.
    POINT anchor{
        MulDiv(anchorX, g_dpi, 96),
        MulDiv(anchorY, g_dpi, 96)
    };
    ClientToScreen(g_owner, &anchor);

    const int w = Px(kPreviewWDip);
    const int h = Px(kPreviewHDip);
    int left = anchor.x - w / 2;
    int top = anchor.y - h - Px(kGapDip);

    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    GetMonitorInfoW(MonitorFromPoint(anchor, MONITOR_DEFAULTTONEAREST), &mi);
    const int pad = Px(8);
    left = Clamp(left, mi.rcWork.left + pad, mi.rcWork.right - w - pad);
    top = Clamp(top, mi.rcWork.top + pad, mi.rcWork.bottom - h - pad);

    g_source = source;
    g_title = title ? title : L"";
    SetWindowPos(g_preview, HWND_TOPMOST, left, top, w, h,
                 SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_SHOWWINDOW);
    InvalidateRect(g_preview, nullptr, TRUE);

    if (FAILED(DwmRegisterThumbnail(g_preview, source, &g_thumb))) {
        ShowWindow(g_preview, SW_HIDE);
        g_source = nullptr;
        g_title.clear();
        return;
    }

    RECT dest{ 1, Px(kCaptionDip) + 1, w - 1, h - 1 };
    SIZE src{};
    if (SUCCEEDED(DwmQueryThumbnailSourceSize(g_thumb, &src)) &&
        src.cx > 0 && src.cy > 0) {
        const double scale = std::min(
            static_cast<double>(dest.right - dest.left) / src.cx,
            static_cast<double>(dest.bottom - dest.top) / src.cy);
        const int tw = std::max(1, static_cast<int>(src.cx * scale));
        const int th = std::max(1, static_cast<int>(src.cy * scale));
        const int cx = (dest.left + dest.right) / 2;
        const int cy = (dest.top + dest.bottom) / 2;
        dest = RECT{ cx - tw / 2, cy - th / 2, cx + tw / 2, cy + th / 2 };
    }

    DWM_THUMBNAIL_PROPERTIES props{};
    props.dwFlags = DWM_TNP_VISIBLE | DWM_TNP_OPACITY | DWM_TNP_RECTDESTINATION;
    props.fVisible = TRUE;
    props.opacity = 255;
    props.rcDestination = dest;
    DwmUpdateThumbnailProperties(g_thumb, &props);
}

void Tick() {
    if (!g_source) return;
    if (!IsWindow(g_source) || !IsWindowVisible(g_source) || IsIconic(g_source)) {
        Hide();
    }
}

void Shutdown() {
    Hide();
    if (g_preview && IsWindow(g_preview)) DestroyWindow(g_preview);
    g_preview = nullptr;
    g_owner = nullptr;
}

} // namespace PreviewHost
