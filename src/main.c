// NetSpeedMonitor - taskbar-embedded network speed display for Windows 10/11
//
// Embedding technique (reading TrafficMonitor by zhongyang219):
//   create a WS_EX_TOOLWINDOW popup window, then SetParent() it into the
//   taskbar (Shell_TrayWnd) and park it just left of TrayNotifyWnd.
//   This is a normal Win32 API usage - no shell DLL / desk band needed,
//   which is exactly why it keeps working on Windows 11 where taskbar
//   toolbars (the old NetSpeedMonitor approach) were removed.
//
// Build (MSVC):  cl /O2 /W3 /DUNICODE /D_UNICODE main.c icon.res ^
//                  /Fe:NetSpeedMonitor.exe /link /SUBSYSTEM:WINDOWS ^
//                  user32.lib gdi32.lib shell32.lib advapi32.lib iphlpapi.lib
// Build (MinGW): x86_64-w64-mingw32-gcc -O2 -mwindows -DUNICODE -D_UNICODE ^
//                  main.c icon.o -o NetSpeedMonitor.exe -liphlpapi -lgdi32 ^
//                  -lshell32 -ladvapi32 -s -static

#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0601
#include <windows.h>
#include <iphlpapi.h>
#include <shellapi.h>
#include <wchar.h>
#include <stdlib.h>

#define APP_NAME     L"NetSpeedMonitor"
#define MUTEX_NAME   L"NetSpeedMonitor_Taskbar_SingleInstance"
#define CLASS_NAME   L"NetSpeedMonitorTaskbarWnd"
#define TIMER_TICK   1          // 1000 ms update + reposition timer
#define IDM_AUTOSTART 20001
#define IDM_TASKMGR    20002
#define IDM_ABOUT      20003
#define IDM_EXIT       20004

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
static int    g_fallbackTries  = 0;
static COLORREF g_bgColor      = RGB(32, 32, 32);
static COLORREF g_textColor    = RGB(240, 240, 240);
static WCHAR  g_text[160]      = L"\x2191 0 B/s  \x2193 0 B/s";
static int    g_colorTick      = 0;
static ULONG64 g_lastTick    = 0;

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
static void FormatRate(double bps, WCHAR* out, size_t cch)
{
    if (bps < 0) bps = 0;
    if (bps >= 1024.0 * 1024.0 * 1024.0)
        swprintf_s(out, cch, L"%.2f GB/s", bps / (1024.0 * 1024.0 * 1024.0));
    else if (bps >= 1024.0 * 1024.0)
        swprintf_s(out, cch, L"%.1f MB/s", bps / (1024.0 * 1024.0));
    else if (bps >= 1024.0)
        swprintf_s(out, cch, L"%.1f KB/s", bps / 1024.0);
    else
        swprintf_s(out, cch, L"%.0f B/s", bps);
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
        WCHAR d[40], u[40];
        FormatRate(down, d, 40);
        FormatRate(up,   u, 40);
        swprintf_s(g_text, 160, L"\x2191 %s  \x2193 %s", u, d);
    }
    g_lastTick = now;
}

// ------------------------------------------------------------- taskbar
// Sample the taskbar's rendered background so our solid fill is seamless.
// Sample point: top strip (2 px from taskbar top edge) a bit left of
// TrayNotifyWnd - always empty background, above our own window.
static void SampleTaskbarColor(void)
{
    RECT rb;
    if (!g_hTaskbar || !GetWindowRect(g_hTaskbar, &rb)) return;
    int sx = rb.right - DpiVal(40);
    HWND hN = FindWindowExW(g_hTaskbar, NULL, L"TrayNotifyWnd", NULL);
    if (hN) {
        RECT rn;
        if (GetWindowRect(hN, &rn) && rn.left - DpiVal(30) > rb.left)
            sx = rn.left - DpiVal(30);
    }
    HDC dc = GetDC(g_hTaskbar);
    if (dc) {
        COLORREF c = GetPixel(dc, sx, rb.top + 2);
        ReleaseDC(g_hTaskbar, dc);
        if (c != CLR_INVALID) {
            g_bgColor = c;
            int r = GetRValue(c), g = GetGValue(c), b = GetBValue(c);
            int lum = (r * 299 + g * 587 + b * 114) / 1000;
            g_textColor = (lum > 128) ? RGB(24, 24, 24) : RGB(240, 240, 240);
        }
    }
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
        swprintf_s(sample, 160, L"\x2191 888.88 MB/s  \x2193 888.88 MB/s");
        if (GetTextExtentPoint32W(dc, sample, (int)wcslen(sample), &sz)) {
            g_winW = sz.cx + DpiVal(14);
            g_winH = sz.cy + DpiVal(4);
            if (g_winH < DpiVal(20)) g_winH = DpiVal(20);
        }
        SelectObject(dc, old);
        ReleaseDC(g_hwnd, dc);
    }
    if (g_winW <= 0) { g_winW = DpiVal(210); g_winH = DpiVal(24); }
}

