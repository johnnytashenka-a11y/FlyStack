#include <windows.h>
#include <ShellScalingApi.h>
#include <wrl.h>
#include <gdiplus.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <commctrl.h>
#include <tlhelp32.h>
#include <string>
#include <vector>
#include <sstream>
#include <algorithm>
#include <cmath>

#include "vendor/include/WebView2.h"
#include "html_data.h"
#include "taskbar_modes.h"
#include "tray_host.h"
#include "preview_host.h"

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "comctl32.lib")

using namespace Microsoft::WRL;
using namespace Gdiplus;

HWND g_hWnd = NULL;
ComPtr<ICoreWebView2Controller> g_webViewController;
ComPtr<ICoreWebView2> g_webView;

ULONG_PTR g_gdiplusToken = 0;

// Shared with the dynamic-mode module (see taskbar_modes.h).
static const int BAR_HEIGHT = TaskbarModes::BAR_HEIGHT;
static const int FLYOUT_OPEN_H = TaskbarModes::FLYOUT_OPEN_H;
static const UINT WM_APPBAR = TaskbarModes::WM_APPBAR_MSG;

// The page (WebView2) reports these so the window is clickable only over the
// capsule and the open flyout panel; everything else is click-through to the
// desktop (the window is a glass frame).
static RECT g_CapsuleRectCss = { 0, 0, 0, 0 };
static RECT g_FlyoutRectCss = { 0, 0, 0, 0 };
static BOOL g_FlyoutOpen = FALSE;
static UINT g_Dpi = 96;

// JS rects are relative to the WebView viewport, which always fills the carrier.
static BOOL PointInRect(LONG x, LONG y, const RECT& r) {
    if (r.right <= r.left || r.bottom <= r.top) return FALSE;
    float scale = (g_Dpi > 0 ? g_Dpi : 96) / 96.0f;
    int left   = (int)std::lround(r.left * scale);
    int top    = (int)std::lround(r.top * scale);
    int right  = (int)std::lround(r.right * scale);
    int bottom = (int)std::lround(r.bottom * scale);
    return x >= left && x < right && y >= top && y < bottom;
}

// WebView2 creates a whole chain of child HWNDs that covers our transparent
// glass frame: Chrome_WidgetWin_0 -> Chrome_WidgetWin_1 ->
// Chrome_RenderWidgetHostHWND. Subclass the outer wrapper for hit-testing; the
// top-level region below is what actually prevents the renderer from swallowing
// clicks over the rest of the desktop.
static HWND       g_webViewChild = nullptr;
static UINT_PTR   kWebViewSubclassId = 1;

// Interactive means the dock capsule or an actually open flyout. Until the page
// reports the capsule rect we keep the frame interactive so the dock is not
// dead during startup.
static BOOL IsPointInteractive(LONG x, LONG y) {
    BOOL known = (g_CapsuleRectCss.right > g_CapsuleRectCss.left &&
                  g_CapsuleRectCss.bottom > g_CapsuleRectCss.top);
    if (!known) return TRUE;
    if (PointInRect(x, y, g_CapsuleRectCss)) return TRUE;
    if (g_FlyoutOpen && PointInRect(x, y, g_FlyoutRectCss)) return TRUE;
    return FALSE;
}

// Clip the whole top-level window (not just the browser child) to the taskbar
// strip, plus the open panel when there is one. The WebView keeps its full,
// fixed 640 DIP viewport, so the page never re-lays out when the calendar opens
// and there is no bright strip at a resized browser boundary. Everything
// outside this region is invisible to both painting and hit-testing: the
// renderer cannot swallow clicks over the rest of the screen.
static void ApplyCarrierRegion() {
    if (!g_hWnd || !IsWindow(g_hWnd)) return;
    RECT client{};
    if (!GetClientRect(g_hWnd, &client)) return;

    const UINT dpi = g_Dpi > 0 ? g_Dpi : 96;
    int strip = MulDiv(BAR_HEIGHT + 42, dpi, 96);
    if (strip > client.bottom) strip = client.bottom;
    HRGN region = CreateRectRgn(0, client.bottom - strip,
                                client.right, client.bottom);
    if (!region) return;

    if (g_FlyoutOpen && g_FlyoutRectCss.right > g_FlyoutRectCss.left &&
        g_FlyoutRectCss.bottom > g_FlyoutRectCss.top) {
        float scale = dpi / 96.0f;
        int left   = (int)std::lround(g_FlyoutRectCss.left * scale);
        int top    = (int)std::lround(g_FlyoutRectCss.top * scale);
        int right  = (int)std::lround(g_FlyoutRectCss.right * scale);
        int bottom = (int)std::lround(g_FlyoutRectCss.bottom * scale);
        left   = (left < 0) ? 0 : (left > (int)client.right ? (int)client.right : left);
        right  = (right < 0) ? 0 : (right > (int)client.right ? (int)client.right : right);
        top    = (top < 0) ? 0 : (top > (int)client.bottom ? (int)client.bottom : top);
        bottom = (bottom < 0) ? 0 : (bottom > (int)client.bottom ? (int)client.bottom : bottom);
        if (right > left && bottom > top) {
            HRGN panel = CreateRectRgn(left, top, right, bottom);
            if (panel) {
                CombineRgn(region, region, panel, RGN_OR);
                DeleteObject(panel);
            }
        }
    }
    SetWindowRgn(g_hWnd, region, TRUE); // window owns the region
}

static LRESULT CALLBACK WebViewHitTestProc(HWND hWnd, UINT msg, WPARAM wParam,
                                           LPARAM lParam, UINT_PTR, DWORD_PTR) {
    if (msg == WM_NCHITTEST && g_hWnd) {
        POINT pt{ static_cast<LONG>(LOWORD(lParam)), static_cast<LONG>(HIWORD(lParam)) };
        POINT local = pt;
        ScreenToClient(g_hWnd, &local);
        if (!IsPointInteractive(local.x, local.y)) return HTTRANSPARENT;
    }
    return DefSubclassProc(hWnd, msg, wParam, lParam);
}

static void SubclassChromeChain(HWND hWnd) {
    if (!hWnd || !IsWindow(hWnd)) return;

    DWORD pid = 0;
    GetWindowThreadProcessId(hWnd, &pid);
    if (pid != GetCurrentProcessId()) return; // renderer is clipped by the region

    wchar_t cls[128]{};
    GetClassNameW(hWnd, cls, 128);
    if (wcsstr(cls, L"Chrome_WidgetWin")) {
        DWORD_PTR ref = 0;
        if (!GetWindowSubclass(hWnd, WebViewHitTestProc, kWebViewSubclassId, &ref)) {
            SetWindowSubclass(hWnd, WebViewHitTestProc, kWebViewSubclassId, 0);
        }
    }

    EnumChildWindows(hWnd, [](HWND child, LPARAM) -> BOOL {
        SubclassChromeChain(child);
        return TRUE;
    }, 0);
}

