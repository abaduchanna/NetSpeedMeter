// NetSpeedMonitor - taskbar-embedded network speed display for Windows 10/11
//
// Embedding technique (reading TrafficMonitor by zhongyang219):
//   create a WS_EX_TOOLWINDOW popup window, then SetParent() it into the
//   taskbar (Shell_TrayWnd) and park it just left of TrayNotifyWnd.
//   This is a normal Win32 API usage - no shell DLL / desk band needed,
//   which is exactly why it keeps working on Windows 11 where taskbar
//   toolbars (the old NetSpeedMonitor approach) were removed.
//
// v2.0.1 hardening (fixes "running but invisible" reports):
//   1. Tray icon is ALWAYS added (Shell_NotifyIconW) - the app can always be
//      found in the tray (hidden icons) and exited from there.
//   2. TrafficMonitor-exact show order: SetParent -> ShowWindow -> position,
//      plus a per-second HWND_TOP re-assert so the widget cannot stay buried
//      under the taskbar's rendered (XAML) surface on any Win11 build.
//   3. Floating mode toggle (right-click menu): if the embed is still not
//      visible on some machine, the bar can run topmost just above the
//      taskbar instead. Choice is persisted in HKCU\Software\NetSpeedMonitor.
//   4. Second-instance dialog now explains where to look and offers to open
//      Task Manager so an older stuck instance can be ended in one click.
//   5. Diagnostic log (%LOCALAPPDATA%\NetSpeedMonitor\log.txt) records the
//      taskbar handle, SetParent result, position and visibility state so
//      "invisible" reports can be diagnosed from the log alone.
//
// v2.0.2 hardening (fixes "floating mode enabled but STILL nothing shows"):
//   6. ROOT CAUSE: WS_EX_TOPMOST set through SetWindowLongPtr() does NOT
//      re-band a window into the topmost layer - only SetWindowPos() with
//      HWND_TOPMOST does. The v2.0.1 floating bar therefore sat BEHIND every
//      normal window. Fix: per-second SetWindowPos(HWND_TOPMOST, x, y, w, h)
//      which positions AND re-bands in one call.
//   7. A fresh start in floating mode never resolved the taskbar handle,
//      so GetWindowRect(NULL) failed every tick and the bar stayed parked
//      offscreen (-32000,-32000) forever. Fix: floating mode now resolves
//      Shell_TrayWnd itself and falls back to SPI_GETWORKAREA when the
//      taskbar rect cannot be measured.
//   8. New menu item "Copy diagnostics" puts version/mode/rects/visibility
//      plus the log tail on the clipboard for one-paste bug reports.
//   9. 60-second heartbeat line in log.txt with the full window state.
//
// v2.3.0 changes (owner request 2026-10-17: "KB/s+MB/s nikal aur alag se
// Kb/s aur Mb/s dal, b small hai capital nahi"):
//  15. SEPARATE LINES mode replaces the v2.2.0 mashed "Both" string — the
//      widget now draws TWO lines: Mb/s on top, Kb/s below (each with its
//      own up/down arrows, colored-speeds aware). The single-line
//      "1582.4 KB/s (1.54 MB/s)" format is gone completely.
//  16. SMALL b everywhere: B/s -> b/s, KB/s -> Kb/s, MB/s -> Mb/s,
//      GB/s -> Gb/s (menu, formats, tooltip, About).
//  17. Saved "both" (and "auto") migrate to the new separate-lines mode;
//      the value is stored as "sep" from now on. Window height doubles
//      for the two-line layout and is clamped to the taskbar height.
//
// v2.2.0 features/fixes (owner requests 2026-10-16):
//  13. (REPLACED by v2.3.0) was: mashed "Both (KB/s + MB/s)" single line.
//  14. THEME toggle fix - the Auto theme sampler passed SCREEN coordinates
//      to the taskbar CLIENT-area DC (GetPixel went out of bounds and
//      returned CLR_INVALID on every call, so Auto never matched the real
//      taskbar color). Sampling now uses proper client coordinates with a
//      composited-screen fallback, and Dark/Light switches repaint
//      immediately instead of waiting for the next timer tick.
//
// v2.1.0 features (owner requests):
//  10. UNITS menu: Auto (B/s .. GB/s scaling), KB/s, MB/s - persisted.
//  11. THEME menu: Auto (matches taskbar via pixel sampling), Dark, Light
//      - persisted; floating bar gets a proper themed background too.
//  12. COLORED SPEEDS: upload drawn RED (top arrow), download drawn GREEN
//      (bottom arrow) - the universal convention. Toggleable; arrow colors
//      auto-pick bright/dark variants from the background luminance.
//
// v2.1.0 features (owner requests):
//  10. UNITS menu: Auto (B/s .. GB/s scaling), KB/s, MB/s - persisted.
//  11. THEME menu: Auto (matches taskbar via pixel sampling), Dark, Light
//      - persisted; floating bar gets a proper themed background too.
//  12. COLORED SPEEDS: upload drawn RED (top arrow), download drawn GREEN
//      (bottom arrow) - the universal convention. Toggleable; arrow colors
//      auto-pick bright/dark variants from the background luminance.
//
// Build (MSVC):  cl /O2 /W3 /DUNICODE /D_UNICODE main.c icon.res ^
//                 /Fe:NetSpeedMonitor.exe /link /SUBSYSTEM:WINDOWS ^
//                 user32.lib gdi32.lib shell32.lib advapi32.lib iphlpapi.lib
// Build (MinGW): x86_64-w64-mingw32-gcc -O2 -mwindows -DUNICODE -D_UNICODE ^
//                 main.c icon.o -o NetSpeedMonitor.exe -liphlpapi -lgdi32 ^
//                 -lshell32 -ladvapi32 -s -static

