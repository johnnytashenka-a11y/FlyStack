#pragma once

#include <windows.h>
#include <string>

// Implemented in main.cpp: posts one JSON string into the WebView page.
void PostJsonToWebView(const std::wstring& json);

// ---------------------------------------------------------------------------
// Visual wrapper around the REAL Windows notification area.
//
// This module owns ONLY presentation: one child window ("tray island") that
// has the real TrayNotifyWnd reparented into it. It never scans icons, never
// sends tray callbacks, never installs hooks and never touches timers that
// belong to the tray provider in main.cpp.
// ---------------------------------------------------------------------------
namespace TrayHost {

void Init(HWND carrier, HWND webViewChild);
void SetWebViewChild(HWND child);

// Rect of the CSS placeholder, in CSS pixels relative to the page viewport.
void OnSlotRect(int x, int y, int w, int h, bool visible);

void OnCarrierResized();

// Page confirmation for the last trayMode message.
void OnModeAck(bool native);

// Cheap watchdog, called from the existing UI timer. Re-acquires the system
// tray after an Explorer restart, follows icon-count changes and re-asserts
// the z-order above WebView2.
void Tick();

bool IsNativeActive();

// Re-send mode + measured width (used after a navigation completed).
void PushState();

void Shutdown();

} // namespace TrayHost