static void InstallWebViewHitTestSubclass() {
    if (!g_webViewChild || !IsWindow(g_webViewChild)) {
        EnumChildWindows(g_hWnd, [](HWND h, LPARAM) -> BOOL {
            wchar_t cls[128]{};
            GetClassNameW(h, cls, 128);
            if (wcsstr(cls, L"Chrome_WidgetWin")) {
                g_webViewChild = h;
                return FALSE;
            }
            return TRUE;
        }, 0);
        if (!g_webViewChild) return;
        TrayHost::SetWebViewChild(g_webViewChild);
    }
    SubclassChromeChain(g_webViewChild);
    // WebView children are created after the first WM_SIZE; re-assert the
    // top-level region now so the renderer inherits the clip.
    ApplyCarrierRegion();
}

void SendTrayIconsToJS();

// Single entry point used by the visual tray host to talk to the page.
void PostJsonToWebView(const std::wstring& json) {
    if (!g_webView || json.empty()) return;
    g_webView->PostWebMessageAsString(json.c_str());
}

// Tell the page that the HWND has been enlarged, so it may reveal the calendar.
void NotifyFlyoutReady() {
    if (!g_webView) return;
    g_webView->ExecuteScript(L"window.__flyReady && window.__flyReady();",
        Callback<ICoreWebView2ExecuteScriptCompletedHandler>(
            [](HRESULT, LPCWSTR) -> HRESULT { return S_OK; }).Get());
}

void UpdateWebViewBounds();

// The carrier and the WebView keep one fixed size for the whole session. No
// SetWindowPos and no put_Bounds on flyout changes: each of those re-laid out
// the page and made the calendar/taskbar visibly jump. Only the top-level
// region grows to include the panel.
void SetFlyout(HWND hwnd, BOOL open) {
    g_FlyoutOpen = open;
    (void)hwnd;
    if (open) PreviewHost::Hide();
    ApplyCarrierRegion();
    if (TaskbarModes::NotifyFlyout(open != FALSE) && open) {
        NotifyFlyoutReady();       // panel may now animate inside the fixed HWND
    }
}

// WebView2 bounds are relative to the parent's client area. They always fill it.
void UpdateWebViewBounds() {
    if (!g_webViewController || !g_hWnd) return;
    RECT client{};
    GetClientRect(g_hWnd, &client);
    g_webViewController->put_Bounds(RECT{ 0, 0, client.right, client.bottom });
    ApplyCarrierRegion();
}

// The mode switch is animated entirely in CSS inside WebView2. The module only
// tells the page which state to animate to; no SetWindowPos in a 60 Hz loop.
void NotifyWebMode(bool expanded) {
    if (!g_webView) return;
    // Primary path: run the page's setter directly, with a null check so it is
    // harmless if the script has not been installed yet. (This SDK exposes
    // ExecuteScript rather than the newer ExecuteScriptAsync.)
    const wchar_t* script = expanded
        ? L"window.__flySetMode && window.__flySetMode(true);"
        : L"window.__flySetMode && window.__flySetMode(false);";
    g_webView->ExecuteScript(script,
        Callback<ICoreWebView2ExecuteScriptCompletedHandler>(
            [](HRESULT, LPCWSTR) -> HRESULT { return S_OK; }).Get());
    // Fallback for the web-message listener in html_data.h.
    g_webView->PostWebMessageAsString(
        expanded ? L"{\"type\":\"mode\",\"expanded\":1}"
                 : L"{\"type\":\"mode\",\"expanded\":0}");
}

// ---------------- helpers ----------------
std::string WideToUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(len, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), s.data(), len, nullptr, nullptr);
    return s;
}
std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(len, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), w.data(), len);
    return w;
}
std::string Base64Encode(const std::vector<unsigned char>& data) {
    static const char* tbl = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    int val = 0, valb = -6;
    for (unsigned char c : data) {
        val = (val << 8) + c;
        valb += 8;
        while (valb >= 0) { out.push_back(tbl[(val >> valb) & 0x3F]); valb -= 6; }
    }
    if (valb > -6) out.push_back(tbl[((val << 8) >> (valb + 8)) & 0x3F]);
    while (out.size() % 4) out.push_back('=');
    return out;
}
std::string JsonEscape(const std::string& s) {
    std::string r;
    for (char c : s) {
        switch (c) {
            case '"': r += "\\\""; break;
            case '\\': r += "\\\\"; break;
            case '\n': r += "\\n"; break;
            case '\r': r += "\\r"; break;
            case '\t': r += "\\t"; break;
            default:
                if ((unsigned char)c < 0x20) { char buf[8]; sprintf_s(buf, "\\u%04x", (unsigned char)c); r += buf; }
                else r += c;
        }
    }
    return r;
}
std::string HIconToPngBase64(HICON hIcon) {
    if (!hIcon) return {};
    Bitmap* bmp = Bitmap::FromHICON(hIcon);
    if (!bmp || bmp->GetLastStatus() != Ok) { delete bmp; return {}; }
    IStream* stream = nullptr;
    if (CreateStreamOnHGlobal(nullptr, TRUE, &stream) != S_OK || !stream) { delete bmp; return {}; }
    CLSID pngClsid; bool found = false;
    UINT num = 0, size = 0;
    GetImageEncodersSize(&num, &size);
    if (size > 0) {
        std::vector<ImageCodecInfo> codecs(size / sizeof(ImageCodecInfo));
        GetImageEncoders(num, size, codecs.data());
        for (UINT i = 0; i < num; i++) {
            if (wcscmp(codecs[i].MimeType, L"image/png") == 0) { pngClsid = codecs[i].Clsid; found = true; break; }
        }
    }
    if (!found) { stream->Release(); delete bmp; return {}; }
    if (bmp->Save(stream, &pngClsid, nullptr) != Ok) { stream->Release(); delete bmp; return {}; }
    STATSTG st; stream->Stat(&st, STATFLAG_NONAME);
    ULONG sz = (ULONG)st.cbSize.QuadPart;
    std::vector<unsigned char> buf(sz);
    LARGE_INTEGER li; li.QuadPart = 0; stream->Seek(li, STREAM_SEEK_SET, nullptr);
    ULONG read = 0; stream->Read(buf.data(), sz, &read);
    stream->Release();
    delete bmp;
    if (read == 0) return {};
    buf.resize(read);
    return "data:image/png;base64," + Base64Encode(buf);
}

// Appbar registration/reservation is handled by TaskbarModes::UpdateAppBar().

// ---------------- window enumeration ----------------
struct WinInfo { HWND hwnd; std::wstring title; std::string icon; bool active; };
struct AppsEnumContext { std::vector<WinInfo>* list; HWND foreground; };