#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0601
#include <windows.h>
#include <iphlpapi.h>
#include <shellapi.h>
#include <wchar.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define APP_NAME     L"NetSpeedMonitor"
#define APP_VERSION  L"2.3.0"
#define MUTEX_NAME   L"NetSpeedMonitor_Taskbar_SingleInstance"
#define CLASS_NAME   L"NetSpeedMonitorTaskbarWnd"
#define TIMER_TICK   1          // 1000 ms update + reposition timer
#define WM_APP_TRAY  (WM_APP + 1)
#define IDM_AUTOSTART 20001
#define IDM_TASKMGR    20002
#define IDM_ABOUT      20003
#define IDM_EXIT       20004
#define IDM_FLOAT      20005
#define IDM_DIAG       20006
#define IDM_UNITS_AUTO 20010
#define IDM_UNITS_KB   20011
#define IDM_UNITS_MB   20012
#define IDM_UNITS_BOTH 20013
#define IDM_THEME_AUTO 20020
#define IDM_THEME_DARK 20021
#define IDM_THEME_LITE 20022
#define IDM_COLOR      20023

typedef struct {
    double down;   // bytes per second
    double up;     // bytes per second
} SPEEDS;

static HWND   g_hwnd;
static HWND   g_hTaskbar;      // Shell_TrayWnd
static HFONT  g_font;
static UINT   g_dpi            = 96;
static int    g_winW           = 0;
static int    g_winH           = 0;
static BOOL   g_embedded       = FALSE;  // SetParent succeeded
static BOOL   g_floatMode      = FALSE;  // user-forced floating bar mode
static int    g_fallbackTries  = 0;
static COLORREF g_bgColor      = RGB(32, 32, 32);
static COLORREF g_textColor    = RGB(240, 240, 240);
static COLORREF g_upColor      = RGB(255, 112, 100);   // upload RED
static COLORREF g_downColor    = RGB(110, 231, 133);   // download GREEN
static WCHAR  g_text[160]      = L"\x2191 0 b/s  \x2193 0 b/s";
static WCHAR  g_upText[80]     = L"\x2191 0 b/s";
static WCHAR  g_downText[80]   = L"\x2193 0 b/s";
static WCHAR  g_lineMb[80]     = L"\x2191 0.00 Mb/s  \x2193 0.00 Mb/s";
static WCHAR  g_lineKb[80]     = L"\x2191 0.0 Kb/s  \x2193 0.0 Kb/s";
static WCHAR  g_lineUpMb[40]   = L"\x2191 0.00 Mb/s";
static WCHAR  g_lineDnMb[40]   = L"\x2193 0.00 Mb/s";
static WCHAR  g_lineUpKb[40]   = L"\x2191 0.0 Kb/s";
static WCHAR  g_lineDnKb[40]   = L"\x2193 0.0 Kb/s";
static int    g_units          = 0;   // 0=auto  1=Kb/s  2=Mb/s  3=separate lines
static int    g_theme          = 0;   // 0=auto  1=dark  2=light
static BOOL   g_colorSpeeds    = TRUE; // up red / down green
static int    g_colorTick      = 0;
static int    g_trayTick       = 0;
static ULONG64 g_lastTick      = 0;
static int    g_hbTick         = 0;
static BOOL   g_trayAdded      = FALSE;
static NOTIFYICONDATAW g_nid;
static HANDLE g_logGuard       = NULL;

// ---------------------------------------------------------------- log
static void LogLine(const WCHAR* line)
{
    WCHAR dir[MAX_PATH * 2];
    DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", dir, MAX_PATH * 2);
    if (n <= 0 || n >= MAX_PATH) return;
    wcscat_s(dir, MAX_PATH * 2, L"\\NetSpeedMonitor");
    CreateDirectoryW(dir, NULL);
    wcscat_s(dir, MAX_PATH * 2, L"\\log.txt");

    // keep the log small: restart it at 64 KB
    WIN32_FILE_ATTRIBUTE_DATA fa;
    if (GetFileAttributesExW(dir, GetFileExInfoStandard, &fa) &&
        fa.nFileSizeHigh == 0 && fa.nFileSizeLow > 64 * 1024)
        DeleteFileW(dir);

    FILE* f = _wfopen(dir, L"a, ccs=UTF-8");
    if (!f) return;
    ULONGLONG t = GetTickCount64();
    fwprintf(f, L"[%llu.%03llu] %s\n", t / 1000, t % 1000, line);
    fclose(f);
}

// ---------------------------------------------------------------- utils
static int DpiVal(int px) { return px * (int)g_dpi / 96; }

static HMODULE User32(void)
{
    static HMODULE h = NULL;
    if (!h) h = GetModuleHandleW(L"user32.dll");
    return h;
}

typedef BOOL (WINAPI *FN_SetProcessDpiAwarenessCtx)(HANDLE);
typedef UINT (WINAPI *FN_GetDpiForWindow)(HWND);

static void EnablePerMonitorDpi(void)
{
    FN_SetProcessDpiAwarenessCtx f =
        (FN_SetProcessDpiAwarenessCtx)(VOID*)GetProcAddress(
            User32(), "SetProcessDpiAwarenessContext");
    if (f) f((HANDLE)-4);   // DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
}

static UINT DpiOfTaskbar(HWND taskbar)
{
    FN_GetDpiForWindow f =
        (FN_GetDpiForWindow)(VOID*)GetProcAddress(User32(), "GetDpiForWindow");
    if (f && taskbar) {
        UINT d = f(taskbar);
        if (d >= 96 && d <= 480) return d;
    }
    HDC dc = GetDC(NULL);
    UINT d = dc ? (UINT)GetDeviceCaps(dc, LOGPIXELSY) : 96;
    if (dc) ReleaseDC(NULL, dc);
    return d ? d : 96;
}

// ---------------------------------------------------------------- speed
// g_units: 0 = auto-scale (b/s .. Gb/s), 1 = always Kb/s, 2 = always Mb/s,
//          3 = SEPARATE lines (owner rule 2026-10-17: the old mashed
//          "1582.4 KB/s (1.54 MB/s)" string is REMOVED; Kb/s and Mb/s are
//          shown as two separate lines instead). Units use a SMALL b
//          everywhere (Kb/s, Mb/s) — owner: "b small hai capital nahi".
static void FormatRate(double bps, WCHAR* out, size_t cch)
{
    if (bps < 0) bps = 0;
    if (g_units == 1) {
        swprintf_s(out, cch, L"%.1f Kb/s", bps / 1024.0);
        return;
    }
    if (g_units == 2) {
        swprintf_s(out, cch, L"%.2f Mb/s", bps / (1024.0 * 1024.0));
        return;
    }
    if (bps >= 1024.0 * 1024.0 * 1024.0)
        swprintf_s(out, cch, L"%.2f Gb/s", bps / (1024.0 * 1024.0 * 1024.0));
    else if (bps >= 1024.0 * 1024.0)
        swprintf_s(out, cch, L"%.1f Mb/s", bps / (1024.0 * 1024.0));
    else if (bps >= 1024.0)
        swprintf_s(out, cch, L"%.1f Kb/s", bps / 1024.0);
    else
        swprintf_s(out, cch, L"%.0f b/s", bps);
}

