#pragma once

const wchar_t* g_htmlContent = LR"rawhtml(<!DOCTYPE html>
<html lang="ru">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>FlyStack Taskbar</title>
<style>
  :root {
    --bg-wallpaper: radial-gradient(circle at 20% 20%, #1e1b4b 0%, #09090b 55%, #18181b 100%);
    --dock-bg: rgba(24, 24, 28, 0.58);
    --dock-border: rgba(255, 255, 255, 0.14);
    --dock-shadow: 0 20px 50px rgba(0,0,0,0.65), inset 0 1px 0 rgba(255,255,255,0.12);
    --primary: #FFFFFF;
    --glow: rgba(212,212,216,0.45);
    --icon-hover-bg: rgba(255,255,255,0.1);
    --divider: rgba(255,255,255,0.15);
    --hint-color: rgba(255,255,255,0.5);
  }

  body.light-theme {
    --bg-wallpaper: radial-gradient(circle at 80% 20%, #e0e7ff 0%, #f4f4f5 50%, #e4e4e7 100%);
    --dock-bg: rgba(244, 244, 245, 0.58);
    --dock-border: rgba(0, 0, 0, 0.12);
    --dock-shadow: 0 20px 50px rgba(0, 0, 0, 0.12), inset 0 1px 0 rgba(255,255,255,0.8);
    --primary: #000000;
    --glow: rgba(63, 63, 70, 0.42);
    --icon-hover-bg: rgba(0, 0, 0, 0.07);
    --divider: rgba(0, 0, 0, 0.12);
    --hint-color: rgba(0, 0, 0, 0.55);
  }

  * { box-sizing: border-box; margin: 0; padding: 0; user-select: none; }
  html,body { height:100%; width:100%; max-width:none; overflow:hidden; background:transparent !important; margin:0; padding:0; border:none; font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,Inter,sans-serif; transition: background 0.4s ease; }
  /* COLUMN direction makes flex-basis apply to HEIGHT, so width:100% on .bar
     is a real full-screen width instead of a 58px flex-basis. */
  body { display:flex; flex-direction:column; align-items:stretch; justify-content:flex-end; width:100%; max-width:none; margin:0; padding:0; }

  .bar {
    position:fixed; left:0; right:0; bottom:0;
    width:100% !important; max-width:none !important; height:58px; flex:0 0 58px;
    display:flex; flex-direction:column; justify-content:flex-end;
    margin:0; padding:0;
    /* Keep the WebView surface fully transparent: the DWM acrylic tint
       (ACCENT_POLICY.GradientColor = #141416) provides the dark color. */
    background:transparent !important;
  }

  .dock-wrapper { display:flex; align-items:center; justify-content:center; width:100%; max-width:none; margin:0; padding:0; background:transparent !important; }
  .dock-capsule {
    position:relative;
    height:58px; width:fit-content; min-width:320px; max-width:calc(100vw - 48px);
    padding:0 16px; border-radius:999px;
    background: var(--dock-bg);
    backdrop-filter: blur(24px) saturate(160%); -webkit-backdrop-filter: blur(24px) saturate(160%);
    border:1px solid rgba(255,255,255,0.08); box-shadow: 0 20px 50px rgba(0,0,0,0.65), inset 0 1px 0 rgba(255,255,255,0.08);
    display:flex; align-items:center; gap:10px;
    /* All mode motion is done here on the GPU — no SetWindowPos per frame. */
    transform:translateY(-40px);
    transition:width 0.35s cubic-bezier(0.16,1,0.3,1),
               border-radius 0.35s cubic-bezier(0.16,1,0.3,1),
               transform 0.35s cubic-bezier(0.16,1,0.3,1),
               bottom 0.35s cubic-bezier(0.16,1,0.3,1);
    will-change:width, border-radius, transform, background-color;
  }
  /* Expanded mode MUST fill the whole lower edge — no centered 320px pill. */
  body.expanded .bar,
  body.expanded .dock-wrapper {
    width:100% !important;
    max-width:none !important;
    left:0 !important;
    right:0 !important;
    border-radius:0 !important;
  }
  body.expanded .dock-capsule {
    max-width:none !important;
    width:100% !important;
    left:0 !important;
    right:0 !important;
    border-radius:0 !important;
    box-sizing:border-box;
    padding:0 16px;
    justify-content:center;
    align-items:center;
    transform:translateY(0);
  }
  /* Keep the exact same icon spacing as in capsule mode, just centered. */
  body.expanded .apps-scroll-area { margin-right:0; }

  /* Full width: no icon limit — show every app. */
  body.expanded .apps-scroll-area {
    max-width:none !important;
    overflow:visible !important;
    flex:0 0 auto;
    scrollbar-width:none;
  }
  body.expanded .dock-capsule { justify-content:center; }
  body.expanded .main-icons { flex:0 0 auto; }
  body.expanded .system-zone {
    position:absolute; right:16px; top:50%; transform:translateY(-50%); margin-left:0;
  }
  body.expanded #trayIcons .tray-item:nth-child(n+2) { display:flex; }

  .apps-scroll-area { display:flex; align-items:center; gap:8px; overflow:visible; scroll-behavior:smooth; padding:6px 2px; max-width:none; scrollbar-width:none; flex:0 0 auto; }
  .apps-scroll-area::-webkit-scrollbar { display:none; }

  .app-item {
    position:relative; flex:0 0 32px; height:32px; border-radius:9px;
    display:flex; align-items:center; justify-content:center; cursor:pointer;
    color:var(--primary); transition:background 0.15s ease;
    /* No transform on hover: scaling/overshoot made the icon jitter under the cursor. */
  }
  .app-item:hover { background:var(--icon-hover-bg); }
  .app-item img { width:18px; height:18px; border-radius:5px; object-fit:contain; }
  .app-item .letter { font-size:13px; font-weight:600; }
  .app-item.running::after {
    content:""; position:absolute; bottom:1px; width:4px; height:3px; border-radius:99px;
    background:var(--primary); box-shadow:0 0 6px var(--glow); transition:width 0.2s ease;
  }
  .app-item.active::after { width:10px; }

  .divider { width:1px; height:22px; background:var(--divider); flex-shrink:0; }

  .main-icons { display:flex; align-items:center; gap:10px; flex:0 1 auto; min-width:0; }
  .system-zone { display:flex; align-items:center; gap:6px; flex-shrink:0; margin-left:auto; }
  #trayIcons .tray-item:nth-child(n+2) { display:none; }
  .tray-icons { display:flex; align-items:center; gap:4px; }
  .tray-item { width:22px; height:22px; display:flex; align-items:center; justify-content:center; border-radius:6px; cursor:pointer; }
  .tray-item:hover { background:var(--icon-hover-bg); }
)rawhtml"
  LR"rawhtml(  .tray-item img { width:16px; height:16px; object-fit:contain; }
  .tray-item .glyph { width:16px; height:16px; border-radius:5px; display:flex; align-items:center; justify-content:center; background:rgba(255,255,255,0.16); color:#fff; font-size:10px; font-weight:600; }
  /* Placeholder for the native notification area. The island is drawn here in
     CSS so it is composited with the WebView surface; the Win32 host that holds
     TrayNotifyWnd is deliberately transparent (GDI would be DWM-tinted). */
  .tray-slot {
    display:none; width:88px; height:34px; border-radius:12px; flex:0 0 auto;
    box-sizing:border-box; background:rgb(23,23,28); border:1px solid rgb(60,60,70);
    align-items:center; gap:4px; padding:0 6px; overflow:hidden;
  }
  .tray-slot .tray-item { width:16px; height:16px; flex:0 0 auto; }
  .tray-slot .tray-item img { width:14px; height:14px; }
  .tray-slot .tray-item .glyph { width:14px; height:14px; }
  body.native-tray .tray-slot { display:flex; }
  body.native-tray .legacy-tray { display:none !important; }
  .tray-panel {
    color:#fff;
    position:fixed; right:12px; bottom:70px; width:260px; min-height:60px;
    transform:translateY(10px) scale(0.98); opacity:0; pointer-events:none;
    background:var(--dock-bg); backdrop-filter:blur(30px) saturate(180%); -webkit-backdrop-filter:blur(30px) saturate(180%);
    border:1px solid var(--dock-border); border-radius:16px; padding:10px;
    box-shadow:var(--dock-shadow); z-index:1002;
    transition:opacity 0.28s ease, transform 0.28s cubic-bezier(0.16,1,0.3,1);
  }
  .tray-panel.open { transform:translateY(0) scale(1); opacity:1; pointer-events:auto; }
  .tray-panel-title { font-size:11px; color:rgba(255,255,255,0.72); padding:4px 6px 8px; }
  .tray-panel .tray-item { color:#fff; }
  .tray-panel .tray-item:hover { background:rgba(255,255,255,0.14); }
  .sys-btn {
    width:26px; height:26px; border-radius:50%; border:none; background:transparent;
    display:flex; align-items:center; justify-content:center; cursor:pointer;
    transition:background 0.2s ease, transform 0.15s ease;
  }
  .sys-btn:hover { background:var(--icon-hover-bg); transform:scale(1.08); }
  .sys-btn svg { width:14px; height:14px; stroke:var(--primary); fill:none; stroke-width:1.6; stroke-linecap:round; stroke-linejoin:round; filter:drop-shadow(0 0 5px var(--glow)); }
  .sys-btn.settings-btn svg { width:16px; height:16px; }

  .clock-container {
    position:relative; display:flex; flex-direction:column; align-items:center;
    cursor:pointer; padding:4px 6px; border-radius:8px; transition:background 0.2s ease;
  }
  .clock-container:hover { background:var(--icon-hover-bg); }
  .clock { font-size:13px; font-weight:600; letter-spacing:0.3px; font-variant-numeric:tabular-nums; color:var(--primary); text-shadow:0 0 8px var(--glow); }
  .clock-glow-bar {
    position:absolute; bottom:-1px; width:0%; height:2px; background:var(--primary);
    border-radius:99px; box-shadow:0 0 8px var(--primary), 0 0 12px var(--glow);
    transition:width 0.25s cubic-bezier(0.22,1,0.36,1);
  }
  .clock-container:hover .clock-glow-bar { width:80%; }

  .flyout-panel {
    position:fixed; bottom:66px; right:12px; width:360px;
    background: var(--dock-bg);
    backdrop-filter: blur(30px) saturate(180%); -webkit-backdrop-filter: blur(30px) saturate(180%);
    border: 1px solid var(--dock-border); box-shadow: var(--dock-shadow);
    border-radius: 20px; padding: 20px; color: var(--primary);
    display: flex; flex-direction: column; gap: 16px;
    transform: translateY(16px) scale(0.98); opacity: 0; pointer-events: none;
    transition: transform 0.3s cubic-bezier(0.16,1,0.3,1), opacity 0.28s ease; z-index:1000;
  }
  .flyout-panel.open { transform: translateY(0) scale(1); opacity:1; pointer-events:auto; }

  .calendar-header {
    display:flex; justify-content:space-between; align-items:center; gap:8px;
    font-weight:600; font-size:15px; padding-bottom:8px; border-bottom:1px solid var(--divider);
  }
  .cal-title { flex:1; text-align:center; font-weight:700; font-size:16px; color:var(--primary); cursor:pointer; white-space:nowrap; overflow:hidden; text-overflow:ellipsis; }
  .cal-nav {
    flex:0 0 26px; width:26px; height:26px; border-radius:50%; border:none; background:transparent;
    color:var(--primary); font-size:16px; line-height:1; cursor:pointer;
    display:flex; align-items:center; justify-content:center; transition:background 0.2s ease;
  }
  .cal-nav:hover { background:var(--icon-hover-bg); }

  .calendar-grid { display:grid; grid-template-columns:repeat(7,1fr); gap:4px; text-align:center; font-size:12px; }
  .calendar-dow { color:var(--hint-color); font-weight:500; padding-bottom:4px; }
  .calendar-day {
    aspect-ratio:1; display:flex; align-items:center; justify-content:center;
    border-radius:50%; cursor:pointer; transition:background 0.15s ease; weight:500;
  }
  .calendar-day:hover { background:var(--icon-hover-bg); }
  .calendar-day.today { background:var(--primary); color:#09090b; font-weight:600; box-shadow:0 0 10px var(--glow); }
  .calendar-day.selected { box-shadow: 0 0 0 2px var(--primary); }
  .calendar-day.other-month { opacity:0.35; }

  .quick-settings {
)rawhtml"
  LR"rawhtml(    display:grid; grid-template-columns:repeat(2,1fr); gap:8px;
    padding-top:8px; border-top:1px solid var(--divider);
  }
  .qs-tile {
    background:var(--icon-hover-bg); border:1px solid var(--dock-border); border-radius:12px;
    padding:10px; display:flex; align-items:center; gap:10px; cursor:pointer;
    font-size:12px; font-weight:500; color:var(--primary);
    transition:transform 0.15s ease, background 0.2s ease;
  }
  .qs-tile:hover { transform:scale(1.02); background:var(--divider); }
  .qs-tile svg { width:16px; height:16px; stroke:var(--primary); fill:none; stroke-width:1.8; }
</style>
</head>
<body>

  <div class="bar">
    <div class="dock-wrapper">
      <div class="dock-capsule" id="dock">
        <div class="main-icons">
          <div class="apps-scroll-area" id="appsList"></div>
          <div class="divider"></div>
          <button class="sys-btn settings-btn" title="Настройки" onclick="post({type:'settings'})">
            <svg viewBox="0 0 24 24" width="16" height="16" stroke="currentColor" fill="none" stroke-width="1.8"><circle cx="12" cy="12" r="3"></circle><path d="M19.4 15a1.65 1.65 0 0 0 .33 1.82l.06.06a2 2 0 0 1-2.83 2.83l-.06-.06a1.65 1.65 0 0 0-1.82-.33 1.65 1.65 0 0 0-1 1.51V21a2 2 0 0 1-4 0v-.09A1.65 1.65 0 0 0 9 19.4a1.65 1.65 0 0 0-1.82.33l-.06.06a2 2 0 0 1-2.83-2.83l.06.06a1.65 1.65 0 0 0 .33-1.82 1.65 1.65 0 0 0-1.51-1H3a2 2 0 0 1 0-4h.09A1.65 1.65 0 0 0 4.6 9a1.65 1.65 0 0 0-.33-1.82l-.06-.06a2 2 0 0 1 2.83-2.83l.06.06a1.65 1.65 0 0 0 1.82.33H9a1.65 1.65 0 0 0 1-1.51V3a2 2 0 0 1 4 0v.09a1.65 1.65 0 0 0 1 1.51 1.65 1.65 0 0 0 1.82-.33l.06-.06a2 2 0 0 1 2.83 2.83l-.06.06a1.65 1.65 0 0 0 .33 1.82V9a1.65 1.65 0 0 0 1.51 1H21a2 2 0 0 1 0 4h-.09A1.65 1.65 0 0 0 19.4 15z"></path></svg>
          </button>
          <button class="sys-btn" title="Поиск Windows (Win+S)" onclick="post({type:'nativeSearch'})">
            <svg viewBox="0 0 24 24"><circle cx="10" cy="10" r="7"></circle><line x1="15" y1="15" x2="20.5" y2="20.5"></line></svg>
          </button>
        </div>
        <div class="system-zone">
          <div class="tray-icons legacy-tray" id="trayIcons"></div>
          <button class="sys-btn tray-btn legacy-tray" id="trayBtn" title="Показать скрытые значки" onclick="toggleTrayPanel()">
            <svg viewBox="0 0 24 24" width="14" height="14" stroke="currentColor" fill="none" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round">
              <path d="M6 9l6 6 6-6"/>
            </svg>
          </button>
          <div class="tray-slot" id="traySlot"></div>
          <div class="clock-container" id="clockContainer" onclick="toggleFlyout()" title="Календарь и время">
            <div class="clock" id="clock">00:00</div>
            <div class="clock-glow-bar"></div>
          </div>
        </div>
      </div>
    </div>
  </div>

  <div class="flyout-panel" id="flyoutPanel">
    <div class="calendar-header">
      <button class="cal-nav" id="calPrev" title="Предыдущий месяц"></button>
      <div class="cal-title" id="calendarHeader" title="Вернуться к текущему месяцу">Ноябрь 2026</div>
      <button class="cal-nav" id="calNext" title="Следующий месяц"></button>
    </div>
    <div class="calendar-grid">
      <div class="calendar-dow">Mo</div><div class="calendar-dow">Tu</div><div class="calendar-dow">We</div>
      <div class="calendar-dow">Th</div><div class="calendar-dow">Fr</div><div class="calendar-dow">Sa</div>
      <div class="calendar-dow">Su</div>
    </div>
    <div class="calendar-grid" id="calendarDays"></div>

    <div class="quick-settings">
      <div class="qs-tile" onclick="post({type:'settings'})">
        <svg viewBox="0 0 24 24"><circle cx="12" cy="12" r="3"></circle><path d="M19.4 15a1.65 1.65 0 0 0 .33 1.82l.06.06a2 2 0 0 1-2.83 2.83l-.06-.06a1.65 1.65 0 0 0-1.82-.33 1.65 1.65 0 0 0-1 1.51V21a2 2 0 0 1-4 0v-.09A1.65 1.65 0 0 0 9 19.4a1.65 1.65 0 0 0-1.82.33l-.06.06a2 2 0 0 1-2.83-2.83l.06-.06a1.65 1.65 0 0 0 .33-1.82 1.65 1.65 0 0 0-1.51-1H3a2 2 0 0 1 0-4h.09A1.65 1.65 0 0 0 4.6 9a1.65 1.65 0 0 0-.33-1.82l-.06-.06a2 2 0 0 1 2.83-2.83l.06.06a1.65 1.65 0 0 0 1.82.33H9a1.65 1.65 0 0 0 1-1.51V3a2 2 0 0 1 4 0v.09a1.65 1.65 0 0 0 1 1.51 1.65 1.65 0 0 0 1.82-.33l.06-.06a2 2 0 0 1 2.83 2.83l-.06.06a1.65 1.65 0 0 0 .33 1.82V9a1.65 1.65 0 0 0 1.51 1H21a2 2 0 0 1 0 4h-.09A1.65 1.65 0 0 0 19.4 15z"></path></svg>
        <span>Settings</span>
      </div>
      <div class="qs-tile" onclick="post({type:'settingsUrl', url:'ms-settings:network-wifi'})">
        <svg viewBox="0 0 24 24"><path d="M5 12.55a11 11 0 0 1 14.08 0"/><path d="M1.42 9a16 16 0 0 1 21.16 0"/><path d="M8.53 16.11a6 6 0 0 1 6.95 0"/><line x1="12" y1="20" x2="12.01" y2="20"/></svg>
        <span>Network</span>
      </div>
    </div>
  </div>

  <div class="tray-panel" id="trayPanel">
    <div class="tray-panel-title">Скрытые значки</div>
    <div class="tray-icons" id="trayHidden"></div>
  </div>

<script>
  const appsList = document.getElementById("appsList");
  const dock = document.getElementById("dock");
  const flyoutPanel = document.getElementById("flyoutPanel");
  const trayIcons = document.getElementById("trayIcons");
  const trayHidden = document.getElementById("trayHidden");
  const trayPanel = document.getElementById("trayPanel");
  const traySlot = document.getElementById("traySlot");
  const calDays = document.getElementById("calendarDays");
  const calTitle = document.getElementById("calendarHeader");
  const calPrev = document.getElementById("calPrev");
  const calNext = document.getElementById("calNext");
)rawhtml"
  LR"rawhtml(  if (calPrev) calPrev.textContent = "\u2039";
  if (calNext) calNext.textContent = "\u203A";
  let apps = [];
  let appsKey = "";
  let previewTimer = null;

  function post(obj){ if(window.chrome && window.chrome.webview) window.chrome.webview.postMessage(obj); }

  function clearPreview(){
    if(previewTimer){ clearTimeout(previewTimer); previewTimer = null; }
    post({ type:"preview", show:0 });
  }

  // ---------------- apps ----------------
  function renderApps(){
    if(!appsList) return;
    // Rebuilding the DOM every 2 s made hovered icons flicker and killed the
    // preview timer. Only rebuild when the SET of windows changes.
    const key = apps.map(a => String(a.hwnd)).join("|");
    if(key === appsKey){
      apps.forEach((a, i) => {
        const div = appsList.children[i];
        if(!div) return;
        div.dataset.title = a.title || "";
        div.setAttribute("aria-label", a.title || "Приложение");
        div.classList.toggle("active", !!a.active);
      });
      return;
    }

    appsKey = key;
    clearPreview();
    appsList.innerHTML = "";
    apps.forEach(a => {
      const div = document.createElement("div");
      div.className = "app-item" + (a.running?" running":"") + (a.active?" active":"");
      div.dataset.hwnd = String(a.hwnd);
      div.dataset.title = a.title || "";
      div.setAttribute("aria-label", a.title || "Приложение");
      const letter = (a.title && a.title[0]) ? a.title[0] : "?";
      if (a.icon) {
        const img = document.createElement("img");
        img.src = a.icon;
        img.onerror = () => { img.remove(); div.innerHTML = '<span class="letter">'+letter+'</span>'; };
        div.appendChild(img);
      } else {
        div.innerHTML = '<span class="letter">'+letter+'</span>';
      }
      div.addEventListener("mouseenter", () => {
        if(previewTimer) clearTimeout(previewTimer);
        previewTimer = setTimeout(() => {
          const r = div.getBoundingClientRect();
          post({ type:"preview", show:1, hwnd:String(a.hwnd), title:a.title || "",
                 x:Math.round(r.left + r.width / 2), y:Math.round(r.top),
                 w:Math.round(r.width), h:Math.round(r.height) });
          previewTimer = null;
        }, 450);
      });
      div.addEventListener("mouseleave", clearPreview);
      div.onclick = (e) => { e.stopPropagation(); clearPreview(); post({type:"activate", hwnd:a.hwnd}); };
      appsList.appendChild(div);
    });
  }

  dock.addEventListener("wheel", (e) => { e.preventDefault(); appsList.scrollLeft += e.deltaY * 0.8; }, { passive: false });

)rawhtml"
  LR"rawhtml(  // ---------------- flyout ----------------
  function flyoutRectCss(){
    const w = flyoutPanel.offsetWidth || 0;
    const h = flyoutPanel.offsetHeight || 0;
    return { x: Math.max(0, window.innerWidth - 12 - w), y: Math.max(0, window.innerHeight - 66 - h), w, h };
  }

  function activeRectCss(){
    if(activePanel === 'tray' && trayPanel){
      const r = trayPanel.getBoundingClientRect();
      return { x:r.x, y:r.y, w:r.width, h:r.height };
    }
    return flyoutRectCss();
  }
  function notifyFlyout(open){
    if(!(window.chrome && window.chrome.webview)) return;
    const r = open ? activeRectCss() : {x:0,y:0,w:0,h:0};
    post({ type:"flyout", open: open ? 1 : 0,
           x: Math.round(r.x), y: Math.round(r.y),
           w: Math.round(r.w), h: Math.round(r.h) });
  }

  let flyoutCloseTimer = null;
  let activePanel = null;          // 'calendar' | 'tray'
  let trayApps = [];
  // Native notification area: the Win32 host is positioned over this box.
  let nativeTray = false;
  let modeAnimating = false;
  function currentPanel(){
    if(activePanel === 'tray') return trayPanel;
    return flyoutPanel;
  }

  let hwndExpandedForPanel = false;
  function openPanel(which){
    if(activePanel && activePanel !== which){
      const old = activePanel === 'tray' ? trayPanel : flyoutPanel;
      if(old) old.classList.remove('open');
    }
    activePanel = which;
    if(flyoutCloseTimer){ clearTimeout(flyoutCloseTimer); flyoutCloseTimer = null; }
    if(which === 'calendar'){ renderCalendar(); updateClock(); }
    // The carrier HWND never changes size. The first open only registers the
    // clickable rect; subsequent opens can reveal immediately.
    if(hwndExpandedForPanel) window.__flyReady();
    else { hwndExpandedForPanel = true; notifyFlyout(true); }
  }
  function closePanel(){
    if(!activePanel) return;
    currentPanel().classList.remove("open");
    flyoutCloseTimer = setTimeout(() => {
      flyoutCloseTimer = null;
      hwndExpandedForPanel = false;
      notifyFlyout(false);
      activePanel = null;
    }, 300);
  }
  function setFlyout(open){ if(open) openPanel('calendar'); else closePanel(); }
  function toggleFlyout(){ if(activePanel === 'calendar') closePanel(); else openPanel('calendar'); }

  function toggleTrayPanel(){
    if(activePanel === 'tray'){ closePanel(); return; }
    renderTrayIcons();
    openPanel('tray');
  }
)rawhtml"
  LR"rawhtml(  function makeTrayItem(a){
    const d = document.createElement("div");
    d.className = "tray-item";
    d.title = a.title || "";
    if(a.icon && a.icon.length > 40 && a.icon.indexOf("data:image/png;base64,") === 0){
      const img = document.createElement("img");
      img.src = a.icon;
      img.alt = "";
      img.onerror = () => { img.remove(); };
      d.appendChild(img);
    } else {
      const g = document.createElement("div");
      g.className = "glyph";
      g.textContent = (a.title || "?").trim().charAt(0).toUpperCase();
      d.appendChild(g);
    }
    const hit = (button) => {
      post({ type:"trayClick", hwnd:String(a.hwnd), uid:Number(a.uid),
             tb:String(a.tb || 0), cx:Number(a.cx || 0), cy:Number(a.cy || 0),
             activate:Number(a.activate || 0), launch:String(a.launch || ""),
             cb:Number(a.cb || 0), exe:String(a.exe || ""),
              title:String(a.title || ""), button:button });
      // Deliberately keep the tray popup open after clicking an icon.
    };
    d.addEventListener("click", () => hit(1));
    d.addEventListener("contextmenu", (e) => { e.preventDefault(); hit(2); });
    return d;
  }
  function renderTrayIcons(){
    if(trayIcons) trayIcons.innerHTML = "";
    if(trayHidden) trayHidden.innerHTML = "";
    if(traySlot) traySlot.innerHTML = "";
    trayApps.forEach((a, i) => {
      // A few in the capsule island; the popup always shows the full list.
      if(i < 6 && trayIcons) trayIcons.appendChild(makeTrayItem(a));
      if(i < 4 && traySlot) traySlot.appendChild(makeTrayItem(a));
      if(trayHidden) trayHidden.appendChild(makeTrayItem(a));
    });
  }

  // Called by C++ when the fixed carrier is ready to reveal a panel.
  window.__flyReady = function(){
    notifyFlyout(true);  // refresh the clickable rect
    postCapsule();
    requestAnimationFrame(() => requestAnimationFrame(() => {
      const el = currentPanel();
      if(el) el.classList.add("open");
    }));
  };

  function postCapsule(){
    const d = document.getElementById("dock");
    if (d) { const r = d.getBoundingClientRect(); post({type:"capsule", x:r.x, y:r.y, w:r.width, h:r.height}); }
    postTraySlot();
  }

  // The native tray island is anchored to the right edge of this placeholder.
  // Report its rect from a frame loop: icons loading and CSS mode changes move
  // the slot without any other event, which left the island at a stale spot.
  let lastSlotKey = "";
  function postTraySlot(force){
    if(!nativeTray || !traySlot) return;
    const r = traySlot.getBoundingClientRect();
    const on = (modeAnimating || r.width < 1 || r.height < 1) ? 0 : 1;
    const x = Math.round(r.x), y = Math.round(r.y);
    const w = Math.round(r.width), h = Math.round(r.height);
    const key = on + ":" + x + ":" + y + ":" + w + ":" + h;
    if(!force && key === lastSlotKey) return;
    lastSlotKey = key;
    post({ type:"traySlot", on, x, y, w, h });
  }
  let lastSlotFrame = -1000;
  function watchTraySlot(ts){
    if(ts - lastSlotFrame >= 80){ lastSlotFrame = ts; postTraySlot(); }
    requestAnimationFrame(watchTraySlot);
  }
  requestAnimationFrame(watchTraySlot);

  // The capsule moves during the CSS mode transition. Re-publish its rect from
  // an animation-frame loop so the Win32 hit-test never keeps a stale rectangle
  // and leaves a dead strip over the desktop.
  let lastCapsuleKey = "";
  let lastCapsuleFrame = -1000;
  function watchCapsule(ts){
    if(ts - lastCapsuleFrame >= 80){
      lastCapsuleFrame = ts;
      const d = document.getElementById("dock");
      if(d){
        const r = d.getBoundingClientRect();
        const x = Math.round(r.x), y = Math.round(r.y);
        const w = Math.round(r.width), h = Math.round(r.height);
        const key = x+":"+y+":"+w+":"+h;
        if(key !== lastCapsuleKey){
          lastCapsuleKey = key;
          post({type:"capsule", x:x, y:y, w:w, h:h});
        }
      }
    }
    requestAnimationFrame(watchCapsule);
  }
  requestAnimationFrame(watchCapsule);

  // Direct entry point used by C++ via ICoreWebView2::ExecuteScriptAsync —
  // more reliable than a web message when the page has just been navigated.
  window.__flySetMode = function(on){
    const body = document.body; if (!body) return;
    if (body.classList.contains("expanded") === !!on) return;
    // The native island cannot follow the CSS animation frame by frame, so it
    // hides for the duration of the transition and lands in one final jump.
    modeAnimating = true;
    postTraySlot(true);
    // Let WebView rebuild its layout after the HWND change before animating.
    requestAnimationFrame(() => requestAnimationFrame(() => {
      body.classList.toggle("expanded", !!on);
      setTimeout(postCapsule, 420);
      setTimeout(() => { modeAnimating = false; postTraySlot(true); }, 470);
    }));
  };

  // C++ only sends a mode message at state boundaries; CSS does the animation.
  if (window.chrome && window.chrome.webview) {
    window.chrome.webview.addEventListener("message", (e) => {
      const raw = e.data;
      let m = raw;
      if (typeof raw === "string") { try { m = JSON.parse(raw); } catch (err) { return; } }
      if (m && m.type === "mode") {
        window.__flySetMode(m.expanded === 1 || m.expanded === "1");
      }
    });
  }
  const dockEl = document.getElementById("dock");
  if (dockEl) dockEl.addEventListener("transitionend", (e) => {
    postCapsule();
    if (e.propertyName === "width" || e.propertyName === "max-width") post({type:"modeDone"});
  });


  // The calendar is special: ONLY another click on the clock closes it.
  // Losing window focus or clicking elsewhere on the desktop must not close it.
  window.addEventListener("blur", () => {
    if (activePanel && activePanel !== 'calendar') closePanel();
  });

  // Never let a click inside any panel bubble into the outside-click logic.
  [flyoutPanel, trayPanel].forEach((p) => {
    if (p) p.addEventListener("click", (e) => e.stopPropagation());
  });

  window.addEventListener("click", (e) => {
    if (!activePanel) return;

    // Calendar and tray: close ONLY on a click on the empty page background.
    // Clicks on the panels themselves are stopped by their own listeners.
    if (activePanel === 'calendar' || activePanel === 'tray') {
      const tag = e.target && e.target.tagName;
      if (tag === 'BODY' || tag === 'HTML') closePanel();
      return;
    }

    const panel = currentPanel();
    const inPanel = panel && panel.contains(e.target);
    const inTrigger = e.target.closest &&
      (e.target.closest(".tray-btn") || e.target.closest(".sys-btn") ||
       e.target.closest(".clock-container"));
    if (!inPanel && !inTrigger) closePanel();
  });

  // ---------------- calendar ----------------
  let viewYear = 0;
  let viewMonth = 0;
  let selected = null; // {y, m, d}

  function sameDate(a, b){ return !!a && !!b && a.y === b.y && a.m === b.m && a.d === b.d; }
  function isToday(cell){ const n = new Date(); return cell.y === n.getFullYear() && cell.m === n.getMonth() && cell.d === n.getDate(); }
)rawhtml"
  LR"rawhtml(  function cellDate(cell){ const dt = new Date(viewYear, viewMonth + cell.offset, cell.day); return { y: dt.getFullYear(), m: dt.getMonth(), d: dt.getDate() }; }

  function renderCalendar(){
    if(!calDays || !calTitle) return;
    const now = new Date();
    if(!viewYear){ viewYear = now.getFullYear(); viewMonth = now.getMonth(); }

    calTitle.textContent = new Date(viewYear, viewMonth, 1).toLocaleDateString("ru-RU", { month:"long", year:"numeric" });

    const firstWeekday = (new Date(viewYear, viewMonth, 1).getDay() + 6) % 7; // Monday = 0
    const daysInMonth = new Date(viewYear, viewMonth + 1, 0).getDate();
    const daysInPrevMonth = new Date(viewYear, viewMonth, 0).getDate();

    const cells = [];
    for(let i = firstWeekday - 1; i >= 0; i--) cells.push({ day: daysInPrevMonth - i, offset: -1 });
    for(let d = 1; d <= daysInMonth; d++) cells.push({ day: d, offset: 0 });
    const totalCells = Math.ceil(cells.length / 7) * 7;
    let nextDay = 1;
    while(cells.length < totalCells) cells.push({ day: nextDay++, offset: 1 });

    calDays.innerHTML = "";
    cells.forEach(cell => {
      const el = document.createElement("div");
      el.className = "calendar-day";
      el.textContent = cell.day;
      const date = cellDate(cell);
      if(cell.offset !== 0) el.classList.add("other-month");
      if(isToday(date)) el.classList.add("today");
      if(sameDate(selected, date)) el.classList.add("selected");
      el.addEventListener("click", () => {
        selected = date;
        if(date.y !== viewYear || date.m !== viewMonth){ viewYear = date.y; viewMonth = date.m; }
        renderCalendar();
      });
      calDays.appendChild(el);
    });
  }

  function shiftMonth(delta){
    const dt = new Date(viewYear, viewMonth + delta, 1);
    viewYear = dt.getFullYear(); viewMonth = dt.getMonth();
    renderCalendar();
  }
  function goToday(){
    const now = new Date();
    viewYear = now.getFullYear(); viewMonth = now.getMonth();
    selected = { y: viewYear, m: viewMonth, d: now.getDate() };
    renderCalendar();
  }
  if(calPrev) calPrev.addEventListener("click", (e) => { e.stopPropagation(); shiftMonth(-1); });
  if(calNext) calNext.addEventListener("click", (e) => { e.stopPropagation(); shiftMonth(1); });
  if(calTitle) calTitle.addEventListener("click", (e) => { e.stopPropagation(); goToday(); });

  // ---------------- clock ----------------
  function updateClock(){
    const now = new Date();
    const hh = String(now.getHours()).padStart(2,"0");
    const mm = String(now.getMinutes()).padStart(2,"0");
    if(document.getElementById("clock")) document.getElementById("clock").textContent = hh + ":" + mm;
  }
  function scheduleMidnightRefresh(){
    const now = new Date();
    const next = new Date(now.getFullYear(), now.getMonth(), now.getDate() + 1, 0, 0, 0, 0);
    let ms = next.getTime() - now.getTime();
    if(ms < 1000) ms = 1000;
    setTimeout(() => { renderCalendar(); updateClock(); scheduleMidnightRefresh(); }, ms);
  }

  // ---------------- messages ----------------
  if(window.chrome && window.chrome.webview){
    window.chrome.webview.addEventListener("message", (e) => {
      let data = e.data;
      if(typeof data === "string"){ try { data = JSON.parse(data); } catch(err){ return; } }
      if(data && data.type === "apps"){
        apps = (data.apps || []).map(a => ({ hwnd:a.hwnd, title:a.title, icon:a.icon,
                                             running:true, active:!!a.active }));
        renderApps();
      } else if(data && data.type === "trayIcons"){
        trayApps = data.apps || [];
        renderTrayIcons();
      } else if(data && data.type === "trayMode"){
        nativeTray = !!(data.native === 1 || data.native === "1" || data.native === true);
        // Once the native tray mode is announced, the legacy CSS tray stays
        // hidden even if embedding falls back (native:0).
        document.body.classList.add("native-tray");
        post({ type:"trayModeAck", native: nativeTray ? 1 : 0 });
        lastSlotKey = "";
        requestAnimationFrame(() => requestAnimationFrame(() => postTraySlot(true)));
      } else if(data && data.type === "traySlotWidth"){
        const w = Number(data.w) || 0;
        if(w > 0 && traySlot){
          traySlot.style.width = w + "px";
          requestAnimationFrame(() => postTraySlot(true));
        }
      } else if(data && data.type === "closeFlyout"){
        if(activePanel === 'calendar') closePanel();
      }
    });
  }

  renderApps();
  postCapsule();
  window.addEventListener("resize", postCapsule);
  updateClock();
  setInterval(updateClock, 1000);
  scheduleMidnightRefresh();
</script>
</body>
</html>)rawhtml";