std::vector<WinInfo> CollectTaskWindows() {
    std::vector<WinInfo> list;
    AppsEnumContext ctx{ &list, GetForegroundWindow() };
    EnumWindows([](HWND hwnd, LPARAM lp) -> BOOL {
        auto* data = reinterpret_cast<AppsEnumContext*>(lp);
        if (!IsWindowVisible(hwnd)) return TRUE;
        if (TaskbarModes::IsSystemInputWindow(hwnd)) return TRUE;
        if (GetWindow(hwnd, GW_OWNER) != NULL) return TRUE;
        LONG ex = GetWindowLong(hwnd, GWL_EXSTYLE);
        if (ex & WS_EX_TOOLWINDOW) return TRUE;
        if (hwnd == g_hWnd) return TRUE;
        wchar_t title[512]; title[0]=0; GetWindowTextW(hwnd, title, 512);
        if (wcslen(title) == 0) return TRUE;
        wchar_t cls[256]; cls[0]=0; GetClassNameW(hwnd, cls, 256);
        if (wcscmp(cls, L"Progman") == 0) return TRUE;
        if (wcscmp(cls, L"Shell_TrayWnd") == 0) return TRUE;
        if (wcscmp(cls, L"Shell_SecondaryTrayWnd") == 0) return TRUE;
        if (wcscmp(cls, L"WorkerW") == 0) return TRUE;
        HICON hIcon = (HICON)GetClassLongPtr(hwnd, GCLP_HICON);
        if (!hIcon) hIcon = (HICON)SendMessage(hwnd, WM_GETICON, ICON_SMALL2, 0);
        if (!hIcon) hIcon = (HICON)SendMessage(hwnd, WM_GETICON, ICON_SMALL, 0);
        if (!hIcon) hIcon = (HICON)SendMessage(hwnd, WM_GETICON, ICON_BIG, 0);
        if (!hIcon) hIcon = LoadIcon(NULL, IDI_APPLICATION);
        WinInfo wi;
        wi.hwnd = hwnd;
        wi.title = title;
        wi.icon = HIconToPngBase64(hIcon);
        wi.active = (hwnd == data->foreground);
        data->list->push_back(wi);
        return TRUE;
    }, reinterpret_cast<LPARAM>(&ctx));
    return list;
}

void SendAppsToJS() {
    if (!g_webView) return;
    auto list = CollectTaskWindows();
    std::ostringstream ss;
    ss << "{\"type\":\"apps\",\"apps\":[";
    bool first = true;
    for (auto& w : list) {
        if (!first) ss << ",";
        first = false;
        ss << "{\"hwnd\":\"" << (long long)w.hwnd << "\",\"title\":\"" << JsonEscape(WideToUtf8(w.title))
           << "\",\"icon\":\"" << w.icon << "\",\"active\":" << (w.active ? 1 : 0) << "}";
    }
    ss << "]}";
    std::wstring msg = Utf8ToWide(ss.str());
    g_webView->PostWebMessageAsString(msg.c_str());
    SendTrayIconsToJS();
}

std::string TrayIconToPng(HICON hIcon);

// ---------------- custom tray ----------------
std::string GdiBitmapToPngBase64(Gdiplus::Bitmap* bmp);

static bool BitmapHasContent(Gdiplus::Bitmap* bmp) {
    if (!bmp) return false;
    Gdiplus::BitmapData data{};
    Gdiplus::Rect all(0, 0, bmp->GetWidth(), bmp->GetHeight());
    if (bmp->LockBits(&all, Gdiplus::ImageLockModeRead, PixelFormat32bppARGB, &data) != Gdiplus::Ok)
        return false;
    bool ok = false;
    auto* px = static_cast<DWORD*>(data.Scan0);
    const int stride = data.Stride / 4;
    for (int y = 0; y < bmp->GetHeight() && !ok; ++y) {
        for (int x = 0; x < bmp->GetWidth(); ++x) {
            DWORD v = px[y * stride + x];
            int a = (v >> 24) & 0xFF;
            int r = (v >> 16) & 0xFF;
            int g = (v >> 8) & 0xFF;
            int b = v & 0xFF;
            if (a > 10 && (r + g + b) > 30) { ok = true; break; }
        }
    }
    bmp->UnlockBits(&data);
    return ok;
}

// Build a true 32bpp ARGB bitmap from HICON: color DIB + AND-mask as alpha.
// GDI+ Bitmap::FromHICON frequently yields black squares for window icons.
std::string TrayIconToPng(HICON hIcon) {
    ICONINFO info{};
    if (!GetIconInfo(hIcon, &info) || !info.hbmColor) {
        if (info.hbmColor) DeleteObject(info.hbmColor);
        if (info.hbmMask) DeleteObject(info.hbmMask);
        return {};
    }

    BITMAP color{};
    if (!GetObject(info.hbmColor, sizeof(color), &color) ||
        color.bmWidth <= 0 || color.bmHeight <= 0) {
        DeleteObject(info.hbmColor);
        if (info.hbmMask) DeleteObject(info.hbmMask);
        return {};
    }
    const int w = color.bmWidth;
    const int h = color.bmHeight;

    HDC screen = GetDC(nullptr);
    HDC mem = CreateCompatibleDC(screen);
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h; // top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HBITMAP dib = CreateDIBSection(mem, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    std::string result;
    if (dib && bits) {
        HGDIOBJ old = SelectObject(mem, dib);
        HDC colorDC = CreateCompatibleDC(screen);
        HGDIOBJ oldColor = SelectObject(colorDC, info.hbmColor);
        BitBlt(mem, 0, 0, w, h, colorDC, 0, 0, SRCCOPY);
        SelectObject(colorDC, oldColor);
        DeleteDC(colorDC);

        // AND mask: 1 bpp, bottom half of hbmMask. Set alpha=0 where masked.
        if (info.hbmMask) {
            BITMAP mask{};
            if (GetObject(info.hbmMask, sizeof(mask), &mask) && mask.bmHeight >= h * 2) {
                const int stride = ((w + 31) / 32) * 4;
                std::vector<BYTE> maskBits(stride * h);
                BITMAPINFO mbi{};
                mbi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
                mbi.bmiHeader.biWidth = w;
                mbi.bmiHeader.biHeight = -h;
                mbi.bmiHeader.biPlanes = 1;
                mbi.bmiHeader.biBitCount = 1;
                mbi.bmiHeader.biCompression = BI_RGB;
                HDC maskDC = CreateCompatibleDC(screen);
                GetDIBits(maskDC, info.hbmMask, h, h, maskBits.data(), &mbi, DIB_RGB_COLORS);
                DeleteDC(maskDC);
                auto* px = static_cast<DWORD*>(bits);
                for (int y = 0; y < h; ++y) {
                    for (int x = 0; x < w; ++x) {
                        if (maskBits[y * stride + (x / 8)] & (0x80 >> (x % 8))) {
                            px[y * w + x] &= 0x00FFFFFF; // alpha = 0
                        } else {
                            px[y * w + x] |= 0xFF000000; // alpha = 255
                        }
                    }
                }
            }
        }

        Gdiplus::Bitmap bmp(w, h, w * 4, PixelFormat32bppARGB, static_cast<BYTE*>(bits));
        result = GdiBitmapToPngBase64(&bmp);
        SelectObject(mem, old);
    }
    if (dib) DeleteObject(dib);
    DeleteDC(mem);
    ReleaseDC(nullptr, screen);
    DeleteObject(info.hbmColor);
    if (info.hbmMask) DeleteObject(info.hbmMask);
    return result;
}