// Sum per-interface octet deltas over all connected non-loopback adapters.
// Uses the classic GetIfTable (32-bit counters) with explicit wrap handling,
// which avoids the netioapi.h / winsock2.h header dependency maze entirely.
typedef struct { DWORD idx; ULONG64 in; ULONG64 out; int seen; } IFSTATE;
static IFSTATE g_if[64];

static ULONG64 WrapDiff(ULONG64 now, ULONG64 prev)
{
    if (now >= prev) return now - prev;
    return 0x100000000ULL - prev + now;      // 32-bit counter wrapped
}

static void AccumulateDeltas(double* down, double* up)
{
    DWORD size = 0;
    if (GetIfTable(NULL, &size, FALSE) != ERROR_INSUFFICIENT_BUFFER || !size)
        return;
    PMIB_IFTABLE t = (PMIB_IFTABLE)malloc(size);
    if (!t) return;
    if (GetIfTable(t, &size, FALSE) == NO_ERROR) {
        DWORD i;
        for (i = 0; i < t->dwNumEntries; ++i) {
            PMIB_IFROW r = &t->table[i];
            if (r->dwOperStatus != MIB_IF_OPER_STATUS_OPERATIONAL) continue;
            if (r->dwType == IF_TYPE_SOFTWARE_LOOPBACK) continue;

            IFSTATE* st = NULL;
            int j;
            for (j = 0; j < 64; ++j) {
                if (g_if[j].seen && g_if[j].idx == r->dwIndex) { st = &g_if[j]; break; }
                if (!st && !g_if[j].seen) st = &g_if[j];
            }
            if (!st) continue;
            ULONG64 in = r->dwInOctets, out = r->dwOutOctets;
            if (st->seen) {
                *down += (double)WrapDiff(in,  st->in);
                *up   += (double)WrapDiff(out, st->out);
            }
            st->idx = r->dwIndex; st->in = in; st->out = out; st->seen = 1;
        }
    }
    free(t);
}

static void UpdateSpeedText(void)
{
    double down = 0, up = 0;
    AccumulateDeltas(&down, &up);
    ULONGLONG now = GetTickCount64();

    if (g_lastTick && now > g_lastTick) {
        double dt = (double)(now - g_lastTick) / 1000.0;
        down /= dt;
        up   /= dt;
        if (down < 0 || down > 1e12) down = 0;
        if (up   < 0 || up   > 1e12) up   = 0;
        if (g_units == 3) {
            // SEPARATE lines mode: one line Mb/s, one line Kb/s (owner rule)
            swprintf_s(g_lineUpMb, 40, L"\x2191 %.2f Mb/s", up   / (1024.0 * 1024.0));
            swprintf_s(g_lineDnMb, 40, L"\x2193 %.2f Mb/s", down / (1024.0 * 1024.0));
            swprintf_s(g_lineMb,   80, L"%s  %s", g_lineUpMb, g_lineDnMb);
            swprintf_s(g_lineUpKb, 40, L"\x2191 %.1f Kb/s", up   / 1024.0);
            swprintf_s(g_lineDnKb, 40, L"\x2193 %.1f Kb/s", down / 1024.0);
            swprintf_s(g_lineKb,   80, L"%s  %s", g_lineUpKb, g_lineDnKb);
            swprintf_s(g_text,    160, L"%s | %s", g_lineMb, g_lineKb);
        } else {
            WCHAR d[40], u[40];
            FormatRate(down, d, 40);
            FormatRate(up,   u, 40);
            swprintf_s(g_upText,   80, L"\x2191 %s", u);
            swprintf_s(g_downText, 80, L"\x2193 %s", d);
            swprintf_s(g_text,    160, L"%s  %s", g_upText, g_downText);
        }
    }
    g_lastTick = now;
}

// ------------------------------------------------------------- taskbar
// Sample the taskbar's rendered background so our solid fill is seamless
// (theme = Auto only). Sets the background color; text/arrow colors are
// derived from background luminance in DeriveArrowColors().
// v2.2.0: coordinates are CLIENT-relative (the v2.1.x code passed screen
// coordinates into the client DC, so GetPixel always failed and Auto kept
// the startup color forever). Falls back to sampling the composited screen
// when the taskbar DC returns nothing (XAML-composited taskbars).
static void SampleTaskbarColor(void)
{
    RECT rb;
    if (!g_hTaskbar || !GetWindowRect(g_hTaskbar, &rb)) return;
    {
        int sx = 0, sy = 2;
        COLORREF c = CLR_INVALID;
        HWND hN = FindWindowExW(g_hTaskbar, NULL, L"TrayNotifyWnd", NULL);
        if (hN) {
            RECT rn;
            POINT tl;
            if (GetWindowRect(hN, &rn)) {
                tl.x = rn.left; tl.y = rn.top;
                ScreenToClient(g_hTaskbar, &tl);
                sx = tl.x - DpiVal(30);
                if (sx < 0) sx = 0;
            }
        } else {
            RECT rc;
            if (GetClientRect(g_hTaskbar, &rc)) sx = rc.right - DpiVal(40);
        }
        {
            HDC dc = GetDC(g_hTaskbar);
            if (dc) {
                c = GetPixel(dc, sx, sy);
                ReleaseDC(g_hTaskbar, dc);
            }
        }
        if (c == CLR_INVALID) {
            HDC sdc = GetDC(NULL);
            if (sdc) {
                c = GetPixel(sdc, rb.right - DpiVal(120), rb.top + DpiVal(8));
                ReleaseDC(NULL, sdc);
            }
        }
        if (c != CLR_INVALID) g_bgColor = c;
    }
}

