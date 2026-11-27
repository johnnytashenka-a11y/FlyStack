#pragma once

#include <windows.h>

// Native DWM window previews shown when a taskbar icon is hovered.
// All coordinates passed to Show() are CSS pixels relative to the main
// WebView client area; this module converts them to physical/screen pixels.
namespace PreviewHost {

void Init(HWND owner);

void Show(HWND source, const wchar_t* title,
          int anchorX, int anchorY, int anchorW, int anchorH);

void Hide();

// Called from the existing UI watchdog: drops stale thumbnails when the
// previewed window is closed or minimized while the cursor is still away.
void Tick();

void Shutdown();

} // namespace PreviewHost