// Skip system tray icons the user never asked to duplicate (network, sound).
static bool IsExcludedTrayTitle(const std::wstring& title) {
    std::wstring t;
    t.reserve(title.size());
    for (wchar_t c : title) t.push_back(static_cast<wchar_t>(towlower(c)));
    static const wchar_t* bad[] = {
        L"network", L"internet", L"wi-fi", L"wifi", L"ethernet",
        L"volume", L"sound", L"audio", L"speaker", L"microphone",
        L"сеть", L"интернет", L"звук", L"громкост", L"динамик", L"микрофон",
        L"battery", L"батаре", L"power", L"питани",
        L"chrome", L"google", L"edge", L"microsoft edge", L"msedge",
        L"opera", L"firefox", L"браузер",
        L"vol", L"net", L"wi-fi", L"wifi", L"eth", L"spk", L"mic",
        L"громк", L"инет", L"сетев", L"звуков", L"дин", L"микрофон"
    };
    for (const auto* b : bad) if (t.find(b) != std::wstring::npos) return true;
    return false;
}

// Explicit 64-bit TBBUTTON layout (32 bytes in explorer.exe x64).
struct TBBUTTON64 {
    int       iBitmap;
    int       idCommand;
    BYTE      fsState;
    BYTE      fsStyle;
    BYTE      bReserved[6];
    DWORD_PTR dwData;
    INT_PTR   iString;
};
static_assert(sizeof(TBBUTTON64) == 32, "TBBUTTON must match x64 explorer layout");

#pragma pack(push, 1)
struct TRAYDATA {
    HWND  hwnd;
    UINT  uID;
    UINT  uCallbackMessage;
    DWORD reserved[2];
    HICON hIcon;
};
#pragma pack(pop)

std::string GdiBitmapToPngBase64(Gdiplus::Bitmap* bmp) {
    if (!bmp) return {};
    IStream* stream = nullptr;
    if (CreateStreamOnHGlobal(nullptr, TRUE, &stream) != S_OK || !stream) return {};
    CLSID pngClsid; bool found = false;
    UINT num = 0, size = 0;
    GetImageEncodersSize(&num, &size);
    if (size > 0) {
        std::vector<Gdiplus::ImageCodecInfo> codecs(size / sizeof(Gdiplus::ImageCodecInfo));
        Gdiplus::GetImageEncoders(num, size, codecs.data());
        for (UINT i = 0; i < num; i++) {
            if (wcscmp(codecs[i].MimeType, L"image/png") == 0) { pngClsid = codecs[i].Clsid; found = true; break; }
        }
    }
    if (!found || bmp->Save(stream, &pngClsid, nullptr) != Gdiplus::Ok) {
        stream->Release(); return {};
    }
    STATSTG st{}; stream->Stat(&st, STATFLAG_NONAME);
    ULONG sz = (ULONG)st.cbSize.QuadPart;
    std::vector<unsigned char> buf(sz);
    LARGE_INTEGER li{}; stream->Seek(li, STREAM_SEEK_SET, nullptr);
    ULONG read = 0; stream->Read(buf.data(), sz, &read);
    stream->Release();
    if (!read) return {};
    buf.resize(read);
    return "data:image/png;base64," + Base64Encode(buf);
}

struct TrayIconInfo {
    HWND hwnd; UINT uid; std::wstring title; std::string icon;
    HWND toolbar; int cx; int cy; bool activate; std::wstring launch;
    UINT callback; std::wstring exePath;
};

// Icons are captured ONCE at startup while the real Explorer tray is still
// visible. Cross-process HICON duplication is unreliable on modern Windows.
static std::vector<TrayIconInfo> g_trayCache;

void ScanTrayToolbar(HWND tb, std::vector<TrayIconInfo>& out) {
    if (!tb || !IsWindow(tb)) return;
    DWORD pid = 0;
    GetWindowThreadProcessId(tb, &pid);
    HANDLE hp = OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION, FALSE, pid);
    if (!hp) return;

    const int count = static_cast<int>(SendMessageW(tb, TB_BUTTONCOUNT, 0, 0));
    LPVOID remoteBtn = VirtualAllocEx(hp, nullptr, sizeof(TBBUTTON64), MEM_COMMIT, PAGE_READWRITE);
    LPVOID remoteText = VirtualAllocEx(hp, nullptr, sizeof(wchar_t) * 260, MEM_COMMIT, PAGE_READWRITE);
    LPVOID remoteRect = VirtualAllocEx(hp, nullptr, sizeof(RECT), MEM_COMMIT, PAGE_READWRITE);
    std::vector<std::wstring> seenExes;
    if (remoteBtn && remoteText && remoteRect) {
        for (int i = 0; i < count && i < 64; ++i) {
            TBBUTTON64 btn{};
            if (!SendMessageW(tb, TB_GETBUTTON, i, reinterpret_cast<LPARAM>(remoteBtn))) continue;
            if (!ReadProcessMemory(hp, remoteBtn, &btn, sizeof(btn), nullptr)) continue;

            wchar_t text[260]{};
            SendMessageW(tb, TB_GETBUTTONTEXTW, btn.idCommand, reinterpret_cast<LPARAM>(remoteText));
            ReadProcessMemory(hp, remoteText, text, sizeof(wchar_t) * 259, nullptr);
            if (IsExcludedTrayTitle(text)) continue;

            RECT rc{};
            SendMessageW(tb, TB_GETITEMRECT, i, reinterpret_cast<LPARAM>(remoteRect));
            ReadProcessMemory(hp, remoteRect, &rc, sizeof(rc), nullptr);
            const int cx = (rc.left + rc.right) / 2;
            const int cy = (rc.top + rc.bottom) / 2;

            TRAYDATA data{};
            if (!btn.dwData || !ReadProcessMemory(hp, reinterpret_cast<LPCVOID>(btn.dwData),
                                                  &data, sizeof(data), nullptr) || !data.hwnd) {
                continue; // no owning window — not a real notification icon
            }

            // Get the real icon from the owning application's EXE. This works
            // cross-process on both stock Windows and stripped builds.
            std::string icon;
            std::wstring exePath;
            DWORD ownerPid = 0;
            GetWindowThreadProcessId(data.hwnd, &ownerPid);
            HANDLE hOwner = ownerPid ? OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ,
                                                   FALSE, ownerPid) : nullptr;
            if (hOwner) {
                wchar_t exe[MAX_PATH]{};
                DWORD len = MAX_PATH;
                if (QueryFullProcessImageNameW(hOwner, 0, exe, &len) && len > 0) {
                    exePath = exe;
                    std::wstring exeName = exe;
                    size_t slash = exeName.find_last_of(L"\\/");
                    if (slash != std::wstring::npos) exeName = exeName.substr(slash + 1);
                    for (wchar_t& c : exeName) c = static_cast<wchar_t>(towlower(c));
                    // Explorer itself owns several system buttons; its folder
                    // icon is never a real notification icon.
                    const bool systemOwner =
                        exeName == L"explorer.exe" || exeName == L"system" ||
                        exeName == L"svchost.exe" || exeName == L"winlogon.exe" ||
                        exeName == L"csrss.exe" || exeName == L"services.exe" ||
                        exeName == L"lsass.exe" || exeName == L"dwm.exe";
                    // One icon per application is enough.
                    bool duplicate = false;
                    for (const auto& seen : seenExes) {
                        if (seen == exe) { duplicate = true; break; }
                    }
                    if (!systemOwner && !duplicate) {
                        seenExes.push_back(exe);
                        SHFILEINFOW sfi{};
                        if (SHGetFileInfoW(exe, 0, &sfi, sizeof(sfi),
                                           SHGFI_ICON | SHGFI_SMALLICON)) {
                            if (sfi.hIcon) {
                                icon = TrayIconToPng(sfi.hIcon);
                                DestroyIcon(sfi.hIcon);
                            }
                        }
                    }
                }
                CloseHandle(hOwner);
            }
            if (icon.empty()) continue;

            out.push_back({ data.hwnd, data.uID, text, icon, tb, cx, cy, false, L"",
                            data.uCallbackMessage, exePath });
        }
    }
    if (remoteBtn) VirtualFreeEx(hp, remoteBtn, 0, MEM_RELEASE);
    if (remoteText) VirtualFreeEx(hp, remoteText, 0, MEM_RELEASE);
    if (remoteRect) VirtualFreeEx(hp, remoteRect, 0, MEM_RELEASE);
    CloseHandle(hp);
}