// Text + arrow colors from the background luminance. Dark backgrounds get
// bright arrows, light backgrounds get dark arrows (always readable).
static void DeriveArrowColors(void)
{
    int r = GetRValue(g_bgColor), g = GetGValue(g_bgColor), b = GetBValue(g_bgColor);
    int lum = (r * 299 + g * 587 + b * 114) / 1000;
    if (lum > 128) {
        g_textColor = RGB(25, 25, 25);
        g_upColor   = RGB(191, 34, 34);     // dark red on light bg
        g_downColor = RGB(22, 101, 52);     // dark green on light bg
    } else {
        g_textColor = RGB(235, 235, 235);
        g_upColor   = RGB(255, 112, 100);   // bright red on dark bg
        g_downColor = RGB(110, 231, 133);   // bright green on dark bg
    }
}

// Apply the selected theme (g_theme: 0=auto 1=dark 2=light).
static void ApplyTheme(void)
{
    if (g_theme == 1)      g_bgColor = RGB(24, 24, 28);
    else if (g_theme == 2) g_bgColor = RGB(243, 243, 243);
    else                   SampleTaskbarColor();
    DeriveArrowColors();
}

static void DestroyFont(void)
{
    if (g_font) { DeleteObject(g_font); g_font = NULL; }
}

static void CreateFontForDpi(void)
{
    DestroyFont();
    int hpt = -MulDiv(9, (int)g_dpi, 72);          // Segoe UI 9 pt
    g_font = CreateFontW(hpt, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                         DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                         CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                         DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    // measure widest realistic text to size the window
    HDC dc = GetDC(g_hwnd);
    if (dc) {
        HGDIOBJ old = SelectObject(dc, g_font);
        SIZE sz;
        WCHAR sample[160];
        if (g_units == 3)
            // per-line sample: Kb/s line is the wider of the two; the window
            // is TALL enough for BOTH lines (v2.3.0 separate-lines mode)
            swprintf_s(sample, 160,
                L"\x2191 8888.8 Kb/s  \x2193 8888.8 Kb/s");
        else
            swprintf_s(sample, 160, L"\x2191 888.88 Mb/s  \x2193 888.88 Mb/s");
        if (GetTextExtentPoint32W(dc, sample, (int)wcslen(sample), &sz)) {
            g_winW = sz.cx + DpiVal(14);
            if (g_units == 3)
                g_winH = sz.cy * 2 + DpiVal(6);      // two stacked lines
            else
                g_winH = sz.cy + DpiVal(4);
            if (g_winH < DpiVal(20)) g_winH = DpiVal(20);
        }
        SelectObject(dc, old);
        ReleaseDC(g_hwnd, dc);
    }
    if (g_winW <= 0) { g_winW = DpiVal(210); g_winH = DpiVal(24); }
}

// ------------------------------------------------------------- tray icon
static void TrayAdd(void)
{
    if (g_trayAdded || !g_hwnd) return;
    ZeroMemory(&g_nid, sizeof(g_nid));
    g_nid.cbSize           = sizeof(g_nid);
    g_nid.hWnd             = g_hwnd;
    g_nid.uID              = 1;
    g_nid.uFlags           = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    g_nid.uCallbackMessage = WM_APP_TRAY;
    g_nid.hIcon            = LoadIconW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(1));
    lstrcpynW(g_nid.szTip, APP_NAME L" - loading...", 128);
    g_trayAdded = Shell_NotifyIconW(NIM_ADD, &g_nid) ? TRUE : FALSE;
    if (g_trayAdded) {
        WCHAR b[80];
        swprintf_s(b, 80, L"tray icon added");
        LogLine(b);
    }
}

static void TrayUpdateTip(void)
{
    if (!g_trayAdded) return;
    WCHAR tip[128];
    swprintf_s(tip, 128, L"NetSpeedMonitor\n%s", g_text);
    lstrcpynW(g_nid.szTip, tip, 128);
    Shell_NotifyIconW(NIM_MODIFY, &g_nid);
}

static void TrayRemove(void)
{
    if (g_trayAdded) {
        Shell_NotifyIconW(NIM_DELETE, &g_nid);
        g_trayAdded = FALSE;
    }
}

// ------------------------------------------------------------- settings
static void LoadSettings(void)
{
    HKEY k;
    g_floatMode = FALSE;
    g_units = 3;      // v2.2.0: "Both (KB/s + MB/s)" is the default (owner rule)
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\NetSpeedMonitor",
                      0, KEY_QUERY_VALUE, &k) == ERROR_SUCCESS) {
        WCHAR v[16] = L"";
        DWORD cb = sizeof(v), type = 0;
        if (RegQueryValueExW(k, L"Mode", NULL, &type, (BYTE*)v, &cb) == ERROR_SUCCESS
            && type == REG_SZ && lstrcmpiW(v, L"floating") == 0)
            g_floatMode = TRUE;
        cb = sizeof(v);
        if (RegQueryValueExW(k, L"Units", NULL, &type, (BYTE*)v, &cb) == ERROR_SUCCESS
            && type == REG_SZ) {
            if (lstrcmpiW(v, L"kb") == 0) g_units = 1;
            else if (lstrcmpiW(v, L"mb") == 0) g_units = 2;
            else g_units = 3;   // "sep", legacy "both", "auto", unknown -> separate lines
        }
        cb = sizeof(v);
        if (RegQueryValueExW(k, L"Theme", NULL, &type, (BYTE*)v, &cb) == ERROR_SUCCESS
            && type == REG_SZ) {
            if (lstrcmpiW(v, L"dark") == 0) g_theme = 1;
            else if (lstrcmpiW(v, L"light") == 0) g_theme = 2;
            else g_theme = 0;
        }
        DWORD dw = 0, cbd = sizeof(DWORD);
        if (RegQueryValueExW(k, L"Colored", NULL, &type, (BYTE*)&dw, &cbd) == ERROR_SUCCESS
            && type == REG_DWORD)
            g_colorSpeeds = dw ? TRUE : FALSE;
        RegCloseKey(k);
    }
}

