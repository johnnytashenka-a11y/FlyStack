#pragma once

#include <windows.h>

// ---------------------------------------------------------------------------
// Shared constants. main.cpp uses the same BAR_HEIGHT / FLYOUT_OPEN_H so the
// capsule geometry and the WebView layout height can never drift apart.
// ---------------------------------------------------------------------------
namespace TaskbarModes {

inline constexpr int  BAR_HEIGHT             = 58;
inline constexpr int  FLYOUT_OPEN_H          = 600;
inline constexpr int  DESKTOP_BOTTOM_MARGIN  = 40;   // gap under the capsule
// The carrier is ALWAYS this tall. Resizing it when the calendar opens made
// DWM copy the old bottom strip to the top for one frame.
inline constexpr int  CARRIER_HEIGHT_DIP     = FLYOUT_OPEN_H + DESKTOP_BOTTOM_MARGIN;
inline constexpr int  CAPSULE_WIDTH          = 320;  // must match CSS .dock-capsule max-width

// Must match the callback message used by the appbar in main.cpp.
inline constexpr UINT WM_APPBAR_MSG          = WM_USER + 1;

// Private messages / timer IDs the host WndProc has to forward to this module.
inline constexpr UINT      WM_TASKBAR_WINEVENT = WM_APP + 0x41;
inline constexpr UINT      WM_TASKBAR_MODE     = WM_APP + 0x42;
inline constexpr UINT      DEBOUNCE_MS         = 120;
inline constexpr UINT      CSS_TRANSITION_MS   = 420; // 0.35 s CSS + 2 rAF frames
inline constexpr UINT_PTR  TIMER_SETTLE        = 0x31; // one-shot: after CSS transition
inline constexpr UINT_PTR  TIMER_DEBOUNCE      = 0x32;

// --- public API -------------------------------------------------------------
void Init(HWND hwnd);          // install hooks, apply initial mode (no animation)
void Shutdown();               // unhook, kill timers, remove appbar, drop region

void Refresh();                // re-evaluate state; CSS does the visual animation
void OnSettleTimer();          // forward WM_TIMER(TIMER_SETTLE) here
void OnCssTransitionDone();    // called when WebView reports transitionend
void OnSize();                 // forward WM_SIZE here (blur re-apply after DWM reset)
bool NotifyFlyout(bool open);  // returns true when the state actually changed
void ApplyGeometryNow();       // force current mode geometry (no animation)

bool IsDesktopActive();        // true when there are no visible application windows
bool HasMaximizedWindow();     // true when a window covers the current monitor (state B trigger)
bool IsExpanded();             // true = state B
bool IsSystemInputWindow(HWND hwnd); // IME / TextInputHost pseudo-windows

} // namespace TaskbarModes