HWND FindToolbarRecursive(HWND parent) {
    if (!parent) return nullptr;
    HWND found = nullptr;
    EnumChildWindows(parent, [](HWND h, LPARAM lp) -> BOOL {
        wchar_t cls[128]{};
        GetClassNameW(h, cls, 128);
        if (wcscmp(cls, L"ToolbarWindow32") == 0) {
            int n = static_cast<int>(SendMessageW(h, TB_BUTTONCOUNT, 0, 0));
            if (n > 0) { *reinterpret_cast<HWND*>(lp) = h; return FALSE; }
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&found));
    return found;
}

std::vector<TrayIconInfo> CollectTrayIcons() {
    std::vector<TrayIconInfo> result;
    HWND tray = FindWindowW(L"Shell_TrayWnd", NULL);
    HWND notify = FindWindowExW(tray, NULL, L"TrayNotifyWnd", NULL);
    ScanTrayToolbar(FindToolbarRecursive(notify), result);

    if (HWND overflow = FindWindowW(L"NotifyIconOverflowWindow", NULL)) {
        ScanTrayToolbar(FindToolbarRecursive(overflow), result);
    }
    return result;
}

void SendTrayIconsToJS() {
    if (!g_webView) return;
    // Explorer may re-show its overflow flyout; keep it hidden permanently.
    if (HWND overflow = FindWindowW(L"NotifyIconOverflowWindow", NULL)) {
        ShowWindow(overflow, SW_HIDE);
    }
        // Rescan live: tray windows/callbacks change when apps restart.
    std::vector<TrayIconInfo> icons = CollectTrayIcons();
    std::ostringstream ss;
    ss << "{\"type\":\"trayIcons\",\"apps\":[";
    bool first = true;
    for (const auto& t : icons) {
        if (!first) ss << ",";
        first = false;
        ss << "{\"hwnd\":\"" << (long long)t.hwnd << "\",\"uid\":" << t.uid
           << ",\"tb\":\"" << (long long)t.toolbar << "\",\"cx\":" << t.cx
           << ",\"cy\":" << t.cy
           << ",\"activate\":" << (t.activate ? 1 : 0)
           << ",\"launch\":\"" << JsonEscape(WideToUtf8(t.launch))
           << "\",\"cb\":" << t.callback
           << ",\"exe\":\"" << JsonEscape(WideToUtf8(t.exePath))
           << "\",\"title\":\"" << JsonEscape(WideToUtf8(t.title))
           << "\",\"icon\":\"" << t.icon << "\"}";
    }
    ss << "]}";
    std::wstring msg = Utf8ToWide(ss.str());
    g_webView->PostWebMessageAsString(msg.c_str());
}

// Right-click menu shown for FlyStack tray icons. Some apps (Qt/Telegram)
// ignore a synthesized tray callback when their window is hidden, so the menu
// is owned by FlyStack and performs the same useful actions directly.
static bool ContainsCaseless(const std::wstring& text, const wchar_t* needle) {
    std::wstring a = text, b = needle;
    std::transform(a.begin(), a.end(), a.begin(), [](wchar_t c) { return (wchar_t)towlower(c); });
    std::transform(b.begin(), b.end(), b.begin(), [](wchar_t c) { return (wchar_t)towlower(c); });
    return a.find(b) != std::wstring::npos;
}

// Win+S opens the real Windows Search panel on the left side of the screen.
// No custom index, no custom window: this is the OS search the user already
// knows, with apps, files, web and settings.
static void OpenWindowsSearch() {
    INPUT keys[4]{};
    keys[0].type = keys[1].type = keys[2].type = keys[3].type = INPUT_KEYBOARD;
    keys[0].ki.wVk = VK_LWIN;
    keys[1].ki.wVk = L'S';
    keys[2].ki.wVk = L'S';
    keys[2].ki.dwFlags = KEYEVENTF_KEYUP;
    keys[3].ki.wVk = VK_LWIN;
    keys[3].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(4, keys, sizeof(INPUT));
}

static void ShowTrayContextMenu(const POINT& pt, HWND target, UINT uid, UINT cb,
                                const std::wstring& exe, const std::wstring& title) {
    HMENU menu = CreatePopupMenu();
    if (!menu) return;

    std::wstring openLabel = L"Открыть";
    if (!title.empty()) openLabel += L" " + title;
    AppendMenuW(menu, MF_STRING, 1, openLabel.c_str());
    AppendMenuW(menu, MF_STRING, 2, L"Настройки уведомлений");

    const bool steam = ContainsCaseless(exe, L"steam.exe");
    if (steam) {
        AppendMenuW(menu, MF_STRING, 3, L"Библиотека Steam — запустить игру");
    }
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, 4, L"Закрыть меню");

    SetForegroundWindow(g_hWnd);
    UINT cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
                              pt.x, pt.y, 0, g_hWnd, nullptr);
    PostMessageW(g_hWnd, WM_NULL, 0, 0);
    DestroyMenu(menu);

    switch (cmd) {
    case 1:
        if (!exe.empty()) {
            ShellExecuteW(NULL, L"open", exe.c_str(), NULL, NULL, SW_SHOWNORMAL);
        } else if (target && IsWindow(target) && cb >= WM_USER && cb <= 0xFFFF) {
            SendMessageW(target, cb, uid, WM_LBUTTONDOWN);
            SendMessageW(target, cb, uid, WM_LBUTTONUP);
        }
        break;
    case 2:
        ShellExecuteW(NULL, L"open", L"ms-settings:notifications", NULL, NULL, SW_SHOWNORMAL);
        break;
    case 3:
        ShellExecuteW(NULL, L"open", L"steam://open/games", NULL, NULL, SW_SHOWNORMAL);
        break;
    default:
        break;
    }
}