static void SaveSettings(void)
{
    HKEY k;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\NetSpeedMonitor",
                        0, NULL, 0, KEY_SET_VALUE, NULL, &k, NULL) == ERROR_SUCCESS) {
        const WCHAR* mode = g_floatMode ? L"floating" : L"embedded";
        RegSetValueExW(k, L"Mode", 0, REG_SZ, (const BYTE*)mode,
                       (DWORD)((lstrlenW(mode) + 1) * sizeof(WCHAR)));
        const WCHAR* units = (g_units == 1) ? L"kb" :
                             (g_units == 2) ? L"mb" :
                             (g_units == 3) ? L"sep" : L"auto";
        RegSetValueExW(k, L"Units", 0, REG_SZ, (const BYTE*)units,
                       (DWORD)((lstrlenW(units) + 1) * sizeof(WCHAR)));
        const WCHAR* theme = (g_theme == 1) ? L"dark" : (g_theme == 2) ? L"light" : L"auto";
        RegSetValueExW(k, L"Theme", 0, REG_SZ, (const BYTE*)theme,
                       (DWORD)((lstrlenW(theme) + 1) * sizeof(WCHAR)));
        DWORD colored = g_colorSpeeds ? 1 : 0;
        RegSetValueExW(k, L"Colored", 0, REG_DWORD,
                       (const BYTE*)&colored, sizeof(DWORD));
        RegCloseKey(k);
    }
}

// Re-find the taskbar, (re)embed and reposition. Runs every second so
// explorer restarts, taskbar moves and DPI changes all recover silently.
static void RecalcAndPosition(void)
{
    HWND me = g_hwnd;
    if (!IsWindow(me)) return;

    // keep a taskbar handle for positioning reference in BOTH modes
    // (v2.0.1 bug: a fresh floating-mode start never resolved it, so the
    //  bar stayed parked offscreen at -32000,-32000 forever)
    if (!IsWindow(g_hTaskbar))
        g_hTaskbar = FindWindowW(L"Shell_TrayWnd", NULL);

    // ---- embed / unembed according to mode -----------------------------
    if (g_floatMode) {
        if (GetParent(me) != NULL) {
            SetParent(me, NULL);
            g_embedded = FALSE;
            LogLine(L"mode=floating: unparented");
        }
    } else {
        if (!IsWindow(g_hTaskbar)) {
            g_embedded = FALSE;
            return;                    // no taskbar yet - stay offscreen
        }
        if (GetParent(me) != g_hTaskbar) {
            g_fallbackTries = 0;
            UINT d = DpiOfTaskbar(g_hTaskbar);
            if (d != g_dpi) { g_dpi = d; CreateFontForDpi(); }
            BOOL ok = SetParent(me, g_hTaskbar) ? TRUE : FALSE;
            if (ok != g_embedded) {
                WCHAR b[120];
                swprintf_s(b, 120, L"SetParent(taskbar=%p) -> %s",
                           (void*)g_hTaskbar, ok ? L"OK" : L"FAILED");
                LogLine(b);
            }
            g_embedded = ok;
            ShowWindow(me, SW_SHOWNOACTIVATE);   // TrafficMonitor order:
                                                 // SetParent then show
        }
    }

    RECT rb;
    BOOL haveTb = IsWindow(g_hTaskbar) && GetWindowRect(g_hTaskbar, &rb);

    if (g_floatMode) {
        // floating: sit just above the taskbar; if the taskbar rect cannot
        // be measured, fall back to the primary work area so the bar can
        // never end up stranded offscreen
        int x, y;
        if (haveTb && (rb.bottom - rb.top) > 0) {
            x = rb.right - g_winW - DpiVal(8);
            y = rb.top - g_winH - DpiVal(2);
        } else {
            RECT wa;
            if (!SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0)) return;
            x = wa.right - g_winW - DpiVal(8);
            y = wa.bottom - g_winH - DpiVal(2);
        }
        if (x < 0) x = 0;
        if (y < 0) y = 0;
        if (!IsWindowVisible(me)) ShowWindow(me, SW_SHOWNOACTIVATE);
        // HWND_TOPMOST positions AND re-bands the window into the topmost
        // z-layer in one call. (Setting WS_EX_TOPMOST via SetWindowLongPtr
        // does NOT re-band - that is why the v2.0.1 floating bar was
        // invisible behind every normal window.)
        SetWindowPos(me, HWND_TOPMOST, x, y, g_winW, g_winH,
                     SWP_NOACTIVATE | SWP_NOCOPYBITS);
        return;
    }

    if (!haveTb) return;
    int tw = rb.right - rb.left;
    int th = rb.bottom - rb.top;
    if (tw <= 0 || th <= 0) return;

    // taskbar-relative notify window position
    int notifyLeft = -1;
    HWND hN = FindWindowExW(g_hTaskbar, NULL, L"TrayNotifyWnd", NULL);
    if (hN) {
        RECT rn;
        if (GetWindowRect(hN, &rn)) {
            POINT org = { rb.left, rb.top };
            ScreenToClient(g_hTaskbar, &org);
            notifyLeft = rn.left - org.x;
            if (notifyLeft <= 0 || notifyLeft > tw) notifyLeft = -1;
        }
    }

    // v2.3.0: the separate-lines layout doubles the widget height — clamp
    // it to the real taskbar height so the two lines can never overflow.
    int useH = g_winH;
    if (useH > th) useH = th - 2;
    if (useH < DpiVal(16)) useH = DpiVal(16);

    // vertical: center on the Start button height (TrafficMonitor formula,
    // incl. the touch-device correction term)
    int y = (th - useH) / 2;
    HWND hS = FindWindowExW(g_hTaskbar, NULL, L"Start", NULL);
    if (hS) {
        RECT rs;
        if (GetWindowRect(hS, &rs)) {
            int sh = rs.bottom - rs.top;
            if (sh > 0 && sh <= th) y = (sh - useH) / 2 + (th - sh);
        }
    }

    int x;
    if (notifyLeft > 0) x = notifyLeft - g_winW - DpiVal(2);
    else                x = tw - g_winW - DpiVal(88) - DpiVal(2);  // clock area guess
    if (x < DpiVal(2)) x = DpiVal(2);

    SetWindowPos(me, NULL, x, y, g_winW, useH,
                 SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOCOPYBITS);

    // ---- visibility hardening ------------------------------------------
    // The taskbar's rendered surface is composited above classic HWND
    // siblings on Windows 11. Re-assert z-order + visibility every second
    // so the widget can never stay buried.
    if (!IsWindowVisible(me)) {
        ShowWindow(me, SW_SHOWNOACTIVATE);
        LogLine(L"visibility re-asserted (was hidden)");
    }
    SetWindowPos(me, HWND_TOP, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

// ---------------------------------------------------------------- diag
static void AppendLogTail(WCHAR* out, size_t cch)
{
    WCHAR dir[MAX_PATH * 2];
    DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", dir, MAX_PATH * 2);
    if (n <= 0 || n >= MAX_PATH) return;
    wcscat_s(dir, MAX_PATH * 2, L"\\NetSpeedMonitor\\log.txt");
    FILE* f = _wfopen(dir, L"r, ccs=UTF-8");
    if (!f) return;
    WCHAR lines[12][200];
    int cnt = 0;
    WCHAR line[300];
    while (fgetws(line, 300, f)) {
        line[wcscspn(line, L"\r\n")] = 0;
        wcsncpy_s(lines[cnt % 12], 200, line, _TRUNCATE);
        ++cnt;
    }
    fclose(f);
    int start = cnt > 12 ? 12 : cnt;
    wcscat_s(out, cch, L"\r\n--- log tail ---\r\n");
    for (int i = start; i > 0; --i) {
        wcscat_s(out, cch, lines[(cnt - i) % 12]);
        wcscat_s(out, cch, L"\r\n");
    }
}

static void CopyDiagToClipboard(HWND hwnd)
{
    WCHAR buf[4096];
    RECT rme = { 0, 0, 0, 0 }, rtb = { 0, 0, 0, 0 };
    if (g_hwnd)     GetWindowRect(g_hwnd, &rme);
    if (IsWindow(g_hTaskbar)) GetWindowRect(g_hTaskbar, &rtb);
    swprintf_s(buf, 4096,
        L"NetSpeedMonitor diagnostics\r\n"
        L"version=%s\r\n"
        L"floatMode=%d embedded=%d units=%d theme=%d colored=%d\r\n"
        L"hwnd=%p parent=%p visible=%d\r\n"
        L"win rect=(%ld,%ld)-(%ld,%ld)\r\n"
        L"taskbar=%p rect=(%ld,%ld)-(%ld,%ld)\r\n"
        L"dpi=%u winSize=%dx%d\r\n"
        L"text=%s\r\n",
        APP_VERSION,
        (int)g_floatMode, (int)g_embedded,
        g_units, g_theme, (int)g_colorSpeeds,
        (void*)g_hwnd, (void*)GetParent(g_hwnd), IsWindowVisible(g_hwnd),
        (long)rme.left, (long)rme.top, (long)rme.right, (long)rme.bottom,
        (void*)g_hTaskbar, (long)rtb.left, (long)rtb.top,
        (long)rtb.right, (long)rtb.bottom,
        g_dpi, g_winW, g_winH, g_text);
    AppendLogTail(buf, 4096);

    if (!OpenClipboard(hwnd)) return;
    EmptyClipboard();
    SIZE_T n = (SIZE_T)((lstrlenW(buf) + 1) * (int)sizeof(WCHAR));
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, n);
    if (h) {
        void* p = GlobalLock(h);
        if (p) { memcpy(p, buf, n); GlobalUnlock(h); }
        if (!SetClipboardData(CF_UNICODETEXT, h)) GlobalFree(h);
    }
    CloseClipboard();
}