// Re-find the taskbar, (re)embed and reposition. Runs every second so
// explorer restarts, taskbar moves and DPI changes all recover silently.
static void RecalcAndPosition(void)
{
    HWND me = g_hwnd;
    if (!IsWindow(me)) return;

    if (!IsWindow(g_hTaskbar) || GetParent(me) != g_hTaskbar) {
        g_hTaskbar = FindWindowW(L"Shell_TrayWnd", NULL);
        if (!g_hTaskbar) {
            g_embedded = FALSE;
            return;                    // no taskbar yet - stay offscreen
        }
        g_fallbackTries = 0;
        UINT d = DpiOfTaskbar(g_hTaskbar);
        if (d != g_dpi) { g_dpi = d; CreateFontForDpi(); }
        if (SetParent(me, g_hTaskbar)) {
            g_embedded = TRUE;
            LONG_PTR ex = GetWindowLongPtrW(me, GWL_EXSTYLE);
            SetWindowLongPtrW(me, GWL_EXSTYLE, ex & ~WS_EX_TOPMOST);
        } else {
            g_embedded = FALSE;        // floating topmost fallback
            LONG_PTR ex = GetWindowLongPtrW(me, GWL_EXSTYLE);
            SetWindowLongPtrW(me, GWL_EXSTYLE, ex | WS_EX_TOPMOST);
        }
    }
    if (!g_hTaskbar) return;

    RECT rb;
    if (!GetWindowRect(g_hTaskbar, &rb)) return;
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

    // vertical: center on the Start button height (TrafficMonitor formula,
    // incl. the touch-device correction term)
    int y = (th - g_winH) / 2;
    HWND hS = FindWindowExW(g_hTaskbar, NULL, L"Start", NULL);
    if (hS) {
        RECT rs;
        if (GetWindowRect(hS, &rs)) {
            int sh = rs.bottom - rs.top;
            if (sh > 0 && sh <= th) y = (sh - g_winH) / 2 + (th - sh);
        }
    }

    int x;
    if (notifyLeft > 0) x = notifyLeft - g_winW - DpiVal(2);
    else                x = tw - g_winW - DpiVal(88) - DpiVal(2);  // clock area guess
    if (x < DpiVal(2)) x = DpiVal(2);

    if (!g_embedded) {                 // floating: convert to screen coords
        x += rb.left;
        y += rb.top;
    }
    SetWindowPos(me, NULL, x, y, g_winW, g_winH,
                 SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOCOPYBITS);
}

// ---------------------------------------------------------------- paint
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
        SetTextColor(mem, g_textColor);
        DrawTextW(mem, g_text, -1, &rc,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
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
    AppendMenuW(m, MF_STRING, IDM_TASKMGR, L"Open &Task Manager");
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, MF_STRING | (AutostartEnabled() ? MF_CHECKED : 0),
                IDM_AUTOSTART, L"&Start with Windows");
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
        if (++g_colorTick >= 5) { g_colorTick = 0; SampleTaskbarColor(); }
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    case WM_PAINT:
        OnPaint(hwnd);
        return 0;
    case WM_ERASEBKGND:
        return 1;
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
        case IDM_ABOUT:
            MessageBoxW(hwnd,
                L"NetSpeedMonitor - taskbar network speed display\r\n\r\n"
                L"Embeds into the Windows 10/11 taskbar (SetParent technique,\r\n"
                L"as pioneered by TrafficMonitor by zhongyang219).\r\n\r\n"
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
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ---------------------------------------------------------------- main
int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE prev, PWSTR cmd, int show)
{
    (void)prev; (void)cmd;

    HANDLE mutex = CreateMutexW(NULL, FALSE, MUTEX_NAME);
    if (mutex && GetLastError() == ERROR_ALREADY_EXISTS) {
        MessageBoxW(NULL,
            L"NetSpeedMonitor is already running.\nRight-click its taskbar area to exit.",
            APP_NAME, MB_ICONINFORMATION);
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

    g_hwnd = CreateWindowExW(
        WS_EX_TOOLWINDOW, CLASS_NAME, APP_NAME,
        WS_POPUP, -32000, -32000, 10, 10, NULL, NULL, hInst, NULL);
    if (!g_hwnd) return 1;

    CreateFontForDpi();
    SampleTaskbarColor();
    UpdateSpeedText();
    RecalcAndPosition();          // finds taskbar + SetParent + position
    ShowWindow(g_hwnd, SW_SHOWNOACTIVATE);

    MSG m;
    while (GetMessageW(&m, NULL, 0, 0) > 0) {
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    if (mutex) ReleaseMutex(mutex);
    return 0;
}