// ---------------- webview2 ----------------
void InitWebView() {
    HRESULT hr = CreateCoreWebView2EnvironmentWithOptions(
        nullptr, nullptr, nullptr,
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [](HRESULT result, ICoreWebView2Environment* env) -> HRESULT {
                if (FAILED(result) || !env) return result;
                env->CreateCoreWebView2Controller(
                    g_hWnd,
                    Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                        [](HRESULT result, ICoreWebView2Controller* controller) -> HRESULT {
                            if (FAILED(result) || !controller) return result;
                            g_webViewController = controller;
                            controller->get_CoreWebView2(&g_webView);

                            UpdateWebViewBounds();
                            controller->put_IsVisible(TRUE);
                            InstallWebViewHitTestSubclass();
                            // Chromium creates the renderer HWND slightly later.
                            // Re-assert the region once it exists; otherwise the
                            // renderer keeps swallowing clicks outside the dock.
                            SetTimer(g_hWnd, 9, 700, nullptr);

                            ComPtr<ICoreWebView2Settings> settings;
                            g_webView->get_Settings(&settings);
                            if (settings) {
                                settings->put_IsStatusBarEnabled(FALSE);
                                // Tray icons own their contextmenu event; WebView's
                                // built-in menu would swallow it and open instead.
                                settings->put_AreDefaultContextMenusEnabled(FALSE);
                            }
                            ComPtr<ICoreWebView2Controller2> c2;
                            if (SUCCEEDED(controller->QueryInterface(IID_PPV_ARGS(&c2)))) {
                                // COREWEBVIEW2_COLOR is { R, G, B, A };
                                // all zeroes = fully transparent WebView canvas.
                                COREWEBVIEW2_COLOR transparent = { 0, 0, 0, 0 };
                                c2->put_DefaultBackgroundColor(transparent);
                            }

                            g_webView->add_WebMessageReceived(
                                Callback<ICoreWebView2WebMessageReceivedEventHandler>(
                                    [](ICoreWebView2* sender, ICoreWebView2WebMessageReceivedEventArgs* args) -> HRESULT {
                                        LPWSTR json = nullptr;
                                        args->get_WebMessageAsJson(&json);
                                        if (json) {
                                            std::wstring msg(json);
                                            auto findVal = [&](const std::wstring& key) -> std::wstring {
                                                std::wstring k = L"\"" + key + L"\"";
                                                size_t p = msg.find(k);
                                                if (p == std::wstring::npos) return L"";
                                                size_t c = msg.find(L":", p + k.size());
                                                if (c == std::wstring::npos) return L"";
                                                size_t i = c + 1;
                                                while (i < msg.size() && (msg[i] == L' ' || msg[i] == L'\t' ||
                                                       msg[i] == L'\r' || msg[i] == L'\n')) i++;
                                                if (i < msg.size() && msg[i] == L'"') {
                                                    size_t q = msg.find(L'"', i + 1);
                                                    if (q == std::wstring::npos) return L"";
                                                    return msg.substr(i + 1, q - i - 1);
                                                }
                                                size_t e = i;
                                                while (e < msg.size()) {
                                                    wchar_t ch = msg[e];
                                                    if ((ch >= L'0' && ch <= L'9') || ch == L'-' || ch == L'+' ||
                                                        ch == L'.' || ch == L'e' || ch == L'E') e++;
                                                    else break;
                                                }
                                                return msg.substr(i, e - i);
                                            };
                                            std::wstring type = findVal(L"type");
                                            if (type == L"capsule") {
                                                LONG x = (LONG)wcstol(findVal(L"x").c_str(), nullptr, 10);
                                                LONG y = (LONG)wcstol(findVal(L"y").c_str(), nullptr, 10);
                                                LONG w = (LONG)wcstol(findVal(L"w").c_str(), nullptr, 10);
                                                LONG h = (LONG)wcstol(findVal(L"h").c_str(), nullptr, 10);
                                                g_CapsuleRectCss = RECT{ x, y, x + w, y + h };
                                            } else if (type == L"flyout") {
                                                std::wstring open = findVal(L"open");
                                                BOOL isOpen = (open == L"1" || open == L"true");
                                                if (isOpen) {
                                                    LONG x = (LONG)wcstol(findVal(L"x").c_str(), nullptr, 10);
                                                    LONG y = (LONG)wcstol(findVal(L"y").c_str(), nullptr, 10);
                                                    LONG w = (LONG)wcstol(findVal(L"w").c_str(), nullptr, 10);
                                                    LONG h = (LONG)wcstol(findVal(L"h").c_str(), nullptr, 10);
                                                    g_FlyoutRectCss = RECT{ x, y, x + w, y + h };
                                                }
                                                SetFlyout(g_hWnd, isOpen);
                                            } else if (type == L"traySlot") {
                                                std::wstring on = findVal(L"on");
                                                const bool visible = (on == L"1" || on == L"true");
                                                TrayHost::OnSlotRect(
                                                    (int)wcstol(findVal(L"x").c_str(), nullptr, 10),
                                                    (int)wcstol(findVal(L"y").c_str(), nullptr, 10),
                                                    (int)wcstol(findVal(L"w").c_str(), nullptr, 10),
                                                    (int)wcstol(findVal(L"h").c_str(), nullptr, 10),
                                                    visible);
                                            } else if (type == L"trayModeAck") {
                                                std::wstring native = findVal(L"native");
                                                TrayHost::OnModeAck(native == L"1" || native == L"true");
                                            } else if (type == L"modeDone") {
                                                TaskbarModes::OnCssTransitionDone();
                                            } else if (type == L"activate") {
                                                std::wstring h = findVal(L"hwnd");
                                                HWND target = (HWND)wcstoull(h.c_str(), nullptr, 10);
                                                if (IsWindow(target)) {
                                                    HWND fgWnd = GetForegroundWindow();
                                                    if (fgWnd == target && !IsIconic(target)) {
                                                        // Already on the foreground, not minimized — minimize it.
                                                        ShowWindow(target, SW_MINIMIZE);
                                                    } else {
                                                        if (IsIconic(target)) ShowWindow(target, SW_RESTORE);
                                                        DWORD fgTid = GetWindowThreadProcessId(fgWnd, NULL);
                                                        DWORD tgTid = GetWindowThreadProcessId(target, NULL);
                                                        if (fgTid != tgTid) AttachThreadInput(fgTid, tgTid, TRUE);
                                                        SetForegroundWindow(target);
                                                        BringWindowToTop(target);
                                                        if (fgTid != tgTid) AttachThreadInput(fgTid, tgTid, FALSE);
                                                    }
                                                }
                                            } else if (type == L"launch") {
                                                std::wstring cmd = findVal(L"cmd");
                                                ShellExecuteW(NULL, L"open", cmd.c_str(), NULL, NULL, SW_SHOWNORMAL);
                                            } else if (type == L"trayClick") {
                                                HWND target = reinterpret_cast<HWND>(
                                                    wcstoull(findVal(L"hwnd").c_str(), nullptr, 10));
                                                UINT uid = static_cast<UINT>(
                                                    wcstoul(findVal(L"uid").c_str(), nullptr, 10));
                                                int button = wcstol(findVal(L"button").c_str(), nullptr, 10);
                                                const bool rightClick = (button == 2);
                                                const UINT down = rightClick ? WM_RBUTTONDOWN : WM_LBUTTONDOWN;
                                                const UINT up   = rightClick ? WM_RBUTTONUP   : WM_LBUTTONUP;
                                                POINT pt{}; GetCursorPos(&pt);
                                                UINT cb = static_cast<UINT>(
                                                    wcstoul(findVal(L"cb").c_str(), nullptr, 10));
                                                HWND tb = reinterpret_cast<HWND>(
                                                    wcstoull(findVal(L"tb").c_str(), nullptr, 10));
                                                int cx = wcstol(findVal(L"cx").c_str(), nullptr, 10);
                                                int cy = wcstol(findVal(L"cy").c_str(), nullptr, 10);

                                                if (rightClick) {
                                                    // Keep the app window closed on PKM and show a
                                                    // real Win32 menu with the useful tray actions.
                                                    ShowTrayContextMenu(pt, target, uid, cb,
                                                                        findVal(L"exe"),
                                                                        findVal(L"title"));
                                                } else {
                                                    // Left-click keeps the FlyStack behavior: open or
                                                    // activate the application window.
                                                    int activate = wcstol(findVal(L"activate").c_str(), nullptr, 10);
                                                    std::wstring launch = findVal(L"launch");
                                                    if (!launch.empty()) {
                                                        ShellExecuteW(NULL, L"open", launch.c_str(), NULL, NULL, SW_SHOWNORMAL);
                                                    } else if (activate && target && IsWindow(target)) {
                                                        if (IsIconic(target)) ShowWindow(target, SW_RESTORE);
                                                        SetForegroundWindow(target);
                                                    } else if (target && IsWindow(target)) {
                                                        // Notify-icon convention: the app's own
                                                        // uCallbackMessage carries the mouse event.
                                                        if (cb >= WM_USER && cb <= 0xFFFF) {
                                                            // Qt-based tray icons (Telegram) process
                                                            // the callback synchronously — SendMessage
                                                            // is required, not SendNotifyMessage.
                                                            SendMessageW(target, cb, uid, WM_LBUTTONDOWN);
                                                            SendMessageW(target, cb, uid, WM_LBUTTONUP);
                                                        } else {
                                                            SendNotifyMessageW(target, down, uid, MAKELPARAM(pt.x, pt.y));
                                                            SendNotifyMessageW(target, up,   uid, MAKELPARAM(pt.x, pt.y));
                                                        }
                                                    } else if (tb && IsWindow(tb)) {
                                                        SendMessageW(tb, down, MK_LBUTTON, MAKELPARAM(cx, cy));
                                                        SendMessageW(tb, up,   0,          MAKELPARAM(cx, cy));
                                                    }
                                                    // Last-resort fallback for apps whose tray callback
                                                    // we cannot drive reliably: launching the EXE
                                                    // activates the running app.
                                                    std::wstring exe = findVal(L"exe");
                                                    if (!exe.empty()) {
                                                        ShellExecuteW(NULL, L"open", exe.c_str(),
                                                                      NULL, NULL, SW_SHOWNORMAL);
                                                    }
                                                }
                                            } else if (type == L"nativeSearch") {
                                                OpenWindowsSearch();
                                            } else if (type == L"settingsUrl") {
                                                std::wstring url = findVal(L"url");
                                                if (!url.empty()) {
                                                    ShellExecuteW(NULL, L"open", url.c_str(), NULL, NULL, SW_SHOWNORMAL);
                                                }
                                            } else if (type == L"preview") {
                                                std::wstring show = findVal(L"show");
                                                if (show == L"1" || show == L"true") {
                                                    HWND src = reinterpret_cast<HWND>(
                                                        wcstoull(findVal(L"hwnd").c_str(), nullptr, 10));
                                                    PreviewHost::Show(
                                                        src, findVal(L"title").c_str(),
                                                        (int)wcstol(findVal(L"x").c_str(), nullptr, 10),
                                                        (int)wcstol(findVal(L"y").c_str(), nullptr, 10),
                                                        (int)wcstol(findVal(L"w").c_str(), nullptr, 10),
                                                        (int)wcstol(findVal(L"h").c_str(), nullptr, 10));
                                                } else {
                                                    PreviewHost::Hide();
                                                }
                                            } else if (type == L"settings") {
                                                ShellExecuteW(NULL, L"open", L"ms-settings:", NULL, NULL, SW_SHOWNORMAL);
                                            }
                                            CoTaskMemFree(json);
                                        }
                                        return S_OK;
                                    }).Get(), nullptr);

                            // Apply the mode as soon as the document is ready —
                            // the only reliable point where __flySetMode exists.
                            g_webView->add_NavigationCompleted(
                                Callback<ICoreWebView2NavigationCompletedEventHandler>(
                                    [](ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs*) -> HRESULT {
                                        NotifyWebMode(TaskbarModes::IsExpanded());
                                        UpdateWebViewBounds();
                                        if (!TrayHost::IsNativeActive()) SendTrayIconsToJS();
                                        TrayHost::PushState();
                                        return S_OK;
                                    }).Get(), nullptr);

                            g_webView->NavigateToString(g_htmlContent);
                            // WebView was created after TaskbarModes::Init(), so
                            // also send the initial mode immediately.
                            NotifyWebMode(TaskbarModes::IsExpanded());
                            // Repeat once after navigation settles in case the
                            // page had not installed its message listener yet.
                            SetTimer(g_hWnd, 4, 350, nullptr);
                            return S_OK;
                        }).Get());
                return S_OK;
            }).Get());
    if (FAILED(hr)) {
        MessageBoxW(g_hWnd, L"Не удалось инициализировать WebView2.", L"Ошибка", MB_ICONERROR);
    }
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_NCCALCSIZE:
        // Client area == window rect; never return WVR_VALIDRECTS so Windows
        // cannot BitBlt the previous frame into the top of a taller window.
        if (wParam == TRUE) return 0;
        break;
    case WM_NCHITTEST: {
        LRESULT r = DefWindowProc(hWnd, message, wParam, lParam);
        if (r == HTCLIENT) {
            // Click-through outside the capsule (and the open flyout).
            POINT pt = { (SHORT)LOWORD(lParam), (SHORT)HIWORD(lParam) };
            ScreenToClient(hWnd, &pt);
            BOOL hit = PointInRect(pt.x, pt.y, g_CapsuleRectCss);
            if (!hit && g_FlyoutOpen) hit = PointInRect(pt.x, pt.y, g_FlyoutRectCss);
            // Before the page reports the capsule rect, keep the window interactive.
            BOOL knowCapsule = (g_CapsuleRectCss.right > g_CapsuleRectCss.left &&
                                g_CapsuleRectCss.bottom > g_CapsuleRectCss.top);
            if (!hit && knowCapsule) return HTTRANSPARENT;
        }
        return r;
    }
    case WM_CREATE: {
        MARGINS margins = { -1, -1, -1, -1 };
        DwmExtendFrameIntoClientArea(hWnd, &margins);
        break;
    }
    case WM_SIZE:
        InstallWebViewHitTestSubclass(); // retry until WebView child exists
        UpdateWebViewBounds();  // full client width, origin (0,0)
        TaskbarModes::OnSize(); // re-assert acrylic after DWM resets it on resize
        TrayHost::OnCarrierResized();
        break;
    case WM_DPICHANGED: {
        g_Dpi = (UINT)HIWORD(wParam);
        RECT* const suggested = reinterpret_cast<RECT*>(lParam);
        (void)suggested;
        // Recompute capsule/full geometry for the new DPI instead of using the
        // system-suggested rect (which would fight the current mode).
        TaskbarModes::ApplyGeometryNow();
        UpdateWebViewBounds();
        break;
    }
    case WM_TIMER:
        if (wParam == 9) {
            KillTimer(hWnd, 9);
            ApplyCarrierRegion();  // renderer HWND now exists
            break;
        }
        if (wParam == 6 || wParam == 1) {
            TrayHost::Tick();
            PreviewHost::Tick();
        }
        if (wParam == TaskbarModes::TIMER_SETTLE) {
            TaskbarModes::OnSettleTimer();
        } else if (wParam == TaskbarModes::TIMER_DEBOUNCE) {
            KillTimer(hWnd, TaskbarModes::TIMER_DEBOUNCE);
            TaskbarModes::Refresh();
        } else if (wParam == 4) {
            KillTimer(hWnd, 4);
            NotifyWebMode(TaskbarModes::IsExpanded());
        } else {
            SendAppsToJS(); // existing 2 s app-list refresh
            if (!TrayHost::IsNativeActive()) SendTrayIconsToJS();
        }
        break;
    case TaskbarModes::WM_TASKBAR_WINEVENT:
        // EVENT_OBJECT_LOCATIONCHANGE fires extremely often; collapse bursts.
        SetTimer(hWnd, TaskbarModes::TIMER_DEBOUNCE, TaskbarModes::DEBOUNCE_MS, NULL);
        break;
    case TaskbarModes::WM_TASKBAR_MODE:
        NotifyWebMode(wParam != FALSE);
        break;
    case WM_APPBAR:
        break;
    case WM_DESTROY: {
        PreviewHost::Shutdown();
        TrayHost::Shutdown();
        TaskbarModes::Shutdown();
        HWND tray = FindWindowW(L"Shell_TrayWnd", NULL);
        if (tray) ShowWindow(tray, SW_SHOW);
        PostQuitMessage(0);
        break;
    }
    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow) {
    GdiplusStartupInput gsi; gsi.GdiplusVersion = 1; gsi.DebugEventCallback = nullptr; gsi.SuppressBackgroundThread = FALSE; gsi.SuppressExternalCodecs = FALSE;
    GdiplusStartup(&g_gdiplusToken, &gsi, nullptr);

    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    HWND tray = FindWindowW(L"Shell_TrayWnd", NULL);
    // Capture icons BEFORE hiding the real tray: PrintWindow of a hidden
    // toolbar returns an empty bitmap, which is why the popup was empty.
    g_trayCache = CollectTrayIcons();
    if (tray) ShowWindow(tray, SW_HIDE);
    // The system tray flyout would otherwise linger as a dead black rectangle
    // in the corner — our custom popup replaces it completely.
    if (HWND overflow = FindWindowW(L"NotifyIconOverflowWindow", NULL)) {
        ShowWindow(overflow, SW_HIDE);
    }

    WNDCLASSEXW wcex = { sizeof(WNDCLASSEX) };
    // No CS_HREDRAW / CS_VREDRAW, no CS_DROPSHADOW: Windows must not repaint a
    // copy of the old client bitmap into the enlarged window (flyout duplicate).
    wcex.style = 0;
    wcex.lpfnWndProc = WndProc;
    wcex.hInstance = hInstance;
    wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
    wcex.hbrBackground = (HBRUSH)GetStockObject(NULL_BRUSH);
    wcex.lpszClassName = L"FlyStackTaskbarClass";
    RegisterClassExW(&wcex);

    int sw = GetSystemMetrics(SM_CXSCREEN);
    int sh = GetSystemMetrics(SM_CYSCREEN);
    const int carrierH = TaskbarModes::CARRIER_HEIGHT_DIP;
    g_hWnd = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
        L"FlyStackTaskbarClass",
        L"FlyStack Taskbar",
        WS_POPUP,
        0, sh - carrierH,
        sw, carrierH,
        NULL, NULL, hInstance, NULL
    );
    if (!g_hWnd) return 0;

    // Initial mode + hooks + appbar + blur, all in one place.
    TaskbarModes::Init(g_hWnd);
    PreviewHost::Init(g_hWnd);

    g_Dpi = GetDpiForWindow(g_hWnd);
    if (!g_Dpi) g_Dpi = 96;

    ShowWindow(g_hWnd, nCmdShow);
    UpdateWindow(g_hWnd);


    InitWebView();

    // Presentation-only wrapper around the real notification area.
    TrayHost::Init(g_hWnd, g_webViewChild);

    SetTimer(g_hWnd, 1, 2000, NULL);
    SetTimer(g_hWnd, 6, 500, NULL);   // tray host watchdog (UI only)

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    g_webViewController.Reset();
    g_webView.Reset();
    CoUninitialize();
    GdiplusShutdown(g_gdiplusToken);
    return (int)msg.wParam;
}