// ---------------------------------------------------------------- paint
// One speed row: colored mode draws up (red) + down (green) separately,
// plain mode draws the joined string centered. Used by BOTH the single
// line layout and the v2.3.0 separate-lines layout.
static void PaintSpeedRow(HDC dc, RECT rc,
                          const WCHAR* up, const WCHAR* down,
                          const WCHAR* joined)
{
    if (g_colorSpeeds) {
        SIZE su, ss, sd;
        GetTextExtentPoint32W(dc, up,   (int)wcslen(up),   &su);
        GetTextExtentPoint32W(dc, L"  ", 2, &ss);
        GetTextExtentPoint32W(dc, down, (int)wcslen(down), &sd);
        int total = su.cx + ss.cx + sd.cx;
        int x0 = (rc.left + rc.right - total) / 2;
        if (x0 < rc.left) x0 = rc.left;
        RECT r1 = { x0, rc.top, x0 + su.cx, rc.bottom };
        SetTextColor(dc, g_upColor);
        DrawTextW(dc, up, -1, &r1,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        RECT r2 = { x0 + su.cx, rc.top, x0 + su.cx + ss.cx, rc.bottom };
        SetTextColor(dc, g_textColor);
        DrawTextW(dc, L"  ", -1, &r2,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        RECT r3 = { x0 + su.cx + ss.cx, rc.top,
                    x0 + su.cx + ss.cx + sd.cx, rc.bottom };
        SetTextColor(dc, g_downColor);
        DrawTextW(dc, down, -1, &r3,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    } else {
        SetTextColor(dc, g_textColor);
        DrawTextW(dc, joined, -1, &rc,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }
}

static void OnPaint(HWND hwnd)
{
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd, &ps);
    RECT rc;
    GetClientRect(hwnd, &rc);

    HDC mem = CreateCompatibleDC(dc);
    HBITMAP bmp = CreateCompatibleBitmap(dc, rc.right, rc.bottom);
    HGDIOBJ ob = SelectObject(mem, bmp);

    HBRUSH bg = CreateSolidBrush(g_bgColor);
    HGDIOBJ obr = SelectObject(mem, bg);
    FillRect(mem, &rc, bg);
    SelectObject(mem, obr);
    DeleteObject(bg);

    if (g_font) {
        HGDIOBJ of = SelectObject(mem, g_font);
        SetBkMode(mem, TRANSPARENT);
        if (g_units == 3) {
            // SEPARATE lines (v2.3.0): Mb/s row on top, Kb/s row below
            RECT top = rc, bot = rc;
            int half = (rc.bottom - rc.top) / 2;
            top.bottom    = rc.top + half - 1;
            bot.top       = rc.top + half;
            PaintSpeedRow(mem, top, g_lineUpMb, g_lineDnMb, g_lineMb);
            PaintSpeedRow(mem, bot, g_lineUpKb, g_lineDnKb, g_lineKb);
        } else {
            PaintSpeedRow(mem, rc, g_upText, g_downText, g_text);
        }
        SelectObject(mem, of);
    }
    BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
    SelectObject(mem, ob);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(hwnd, &ps);
}

// ---------------------------------------------------------------- menu
static BOOL AutostartEnabled(void)
{
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
            L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
            0, KEY_QUERY_VALUE, &k) != ERROR_SUCCESS) return FALSE;
    DWORD type = 0, cb = 0;
    LONG r = RegQueryValueExW(k, APP_NAME, NULL, &type, NULL, &cb);
    RegCloseKey(k);
    return r == ERROR_SUCCESS;
}

static void AutostartToggle(HWND hwnd)
{
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
            L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
            0, KEY_SET_VALUE, &k) != ERROR_SUCCESS) {
        MessageBoxW(hwnd, L"Could not open the Run registry key.",
                    APP_NAME, MB_ICONERROR);
        return;
    }
    if (AutostartEnabled()) {
        RegDeleteValueW(k, APP_NAME);
    } else {
        WCHAR path[MAX_PATH + 4];
        GetModuleFileNameW(NULL, path, MAX_PATH);
        WCHAR cmd[MAX_PATH * 2];
        swprintf_s(cmd, MAX_PATH * 2, L"\"%s\" /taskbar", path);
        RegSetValueExW(k, APP_NAME, 0, REG_SZ, (const BYTE*)cmd,
                       (DWORD)((wcslen(cmd) + 1) * sizeof(WCHAR)));
    }
    RegCloseKey(k);
}

static void ShowContextMenu(HWND hwnd)
{
    HMENU m = CreatePopupMenu();
    if (!m) return;

    HMENU mu = CreatePopupMenu();          // Units
    AppendMenuW(mu, MF_STRING | (g_units == 3 ? MF_CHECKED : 0), IDM_UNITS_BOTH, L"Kb/s + Mb/s (separate lines)");
    AppendMenuW(mu, MF_STRING | (g_units == 0 ? MF_CHECKED : 0), IDM_UNITS_AUTO, L"Auto");
    AppendMenuW(mu, MF_STRING | (g_units == 1 ? MF_CHECKED : 0), IDM_UNITS_KB,   L"Kb/s");
    AppendMenuW(mu, MF_STRING | (g_units == 2 ? MF_CHECKED : 0), IDM_UNITS_MB,   L"Mb/s");

    HMENU mt = CreatePopupMenu();          // Theme
    AppendMenuW(mt, MF_STRING | (g_theme == 0 ? MF_CHECKED : 0), IDM_THEME_AUTO, L"Auto (match taskbar)");
    AppendMenuW(mt, MF_STRING | (g_theme == 1 ? MF_CHECKED : 0), IDM_THEME_DARK, L"Dark");
    AppendMenuW(mt, MF_STRING | (g_theme == 2 ? MF_CHECKED : 0), IDM_THEME_LITE, L"Light");

    AppendMenuW(m, MF_POPUP, (UINT_PTR)mu, L"&Units");
    AppendMenuW(m, MF_POPUP, (UINT_PTR)mt, L"&Theme");
    AppendMenuW(m, MF_STRING | (g_colorSpeeds ? MF_CHECKED : 0),
                IDM_COLOR, L"Colored speeds (up red, down green)");
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, MF_STRING, IDM_TASKMGR, L"Open &Task Manager");
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, MF_STRING | (AutostartEnabled() ? MF_CHECKED : 0),
                IDM_AUTOSTART, L"&Start with Windows");
    AppendMenuW(m, MF_STRING | (g_floatMode ? MF_CHECKED : 0),
                IDM_FLOAT, L"&Floating mode (if taskbar widget is not visible)");
    AppendMenuW(m, MF_STRING, IDM_DIAG, L"Copy &diagnostics (bug reports)");
    AppendMenuW(m, MF_STRING, IDM_ABOUT, L"&About");
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, MF_STRING, IDM_EXIT, L"E&xit");

    POINT p;
    GetCursorPos(&p);
    SetForegroundWindow(hwnd);          // so the menu dismisses properly
    TrackPopupMenu(m, TPM_RIGHTBUTTON | TPM_NONOTIFY, p.x, p.y, 0, hwnd, NULL);
    PostMessageW(hwnd, WM_NULL, 0, 0);
    DestroyMenu(m);
}

// ---------------------------------------------------------------- wndproc
static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE:
        SetTimer(hwnd, TIMER_TICK, 1000, NULL);
        return 0;
    case WM_TIMER:
        if (wp != TIMER_TICK) return 0;
        UpdateSpeedText();
        RecalcAndPosition();
        if (!g_trayAdded && ++g_trayTick >= 5) { g_trayTick = 0; TrayAdd(); }
        if (++g_hbTick >= 60) {          // 60s heartbeat for remote diagnosis
            g_hbTick = 0;
            RECT rme = { 0, 0, 0, 0 };
            if (g_hwnd) GetWindowRect(g_hwnd, &rme);
            WCHAR b[220];
            swprintf_s(b, 220,
                L"hb: mode=%s parent=%p vis=%d rect=(%ld,%ld)-(%ld,%ld) tb=%p",
                g_floatMode ? L"float" : L"embed",
                (void*)GetParent(g_hwnd), IsWindowVisible(g_hwnd),
                (long)rme.left, (long)rme.top, (long)rme.right, (long)rme.bottom,
                (void*)g_hTaskbar);
            LogLine(b);
        }
        if (++g_colorTick >= 5) {
            g_colorTick = 0;
            if (g_theme == 0) {          // auto theme re-samples the taskbar
                ApplyTheme();
            }
            TrayUpdateTip();
        }
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    case WM_PAINT:
        OnPaint(hwnd);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_APP_TRAY:                    // tray icon mouse
        if (lp == WM_RBUTTONUP || lp == WM_LBUTTONUP || lp == WM_CONTEXTMENU)
            ShowContextMenu(hwnd);
        else if (lp == WM_LBUTTONDBLCLK)
            ShellExecuteW(hwnd, L"open", L"taskmgr.exe", NULL, NULL, SW_SHOWNORMAL);
        return 0;
    case WM_LBUTTONDBLCLK:
        ShellExecuteW(hwnd, L"open", L"taskmgr.exe", NULL, NULL, SW_SHOWNORMAL);
        return 0;
    case WM_CONTEXTMENU:
        ShowContextMenu(hwnd);
        return 0;
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDM_TASKMGR:
            ShellExecuteW(hwnd, L"open", L"taskmgr.exe", NULL, NULL, SW_SHOWNORMAL);
            break;
        case IDM_AUTOSTART:
            AutostartToggle(hwnd);
            break;
        case IDM_FLOAT:
            g_floatMode = !g_floatMode;
            SaveSettings();
            {
                WCHAR b[80];
                swprintf_s(b, 80, L"user toggled mode -> %s",
                           g_floatMode ? L"floating" : L"embedded");
                LogLine(b);
            }
            RecalcAndPosition();     // apply instantly, not on the next tick
            break;
        case IDM_UNITS_AUTO:
        case IDM_UNITS_KB:
        case IDM_UNITS_MB:
        case IDM_UNITS_BOTH:
            g_units = (LOWORD(wp) == IDM_UNITS_KB) ? 1 :
                      (LOWORD(wp) == IDM_UNITS_MB) ? 2 :
                      (LOWORD(wp) == IDM_UNITS_BOTH) ? 3 : 0;
            SaveSettings();
            CreateFontForDpi();      // re-measure the window for the new format
            UpdateSpeedText();
            RecalcAndPosition();
            InvalidateRect(hwnd, NULL, FALSE);
            break;
        case IDM_THEME_AUTO:
        case IDM_THEME_DARK:
        case IDM_THEME_LITE:
            g_theme = (LOWORD(wp) == IDM_THEME_DARK) ? 1 :
                      (LOWORD(wp) == IDM_THEME_LITE) ? 2 : 0;
            ApplyTheme();
            SaveSettings();
            // v2.2.0: repaint IMMEDIATELY (the v2.1.x toggle only posted an
            // invalidate, so the switch could look delayed or half-applied)
            RedrawWindow(hwnd, NULL, NULL,
                         RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW | RDW_ALLCHILDREN);
            break;
        case IDM_COLOR:
            g_colorSpeeds = !g_colorSpeeds;
            SaveSettings();
            InvalidateRect(hwnd, NULL, FALSE);
            break;
        case IDM_DIAG:
            CopyDiagToClipboard(hwnd);
            MessageBoxW(hwnd,
                L"Diagnostics copied to the clipboard.\r\n"
                L"Paste it wherever you report the bug.",
                APP_NAME, MB_ICONINFORMATION);
            break;
        case IDM_ABOUT:
            MessageBoxW(hwnd,
                L"NetSpeedMonitor - taskbar network speed display (v" APP_VERSION L")\r\n\r\n"
                L"Embeds into the Windows 10/11 taskbar (SetParent technique,\r\n"
                L"as pioneered by TrafficMonitor by zhongyang219).\r\n\r\n"
                L"Menu: Units (Kb/s + Mb/s separate lines / Auto / Kb/s / Mb/s),\r\n"
                L"Theme (Auto/Dark/Light), colored speeds (up red, down green).\r\n\r\n"
                L"Not visible in the taskbar? Right-click the tray icon\r\n"
                L"(hidden icons ^) and enable 'Floating mode' (truly topmost).\r\n"
                L"Still stuck? Use 'Copy diagnostics'.\r\n\r\n"
                L"Double-click: Task Manager  |  Right-click: menu",
                APP_NAME, MB_ICONINFORMATION);
            break;
        case IDM_EXIT:
            DestroyWindow(hwnd);
            break;
        }
        return 0;
    case WM_DESTROY:
        KillTimer(hwnd, TIMER_TICK);
        TrayRemove();
        LogLine(L"exit");
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ---------------------------------------------------------------- main
int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE prev, PWSTR cmd, int show)
{
    (void)prev; (void)cmd; (void)show;

    HANDLE mutex = CreateMutexW(NULL, FALSE, MUTEX_NAME);
    if (mutex && GetLastError() == ERROR_ALREADY_EXISTS) {
        // v2.0.0 could end up invisible; give the user a one-click way to
        // end the stuck instance before starting this one.
        int rc = MessageBoxW(NULL,
            L"NetSpeedMonitor is already running.\r\n\r\n"
            L"Look in the taskbar 'hidden icons' area (the ^ chevron near\r\n"
            L"the clock), right-click it for the menu.\r\n\r\n"
            L"If the running copy is an older INVISIBLE version, click OK\r\n"
            L"to open Task Manager and end 'NetSpeedMonitor.exe', then\r\n"
            L"start this version again.",
            APP_NAME, MB_OKCANCEL | MB_ICONINFORMATION);
        if (rc == IDOK)
            ShellExecuteW(NULL, L"open", L"taskmgr.exe", NULL, NULL, SW_SHOWNORMAL);
        return 0;
    }

    EnablePerMonitorDpi();

    WNDCLASSW wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.hIcon         = LoadIconW(hInst, MAKEINTRESOURCEW(1));
    wc.lpszClassName = CLASS_NAME;
    wc.style         = CS_DBLCLKS;
    if (!RegisterClassW(&wc)) return 1;

    g_dpi = DpiOfTaskbar(FindWindowW(L"Shell_TrayWnd", NULL));

    LoadSettings();                   // BEFORE creation so the window is
                                      // born with WS_EX_TOPMOST if floating
    g_hwnd = CreateWindowExW(
        WS_EX_TOOLWINDOW | (g_floatMode ? WS_EX_TOPMOST : 0),
        CLASS_NAME, APP_NAME,
        WS_POPUP | WS_CLIPSIBLINGS, -32000, -32000, 10, 10,
        NULL, NULL, hInst, NULL);
    if (!g_hwnd) return 1;

    CreateFontForDpi();
    ApplyTheme();
    UpdateSpeedText();

    {
        WCHAR b[220];
        swprintf_s(b, 220, L"start v%s: dpi=%u mode=%s units=%d theme=%d colored=%d win=%dx%d",
                   APP_VERSION, g_dpi, g_floatMode ? L"floating" : L"embedded",
                   g_units, g_theme, (int)g_colorSpeeds, g_winW, g_winH);
        LogLine(b);
    }

    RecalcAndPosition();          // finds taskbar + SetParent + position
    ShowWindow(g_hwnd, SW_SHOWNOACTIVATE);
    TrayAdd();                    // always - never invisible-stuck again

    MSG m;
    while (GetMessageW(&m, NULL, 0, 0) > 0) {
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    if (mutex) ReleaseMutex(mutex);
    return 0;
}
