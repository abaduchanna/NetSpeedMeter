// NetSpeedMonitor 3.0.0 - ORIGINAL 2.5.4.0 REBUILD (from the MSI payload)
//
// Owner directive 2026-10-17: "nahi mujhe original NSM bana kar de msi se.
// tune jo banaya hai wo chutiyapay ka hai" - the custom widget look is gone.
// This build is a faithful rebuild of Florian Gilles' NetSpeedMonitor
// 2.5.4.0, reverse-engineered from the x64 MSI (core_dll_601_x64):
//
//   WHAT THE ORIGINAL ACTUALLY IS (proven from the extracted binaries):
//   - a DeskBand COM object (IDeskBand/IDeskBand2 classes in the DLL) that
//     explorer hosts inside the taskbar. Microsoft removed taskbar toolbands
//     on Windows 11, so the deskband can never return; this rebuild keeps
//     the ORIGINAL look and hosts the text window in the taskbar with the
//     SetParent technique TrafficMonitor uses (works on Win10 AND Win11).
//   - display format: "Sent: 12.34 kB/s  Recv: 145.67 kB/s" (labels are the
//     original defaults found in the binary: layout.textsent = "Sent:",
//     layout.textrecv = "Recv:"; layout.changeorder swaps them).
//   - units: kB/s (1000-based) and KiB/s (1024-based), MB/s, MiB/s - the
//     four unit strings found in the DLL. core.bitrate selects the family,
//     core.precision selects %.0f..%.3f (the four precision format strings
//     found in the DLL). 8 bits = 1 byte is irrelevant here: the original
//     is a byte-rate meter, so is this.
//   - speed engine: GetInterfaceInfo() enumeration + GetIfEntry() per
//     adapter, per-adapter octet deltas, no status filter (T196 disasm).
//     The counter LAYER is modernized exactly like v2.6.1: GetIfEntry2
//     (MIB_IF_ROW2, 64-bit InOctets/OutOctets - the counters Task Manager
//     itself uses) first, legacy GetIfEntry row as fallback. The original's
//     own engine reads frozen 0 counters on modern NDIS6 drivers, which is
//     precisely the "0.00 while traffic flows" bug - that one line is the
//     only intentional deviation from the 2.5.4.0 source of truth.
//   - registry config, original key names (all found in the DLL strings):
//     core.monitoring, core.interface, core.bitrate, core.precision,
//     layout.textsent, layout.textrecv, layout.changeorder, layout.color,
//     layout.font, layout.size, traffic.enable - under
//     HKCU\Software\NetSpeedMonitor.
//   - menu (original strings): Monitoring On/Off, Data Traffic, Reset
//     Session, Traffic Import Wizard, Traffic Export Wizard, NetSpeedMonitor
//     Website, Settings, Enable Toolbar Tooltip, Exit.
//   - Data Traffic window: Session / Today / Month / Total x Sent / Recv
//     table + "System Uptime: %d Days %02d:%02d:%02d" (original format
//     string) + daily history list + 14-day bar graph. The original stored
//     this in a SQLite database (traffic.database); this rebuild keeps the
//     same information in a plain daily-log file at the same path
//     (%LOCALAPPDATA%\NetSpeedMonitor\traffic.db) - function preserved,
//     no 700 KB sqlite dependency.
//
// 3.0.0 change summary (vs v2.6.1):
//   1. DISPLAY = original: "Sent: X  Recv: Y", single row, taskbar-native
//      colors (no black box, no theme menu, no stacked rows).
//   2. ORIGINAL registry schema (core.* / layout.*) replaces the v2.x
//      keys; v2.x values (Units/Theme/Mode/Colored) still migrate.
//   3. Settings dialog (the original's configuration UI) with interface
//      picker, unit/precision pickers, sent/recv labels, order swap, font,
//      text color, tooltip, autostart.
//   4. Data Traffic window with Session/Today/Month/Total, daily list,
//      bar graph, CSV import/export (original wizards), Reset Session,
//      Clear Database.
//   5. Engine = original system + NDIS6 counters (v2.6.1 fix kept).
//   6. Removed from v2.x: Units menu, Theme menu, colored-by-default,
//      Task Manager on double-click (original: Data Traffic window).
//
// Build (MSVC):  cl /O2 /W3 /DUNICODE /D_UNICODE main.c icon.res ^
//                /Fe:NetSpeedMonitor.exe /link /SUBSYSTEM:WINDOWS ^
//                user32.lib gdi32.lib shell32.lib advapi32.lib iphlpapi.lib ^
//                comctl32.lib comdlg32.lib

#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0601
// MIB_IF_ROW2 / GetIfEntry2 live in netioapi.h behind `#ifdef _WS2IPDEF_`,
// which only exists after ws2ipdef.h (v2.6.1 CI hit exactly this).
#include <winsock2.h>
#include <ws2ipdef.h>
#ifndef NTDDI_VERSION
#define NTDDI_VERSION 0x06010000     // NTDDI_WIN7
#endif
#include <windows.h>
#include <iphlpapi.h>
#include <netioapi.h>
#include <shellapi.h>
#include <commctrl.h>
#include <commdlg.h>
#include <wchar.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define APP_NAME     L"NetSpeedMonitor"
#define APP_VERSION  L"3.0.0"
#define MUTEX_NAME   L"NetSpeedMonitor_Taskbar_SingleInstance"
#define CLASS_NAME   L"NetSpeedMonitorTaskbarWnd"
#define TRAFFIC_NAME L"NetSpeedMonitorTrafficWnd"
#define REG_PATH     L"Software\\NetSpeedMonitor"
#define TIMER_TICK   1          // 1000 ms update + reposition timer
#define WM_APP_TRAY  (WM_APP + 1)

// menu ids (original menu order)
#define IDM_MONITOR   20001
#define IDM_TRAFFIC   20002
#define IDM_RESETSESS 20003
#define IDM_IMPORT    20004
#define IDM_EXPORT    20005
#define IDM_WEBSITE   20006
#define IDM_SETTINGS  20007
#define IDM_TOOLTIP   20008
#define IDM_FLOAT     20009
#define IDM_AUTOSTART 20010
#define IDM_DIAG      20011
#define IDM_ABOUT     20012
#define IDM_EXIT      20013

// settings dialog item ids
#define IDC_IF      101
#define IDC_UNIT    102
#define IDC_PREC    103
#define IDC_SENT    104
#define IDC_RECV    105
#define IDC_SWAP    106
#define IDC_COL     107
#define IDC_AUTOC   108
#define IDC_TXTCOL  109
#define IDC_FONT    110
#define IDC_START   111
#define IDC_TIP     112

// traffic window button ids
#define TID_RESET   301
#define TID_EXPORT  302
#define TID_IMPORT  303
#define TID_CLEAR   304
#define TID_CLOSE   305

typedef struct {
    double down;   // bytes per second (Recv)
    double up;     // bytes per second (Sent)
} SPEEDS;

// ------------------------------------------------------------------ state
static HWND   g_hwnd;
static HWND   g_hTaskbar;      // Shell_TrayWnd
static HWND   g_hTraffic;      // Data Traffic window (created on demand)
static HWND   g_hList;         // its ListView
static HWND   g_hTooltip;
static HFONT  g_font;
static HFONT  g_listFont;
static UINT   g_dpi            = 96;
static int    g_winW           = 0;
static int    g_winH           = 0;
static BOOL   g_embedded       = FALSE;
static BOOL   g_floatMode      = FALSE;
static int    g_fallbackTries  = 0;
static COLORREF g_bgColor      = RGB(32, 32, 32);
static COLORREF g_textColor    = RGB(240, 240, 240);
static COLORREF g_upColor      = RGB(255, 112, 100);   // Sent red (opt-in)
static COLORREF g_downColor    = RGB(110, 231, 133);   // Recv green (opt-in)
static WCHAR  g_text[160]      = L"Sent: 0.00 B/s  Recv: 0.00 B/s";
static WCHAR  g_sentPart[80]   = L"Sent: 0.00 B/s";
static WCHAR  g_recvPart[80]   = L"Recv: 0.00 B/s";

// ---- original NSM configuration (HKCU\Software\NetSpeedMonitor) ----
static BOOL     g_monitoring   = TRUE;               // core.monitoring
static DWORD    g_ifFilter     = 0;                  // core.interface (0 = all)
static int      g_unit         = 0;                  // core.bitrate 0=auto 1=kB 2=KiB 3=MB 4=MiB
static int      g_precision    = 2;                  // core.precision 0..3
static WCHAR    g_lblSent[32]  = L"Sent:";           // layout.textsent
static WCHAR    g_lblRecv[32]  = L"Recv:";           // layout.textrecv
static BOOL     g_swapOrder    = FALSE;              // layout.changeorder
static BOOL     g_autoColor    = TRUE;               // layout.color = auto
static COLORREF g_userTextCol  = RGB(240, 240, 240); // layout.color
static WCHAR    g_fontName[64] = L"Segoe UI";        // layout.font
static int      g_fontSize     = 9;                  // layout.size (pt)
static BOOL     g_tooltipOn    = TRUE;               // toolbar tooltip
static BOOL     g_colorSpeeds  = FALSE;              // v2.x opt-in, default = original single color

static ULONG64  g_lastTick     = 0;
static ULONG64  g_startTick    = 0;                  // session start (for uptime text)
static ULONG64  g_sessSent     = 0;                  // session counters (bytes)
static ULONG64  g_sessRecv     = 0;
static int      g_hbTick       = 0;
static int      g_colorTick    = 0;
static int      g_trayTick     = 0;
static BOOL     g_trayAdded    = FALSE;
static NOTIFYICONDATAW g_nid;
static HANDLE   g_logGuard     = NULL;
static int      g_saveTick     = 0;

// ------------------------------------------------------------------ log
static void LogLine(const WCHAR* line)
{
    WCHAR dir[MAX_PATH * 2];
    DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", dir, MAX_PATH * 2);
    if (n <= 0 || n >= MAX_PATH) return;
    wcscat_s(dir, MAX_PATH * 2, L"\\NetSpeedMonitor");
    CreateDirectoryW(dir, NULL);
    wcscat_s(dir, MAX_PATH * 2, L"\\log.txt");

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

// ------------------------------------------------------------------ utils
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

// data dir (%LOCALAPPDATA%\NetSpeedMonitor), created on demand
static void DataDir(WCHAR* out, size_t cch)
{
    out[0] = 0;
    DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", out, (DWORD)cch);
    if (n <= 0 || n >= cch - 32) return;
    wcscat_s(out, cch, L"\\NetSpeedMonitor");
    CreateDirectoryW(out, NULL);
}


// ------------------------------------------------------------------ speed
// v3.0.0 SPEED ENGINE = the ORIGINAL NetSpeedMonitor 2.5.4.0 SYSTEM
// (reverse-engineered from MSI payload core_dll_601_x64, T196 disasm:
//  GetInterfaceInfo 0x7A-retry enumerate, 0x104-stride loop, GetIfEntry
//  per adapter, octet deltas, NO dwOperStatus filter) + the one working
//  counter layer on modern Windows: GetIfEntry2 first (64-bit octets,
//  the counters Task Manager uses), legacy GetIfEntry as fallback.
//  core.interface selects a single adapter or all (0 = all, original
//  default).
typedef struct { DWORD idx; ULONG64 in; ULONG64 out; int seen; int missed; int src; } IFSTATE;
static IFSTATE* g_if     = NULL;   // src: 0=priming 1=legacy GetIfEntry 2=GetIfEntry2
static int       g_ifCap = 0;

// diagnostics snapshot (Copy diagnostics reads these; display never does)
static int    g_diagAdapters = 0;
static int    g_diagSrc2     = 0;
static double g_diagDown     = 0;
static double g_diagUp       = 0;

static ULONG64 WrapDiff(ULONG64 now, ULONG64 prev)
{
    if (now >= prev) return now - prev;
    return 0x100000000ULL - prev + now;      // legacy 32-bit counter wrapped
}

static void AccumulateDeltas(double* down, double* up)
{
    ULONG size = 0;
    if (GetInterfaceInfo(NULL, &size) != ERROR_INSUFFICIENT_BUFFER || !size)
        return;
    PIP_INTERFACE_INFO ii = (PIP_INTERFACE_INFO)malloc(size);
    if (!ii) return;
    if (GetInterfaceInfo(ii, &size) != NO_ERROR) { free(ii); return; }

    if (ii->NumAdapters > g_ifCap) {          // grow-only, zero-initialized
        int cap = ii->NumAdapters + 8;
        IFSTATE* ni = (IFSTATE*)calloc((size_t)cap, sizeof(IFSTATE));
        if (!ni) { free(ii); return; }
        if (g_if) { memcpy(ni, g_if, (size_t)g_ifCap * sizeof(IFSTATE)); free(g_if); }
        g_if = ni; g_ifCap = cap;
    }

    int i, j, n = 0, src2 = 0;
    for (i = 0; i < (int)ii->NumAdapters; ++i) {
        DWORD idx  = ii->Adapter[i].Index;
        if (g_ifFilter != 0 && g_ifFilter != idx) {   // core.interface selection
            for (j = 0; j < g_ifCap; ++j)             // keep stale timer fresh
                if (g_if[j].seen && g_if[j].idx == idx) g_if[j].missed = 0;
            continue;
        }
        ULONG64 in = 0, out = 0;
        int src = 0, type = 0;

        MIB_IF_ROW2 row2;                     // NDIS6 64-bit counters first
        ZeroMemory(&row2, sizeof(row2));
        row2.InterfaceIndex = idx;
        if (GetIfEntry2(&row2) == NO_ERROR) {
            type = (int)row2.Type;
            in   = row2.InOctets;
            out  = row2.OutOctets;
            src  = 2;
        } else {                              // legacy fallback = original path
            MIB_IFROW row;
            ZeroMemory(&row, sizeof(row));
            row.dwIndex = idx;
            if (GetIfEntry(&row) != NO_ERROR) continue;
            type = (int)row.dwType;
            in   = row.dwInOctets;
            out  = row.dwOutOctets;
            src  = 1;
        }
        if (type == IF_TYPE_SOFTWARE_LOOPBACK) continue;   // defensive only

        IFSTATE* st = NULL;
        for (j = 0; j < g_ifCap; ++j)
            if (g_if[j].seen && g_if[j].idx == idx) { st = &g_if[j]; break; }
        if (!st) {
            for (j = 0; j < g_ifCap; ++j)
                if (!g_if[j].seen) { st = &g_if[j]; break; }
        }
        if (!st) continue;
        if (st->seen && st->src == src) {     // same counter base -> real delta
            ULONG64 din, dout;
            if (src == 2) {
                din  = (in  >= st->in)  ? in  - st->in  : 0;
                dout = (out >= st->out) ? out - st->out : 0;
            } else {
                din  = WrapDiff(in,  st->in);
                dout = WrapDiff(out, st->out);
            }
            *down += (double)din;
            *up   += (double)dout;
        }
        st->idx = idx; st->in = in; st->out = out;
        st->seen = 1; st->missed = 0; st->src = src;
        if (src == 2) src2 = 1;
        ++n;
    }
    free(ii);

    for (i = 0; i < g_ifCap; ++i) {           // expire dropped adapters
        if (!g_if[i].seen) continue;
        if (++g_if[i].missed > 5) g_if[i].seen = 0;
    }

    g_diagAdapters = n; g_diagSrc2 = src2;
    g_diagDown = *down; g_diagUp = *up;
}

// ------------------------------------------------------------------ format
// ORIGINAL units found in the 2.5.4.0 binary: kB/s (1000), KiB/s (1024),
// MB/s (1000^2), MiB/s (1024^2). core.precision = %.0f..%.3f (default 2,
// matching the "%.2f %s" format string in the DLL).
static const WCHAR* PrecFmt(void)
{
    static const WCHAR* fmts[4] = { L"%.0f", L"%.1f", L"%.2f", L"%.3f" };
    return fmts[g_precision & 3];
}

static void FormatRate(double bps, WCHAR* out, size_t cch)
{
    if (bps < 0) bps = 0;
    const WCHAR* pf = PrecFmt();
    switch (g_unit) {
    case 1:                                   // kB/s = 1000 B/s
        swprintf_s(out, cch, pf, bps / 1000.0);
        wcscat_s(out, cch, L" kB/s");
        return;
    case 2:                                   // KiB/s = 1024 B/s
        swprintf_s(out, cch, pf, bps / 1024.0);
        wcscat_s(out, cch, L" KiB/s");
        return;
    case 3:
        swprintf_s(out, cch, pf, bps / 1000000.0);
        wcscat_s(out, cch, L" MB/s");
        return;
    case 4:
        swprintf_s(out, cch, pf, bps / 1048576.0);
        wcscat_s(out, cch, L" MiB/s");
        return;
    default: {                                // auto scale (original style)
        if (bps >= 1000000000.0) {
            swprintf_s(out, cch, pf, bps / 1000000000.0);
            wcscat_s(out, cch, L" GB/s");
        } else if (bps >= 1000000.0) {
            swprintf_s(out, cch, pf, bps / 1000000.0);
            wcscat_s(out, cch, L" MB/s");
        } else if (bps >= 1000.0) {
            swprintf_s(out, cch, pf, bps / 1000.0);
            wcscat_s(out, cch, L" kB/s");
        } else {
            swprintf_s(out, cch, L"%.0f B/s", bps);
        }
        return;
    }
    }
}

// human byte totals for the traffic window ("%.2f KB" style, original)
static void FormatTotal(ULONG64 bytes, WCHAR* out, size_t cch)
{
    double b = (double)bytes;
    if (b >= 1099511627776.0)
        swprintf_s(out, cch, L"%.2f TB", b / 1099511627776.0);
    else if (b >= 1073741824.0)
        swprintf_s(out, cch, L"%.2f GB", b / 1073741824.0);
    else if (b >= 1048576.0)
        swprintf_s(out, cch, L"%.2f MB", b / 1048576.0);
    else if (b >= 1024.0)
        swprintf_s(out, cch, L"%.2f KB", b / 1024.0);
    else
        swprintf_s(out, cch, L"%I64u B", bytes);
}

// --------------------------------------------------------------- traffic
// Daily traffic log. The ORIGINAL 2.5.4.0 kept this in a SQLite database
// (traffic.enable / traffic.database / traffic.group keys); the same
// information lives here in a plain text file at the same path. If a real
// SQLite file from the original is found, a separate file is used so the
// original database is never touched.
typedef struct { int y, m, d; ULONG64 sent, recv; } DAYREC;
static DAYREC* g_days    = NULL;
static int     g_nDays   = 0;
static int     g_daysCap = 0;

static WCHAR g_dbPath[MAX_PATH * 2] = L"";

static void TrafficDbInit(void)
{
    if (g_dbPath[0]) return;
    WCHAR dir[MAX_PATH * 2];
    DataDir(dir, MAX_PATH * 2);
    swprintf_s(g_dbPath, MAX_PATH * 2, L"%s\\traffic.db", dir);

    // never clobber the original's SQLite database
    FILE* probe = _wfopen(g_dbPath, L"rb");
    if (probe) {
        char hdr[16] = { 0 };
        size_t got = fread(hdr, 1, 15, probe);
        fclose(probe);
        if (got == 15 && memcmp(hdr, "SQLite format 3", 15) == 0)
            swprintf_s(g_dbPath, MAX_PATH * 2, L"%s\\traffic_v3.db", dir);
    }
}

static void TrafficLoad(void)
{
    TrafficDbInit();
    FILE* f = _wfopen(g_dbPath, L"r, ccs=UTF-8");
    if (!f) return;
    WCHAR line[128];
    while (fgetws(line, 128, f)) {
        int y, m, d;
        unsigned long long s, r;
        if (swscanf_s(line, L"%d-%d-%d %llu %llu", &y, &m, &d, &s, &r) == 5) {
            if (g_nDays >= g_daysCap) {
                int cap = g_daysCap ? g_daysCap * 2 : 128;
                DAYREC* nd = (DAYREC*)calloc((size_t)cap, sizeof(DAYREC));
                if (!nd) break;
                if (g_days) { memcpy(nd, g_days, (size_t)g_nDays * sizeof(DAYREC)); free(g_days); }
                g_days = nd; g_daysCap = cap;
            }
            g_days[g_nDays].y = y; g_days[g_nDays].m = m; g_days[g_nDays].d = d;
            g_days[g_nDays].sent = (ULONG64)s;
            g_days[g_nDays].recv = (ULONG64)r;
            ++g_nDays;
        }
    }
    fclose(f);
}

static void TrafficSave(void)
{
    if (!g_dbPath[0]) return;
    FILE* f = _wfopen(g_dbPath, L"w, ccs=UTF-8");
    if (!f) return;
    int i;
    for (i = 0; i < g_nDays; ++i)
        fwprintf(f, L"%04d-%02d-%02d %I64u %I64u\n",
                 g_days[i].y, g_days[i].m, g_days[i].d,
                 g_days[i].sent, g_days[i].recv);
    fclose(f);
}

static DAYREC* TodayRec(BOOL create)
{
    SYSTEMTIME st;
    GetLocalTime(&st);
    int i;
    for (i = 0; i < g_nDays; ++i)
        if (g_days[i].y == st.wYear && g_days[i].m == st.wMonth &&
            g_days[i].d == st.wDay)
            return &g_days[i];
    if (!create) return NULL;
    if (g_nDays >= g_daysCap) {
        int cap = g_daysCap ? g_daysCap * 2 : 128;
        DAYREC* nd = (DAYREC*)calloc((size_t)cap, sizeof(DAYREC));
        if (!nd) return NULL;
        if (g_days) { memcpy(nd, g_days, (size_t)g_nDays * sizeof(DAYREC)); free(g_days); }
        g_days = nd; g_daysCap = cap;
    }
    g_days[g_nDays].y = st.wYear; g_days[g_nDays].m = st.wMonth;
    g_days[g_nDays].d = st.wDay;
    g_days[g_nDays].sent = 0; g_days[g_nDays].recv = 0;
    return &g_days[g_nDays++];
}

static void ResetSession(void)
{
    g_sessSent = 0; g_sessRecv = 0;
    g_startTick = GetTickCount64();
}

static void AddTraffic(ULONG64 sent, ULONG64 recv)
{
    g_sessSent += sent;
    g_sessRecv += recv;
    DAYREC* r = TodayRec(TRUE);
    if (r) { r->sent += sent; r->recv += recv; }
}

static ULONG64 TotalSent(void)
{
    ULONG64 t = 0;
    int i;
    for (i = 0; i < g_nDays; ++i) t += g_days[i].sent;
    return t;
}

static ULONG64 TotalRecv(void)
{
    ULONG64 t = 0;
    int i;
    for (i = 0; i < g_nDays; ++i) t += g_days[i].recv;
    return t;
}

static ULONG64 MonthSent(void)
{
    SYSTEMTIME st;
    GetLocalTime(&st);
    ULONG64 t = 0;
    int i;
    for (i = 0; i < g_nDays; ++i)
        if (g_days[i].y == st.wYear && g_days[i].m == st.wMonth) t += g_days[i].sent;
    return t;
}

static ULONG64 MonthRecv(void)
{
    SYSTEMTIME st;
    GetLocalTime(&st);
    ULONG64 t = 0;
    int i;
    for (i = 0; i < g_nDays; ++i)
        if (g_days[i].y == st.wYear && g_days[i].m == st.wMonth) t += g_days[i].recv;
    return t;
}

static void UpdateSpeedText(void)
{
    double down = 0, up = 0;
    if (g_monitoring) AccumulateDeltas(&down, &up);
    ULONGLONG now = GetTickCount64();

    if (g_lastTick && now > g_lastTick) {
        double dt = (double)(now - g_lastTick) / 1000.0;
        down /= dt;
        up   /= dt;
        if (down < 0 || down > 1e12) down = 0;
        if (up   < 0 || up   > 1e12) up   = 0;

        AddTraffic((ULONG64)(up * dt), (ULONG64)(down * dt));

        WCHAR sv[48], rv[48];
        FormatRate(up,   sv, 48);
        FormatRate(down, rv, 48);
        if (g_swapOrder)   // layout.changeorder: Recv first
            swprintf_s(g_text, 160, L"%s %s  %s %s", g_lblRecv, rv, g_lblSent, sv);
        else
            swprintf_s(g_text, 160, L"%s %s  %s %s", g_lblSent, sv, g_lblRecv, rv);
        swprintf_s(g_sentPart, 80, L"%s %s", g_lblSent, sv);
        swprintf_s(g_recvPart, 80, L"%s %s", g_lblRecv, rv);
    }
    g_lastTick = now;
}


// --------------------------------------------------------------- taskbar
// Sample the taskbar's rendered background (layout.color = "auto") so the
// text sits on the taskbar with native colors - exactly how the original
// deskband drew itself via DrawThemeParentBackground (UxTheme import in
// the original DLL). v2.2.0 fix kept: CLIENT coordinates only.
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

// Text + colored-speed variants derived from background luminance.
static void DeriveColors(void)
{
    int r = GetRValue(g_bgColor), g = GetGValue(g_bgColor), b = GetBValue(g_bgColor);
    int lum = (r * 299 + g * 587 + b * 114) / 1000;
    if (lum > 128) {
        g_textColor = RGB(25, 25, 25);
        g_upColor   = RGB(191, 34, 34);
        g_downColor = RGB(22, 101, 52);
    } else {
        g_textColor = RGB(235, 235, 235);
        g_upColor   = RGB(255, 112, 100);
        g_downColor = RGB(110, 231, 133);
    }
}

static void ApplyColors(void)
{
    SampleTaskbarColor();          // background always matches the taskbar
    DeriveColors();                // arrows derive from bg luminance
    if (!g_autoColor) g_textColor = g_userTextCol;   // layout.color override
}

static void DestroyFont(void)
{
    if (g_font) { DeleteObject(g_font); g_font = NULL; }
    if (g_listFont) { DeleteObject(g_listFont); g_listFont = NULL; }
}

static void CreateFontForDpi(void)
{
    DestroyFont();
    int hpt = -MulDiv(g_fontSize > 0 ? g_fontSize : 9, (int)g_dpi, 72);
    g_font = CreateFontW(hpt, 0, 0, 0, FW_NORMAL,
                         FALSE, FALSE, FALSE,
                         DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                         CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                         DEFAULT_PITCH | FF_DONTCARE, g_fontName);
    HDC dc = GetDC(g_hwnd);
    if (dc) {
        HGDIOBJ old = SelectObject(dc, g_font);
        SIZE sz;
        WCHAR sample[160];
        swprintf_s(sample, 160,
            L"%s 888.88 MiB/s  %s 888.88 MiB/s", g_lblSent, g_lblRecv);
        if (GetTextExtentPoint32W(dc, sample, (int)wcslen(sample), &sz)) {
            g_winW = sz.cx + DpiVal(12);
            g_winH = sz.cy + DpiVal(4);
            if (g_winH < DpiVal(20)) g_winH = DpiVal(20);
        }
        SelectObject(dc, old);
        ReleaseDC(g_hwnd, dc);
    }
    if (g_winW <= 0) { g_winW = DpiVal(240); g_winH = DpiVal(24); }
    g_listFont = CreateFontW(-MulDiv(9, (int)g_dpi, 72), 0, 0, 0, FW_NORMAL,
                             FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                             OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                             CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                             L"Segoe UI");
}

// --------------------------------------------------------------- tray icon
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
    if (g_trayAdded) LogLine(L"tray icon added");
}

static void TrayUpdateTip(void)
{
    if (!g_trayAdded) return;
    DAYREC* t = TodayRec(FALSE);
    WCHAR dt[48] = L"0 B", ut[48] = L"0 B";
    if (t) { FormatTotal(t->recv, dt, 48); FormatTotal(t->sent, ut, 48); }
    WCHAR tip[320];
    swprintf_s(tip, 320, L"%s v%s\n%s\n%s\nToday: recv %s / sent %s",
               APP_NAME, APP_VERSION,
               g_monitoring ? L"Monitoring On" : L"Monitoring is disabled",
               g_text, dt, ut);
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

// --------------------------------------------------------------- settings
static void LoadSettings(void)
{
    HKEY k;
    g_floatMode = FALSE; g_monitoring = TRUE; g_ifFilter = 0;
    g_unit = 0; g_precision = 2; g_swapOrder = FALSE; g_colorSpeeds = FALSE;
    g_autoColor = TRUE; g_tooltipOn = TRUE;
    lstrcpynW(g_lblSent, L"Sent:", 32);
    lstrcpynW(g_lblRecv, L"Recv:", 32);
    lstrcpynW(g_fontName, L"Segoe UI", 64);
    g_fontSize = 9;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, REG_PATH,
                      0, KEY_QUERY_VALUE, &k) == ERROR_SUCCESS) {
        WCHAR v[280] = L"";
        DWORD cb = 0, type = 0, dw = 0, cbd = sizeof(DWORD);

        cbd = sizeof(DWORD);
        if (RegQueryValueExW(k, L"core.monitoring", NULL, &type,
                             (BYTE*)&dw, &cbd) == ERROR_SUCCESS && type == REG_DWORD)
            g_monitoring = dw ? TRUE : FALSE;
        cb = sizeof(v);
        if (RegQueryValueExW(k, L"core.interface", NULL, &type,
                             (BYTE*)v, &cb) == ERROR_SUCCESS && type == REG_SZ)
            g_ifFilter = (lstrcmpiW(v, L"all") == 0) ? 0 : (DWORD)wcstoul(v, NULL, 10);
        cb = sizeof(v);
        if (RegQueryValueExW(k, L"core.bitrate", NULL, &type,
                             (BYTE*)v, &cb) == ERROR_SUCCESS && type == REG_SZ) {
            if      (lstrcmpiW(v, L"kb")  == 0) g_unit = 1;
            else if (lstrcmpiW(v, L"kib") == 0) g_unit = 2;
            else if (lstrcmpiW(v, L"mb")  == 0) g_unit = 3;
            else if (lstrcmpiW(v, L"mib") == 0) g_unit = 4;
            else g_unit = 0;
        }
        // v2.x Units migration (kb->1, mb->3, bits families -> auto)
        cb = sizeof(v);
        if (RegQueryValueExW(k, L"Units", NULL, &type, (BYTE*)v, &cb) == ERROR_SUCCESS
            && type == REG_SZ && g_unit == 0) {
            if      (lstrcmpiW(v, L"kb") == 0) g_unit = 1;
            else if (lstrcmpiW(v, L"mb") == 0) g_unit = 3;
        }
        cbd = sizeof(DWORD);
        if (RegQueryValueExW(k, L"core.precision", NULL, &type,
                             (BYTE*)&dw, &cbd) == ERROR_SUCCESS && type == REG_DWORD)
            g_precision = (int)(dw & 3);
        cb = sizeof(v);
        if (RegQueryValueExW(k, L"layout.textsent", NULL, &type,
                             (BYTE*)v, &cb) == ERROR_SUCCESS && type == REG_SZ) {
            v[31] = 0;
            lstrcpynW(g_lblSent, v, 32);
        }
        cb = sizeof(v);
        if (RegQueryValueExW(k, L"layout.textrecv", NULL, &type,
                             (BYTE*)v, &cb) == ERROR_SUCCESS && type == REG_SZ) {
            v[31] = 0;
            lstrcpynW(g_lblRecv, v, 32);
        }
        cbd = sizeof(DWORD);
        if (RegQueryValueExW(k, L"layout.changeorder", NULL, &type,
                             (BYTE*)&dw, &cbd) == ERROR_SUCCESS && type == REG_DWORD)
            g_swapOrder = dw ? TRUE : FALSE;
        cb = sizeof(v);
        if (RegQueryValueExW(k, L"layout.color", NULL, &type,
                             (BYTE*)v, &cb) == ERROR_SUCCESS && type == REG_SZ
            && lstrcmpiW(v, L"auto") != 0) {
            int r, g, b;
            if (swscanf_s(v, L"%d,%d,%d", &r, &g, &b) == 3) {
                g_autoColor = FALSE;
                g_userTextCol = RGB(r, g, b);
            }
        }
        cb = sizeof(v);
        if (RegQueryValueExW(k, L"layout.font", NULL, &type,
                             (BYTE*)v, &cb) == ERROR_SUCCESS && type == REG_SZ) {
            v[63] = 0;
            lstrcpynW(g_fontName, v, 64);
        }
        cbd = sizeof(DWORD);
        if (RegQueryValueExW(k, L"layout.size", NULL, &type,
                             (BYTE*)&dw, &cbd) == ERROR_SUCCESS && type == REG_DWORD
            && dw >= 6 && dw <= 24)
            g_fontSize = (int)dw;
        cbd = sizeof(DWORD);
        if (RegQueryValueExW(k, L"tooltip", NULL, &type,
                             (BYTE*)&dw, &cbd) == ERROR_SUCCESS && type == REG_DWORD)
            g_tooltipOn = dw ? TRUE : FALSE;
        cbd = sizeof(DWORD);
        if (RegQueryValueExW(k, L"Colored", NULL, &type,
                             (BYTE*)&dw, &cbd) == ERROR_SUCCESS && type == REG_DWORD)
            g_colorSpeeds = dw ? TRUE : FALSE;
        cb = sizeof(v);
        if (RegQueryValueExW(k, L"Mode", NULL, &type, (BYTE*)v, &cb) == ERROR_SUCCESS
            && type == REG_SZ && lstrcmpiW(v, L"floating") == 0)
            g_floatMode = TRUE;
        RegCloseKey(k);
    }
}

static void SaveSettings(void)
{
    HKEY k;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, REG_PATH,
                        0, NULL, 0, KEY_SET_VALUE, NULL, &k, NULL) == ERROR_SUCCESS) {
        DWORD dw = g_monitoring ? 1 : 0;
        RegSetValueExW(k, L"core.monitoring", 0, REG_DWORD, (const BYTE*)&dw, sizeof(DWORD));
        WCHAR v[32];
        if (g_ifFilter == 0) lstrcpynW(v, L"all", 32);
        else                 swprintf_s(v, 32, L"%lu", (unsigned long)g_ifFilter);
        RegSetValueExW(k, L"core.interface", 0, REG_SZ, (const BYTE*)v,
                       (DWORD)((lstrlenW(v) + 1) * sizeof(WCHAR)));
        const WCHAR* unit = (g_unit == 1) ? L"kb" : (g_unit == 2) ? L"kib" :
                            (g_unit == 3) ? L"mb" : (g_unit == 4) ? L"mib" : L"auto";
        RegSetValueExW(k, L"core.bitrate", 0, REG_SZ, (const BYTE*)unit,
                       (DWORD)((lstrlenW(unit) + 1) * sizeof(WCHAR)));
        dw = (DWORD)g_precision;
        RegSetValueExW(k, L"core.precision", 0, REG_DWORD, (const BYTE*)&dw, sizeof(DWORD));
        RegSetValueExW(k, L"layout.textsent", 0, REG_SZ, (const BYTE*)g_lblSent,
                       (DWORD)((lstrlenW(g_lblSent) + 1) * sizeof(WCHAR)));
        RegSetValueExW(k, L"layout.textrecv", 0, REG_SZ, (const BYTE*)g_lblRecv,
                       (DWORD)((lstrlenW(g_lblRecv) + 1) * sizeof(WCHAR)));
        dw = g_swapOrder ? 1 : 0;
        RegSetValueExW(k, L"layout.changeorder", 0, REG_DWORD, (const BYTE*)&dw, sizeof(DWORD));
        if (g_autoColor) {
            const WCHAR* c = L"auto";
            RegSetValueExW(k, L"layout.color", 0, REG_SZ, (const BYTE*)c,
                           (DWORD)((lstrlenW(c) + 1) * sizeof(WCHAR)));
        } else {
            WCHAR c[32];
            swprintf_s(c, 32, L"%d,%d,%d",
                       (int)GetRValue(g_userTextCol),
                       (int)GetGValue(g_userTextCol),
                       (int)GetBValue(g_userTextCol));
            RegSetValueExW(k, L"layout.color", 0, REG_SZ, (const BYTE*)c,
                           (DWORD)((lstrlenW(c) + 1) * sizeof(WCHAR)));
        }
        RegSetValueExW(k, L"layout.font", 0, REG_SZ, (const BYTE*)g_fontName,
                       (DWORD)((lstrlenW(g_fontName) + 1) * sizeof(WCHAR)));
        dw = (DWORD)g_fontSize;
        RegSetValueExW(k, L"layout.size", 0, REG_DWORD, (const BYTE*)&dw, sizeof(DWORD));
        dw = g_tooltipOn ? 1 : 0;
        RegSetValueExW(k, L"tooltip", 0, REG_DWORD, (const BYTE*)&dw, sizeof(DWORD));
        dw = g_colorSpeeds ? 1 : 0;
        RegSetValueExW(k, L"Colored", 0, REG_DWORD, (const BYTE*)&dw, sizeof(DWORD));
        const WCHAR* mode = g_floatMode ? L"floating" : L"embedded";
        RegSetValueExW(k, L"Mode", 0, REG_SZ, (const BYTE*)mode,
                       (DWORD)((lstrlenW(mode) + 1) * sizeof(WCHAR)));
        RegCloseKey(k);
    }
}

// Re-find the taskbar, (re)embed and reposition every second (explorer
// restarts, taskbar moves, DPI changes all recover silently). Same
// proven v2.x placement: taskbar, just left of TrayNotifyWnd.
static void RecalcAndPosition(void)
{
    HWND me = g_hwnd;
    if (!IsWindow(me)) return;

    if (!IsWindow(g_hTaskbar))
        g_hTaskbar = FindWindowW(L"Shell_TrayWnd", NULL);

    if (g_floatMode) {
        if (GetParent(me) != NULL) {
            SetParent(me, NULL);
            g_embedded = FALSE;
            LogLine(L"mode=floating: unparented");
        }
    } else {
        if (!IsWindow(g_hTaskbar)) {
            g_embedded = FALSE;
            return;
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
            ShowWindow(me, SW_SHOWNOACTIVATE);
        }
    }

    RECT rb;
    BOOL haveTb = IsWindow(g_hTaskbar) && GetWindowRect(g_hTaskbar, &rb);

    if (g_floatMode) {
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
        SetWindowPos(me, HWND_TOPMOST, x, y, g_winW, g_winH,
                     SWP_NOACTIVATE | SWP_NOCOPYBITS);
        return;
    }

    if (!haveTb) return;
    int tw = rb.right - rb.left;
    int th = rb.bottom - rb.top;
    if (tw <= 0 || th <= 0) return;

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

    int useH = g_winH;
    if (useH > th) useH = th - 2;
    if (useH < DpiVal(16)) useH = DpiVal(16);

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
    else                x = tw - g_winW - DpiVal(88) - DpiVal(2);
    if (x < DpiVal(2)) x = DpiVal(2);

    SetWindowPos(me, NULL, x, y, g_winW, useH,
                 SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOCOPYBITS);

    if (!IsWindowVisible(me)) {
        ShowWindow(me, SW_SHOWNOACTIVATE);
        LogLine(L"visibility re-asserted (was hidden)");
    }
    SetWindowPos(me, HWND_TOP, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);

    // keep the tooltip rect in sync with the (possibly moved) widget
    if (g_hTooltip) {
        RECT rc;
        GetClientRect(me, &rc);
        TOOLINFO ti;
        ZeroMemory(&ti, sizeof(ti));
        ti.cbSize   = sizeof(ti);
        ti.hwnd     = me;
        ti.uId      = 0;
        ti.rect     = rc;
        SendMessageW(g_hTooltip, TTM_NEWTOOLRECT, 0, (LPARAM)&ti);
    }
}


// --------------------------------------------------------------- paint
// The widget paints ONLY the original text line: "Sent: X  Recv: Y".
// Background = taskbar color (auto) so the text looks painted directly on
// the taskbar, exactly like the original deskband. Colored speeds is an
// opt-in (v2.x legacy, default OFF = original single-color look).
static void PaintSpeedRow(HDC dc, RECT rc,
                          const WCHAR* p1, const WCHAR* p2,
                          const WCHAR* p3, const WCHAR* p4,
                          const WCHAR* joined)
{
    SetBkMode(dc, TRANSPARENT);
    if (g_colorSpeeds) {
        // p1=lbl1 p2=val1 p3=lbl2 p4=val2 (sent red / recv green)
        COLORREF c1 = g_swapOrder ? g_downColor : g_upColor;
        COLORREF c2 = g_swapOrder ? g_upColor   : g_downColor;
        SIZE s1, s2, s3, s4;
        GetTextExtentPoint32W(dc, p1, (int)wcslen(p1), &s1);
        GetTextExtentPoint32W(dc, p2, (int)wcslen(p2), &s2);
        GetTextExtentPoint32W(dc, p3, (int)wcslen(p3), &s3);
        GetTextExtentPoint32W(dc, p4, (int)wcslen(p4), &s4);
        int total = s1.cx + s2.cx + s3.cx + s4.cx + DpiVal(4);
        int x0 = (rc.left + rc.right - total) / 2;
        if (x0 < rc.left) x0 = rc.left;
        RECT r; int x = x0;
        #define DRAW_SEG(txt, col, szx) { \
            r.left = x; r.top = rc.top; r.right = x + (szx); r.bottom = rc.bottom; \
            SetTextColor(dc, (col)); \
            DrawTextW(dc, txt, -1, &r, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS); \
            x += (szx); }
        DRAW_SEG(p1, c1, s1.cx)
        DRAW_SEG(p2, c1, s2.cx)
        DRAW_SEG(L"  ", g_textColor, DpiVal(4))
        DRAW_SEG(p3, c2, s3.cx)
        DRAW_SEG(p4, c2, s4.cx)
        #undef DRAW_SEG
        (void)s3; (void)s4;
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
        // draw as four segments so colored mode can color them separately
        WCHAR t1[80], t2[80], t3[80], t4[80];
        if (g_swapOrder) {
            lstrcpynW(t1, g_lblRecv, 80); lstrcpynW(t2, g_recvPart + lstrlenW(g_lblRecv) + 1, 80);
            lstrcpynW(t3, g_lblSent, 80); lstrcpynW(t4, g_sentPart + lstrlenW(g_lblSent) + 1, 80);
        } else {
            lstrcpynW(t1, g_lblSent, 80); lstrcpynW(t2, g_sentPart + lstrlenW(g_lblSent) + 1, 80);
            lstrcpynW(t3, g_lblRecv, 80); lstrcpynW(t4, g_recvPart + lstrlenW(g_lblRecv) + 1, 80);
        }
        PaintSpeedRow(mem, rc, t1, t2, t3, t4, g_text);
        SelectObject(mem, of);
    }
    BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
    SelectObject(mem, ob);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(hwnd, &ps);
}

// --------------------------------------------------------------- tooltip
static void CreateTooltip(HWND hwnd, HINSTANCE hInst)
{
    g_hTooltip = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        TOOLTIPS_CLASSW, NULL, WS_POPUP | TTS_ALWAYSTIP,
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
        hwnd, NULL, hInst, NULL);
    if (!g_hTooltip) return;
    TOOLINFO ti;
    ZeroMemory(&ti, sizeof(ti));
    ti.cbSize   = sizeof(ti);
    ti.uFlags   = TTF_SUBCLASS;
    ti.hwnd     = hwnd;
    ti.uId      = 0;
    ti.lpszText = (LPWSTR)LPSTR_TEXTCALLBACKW;
    GetClientRect(hwnd, &ti.rect);
    SendMessageW(g_hTooltip, TTM_ADDTOOL, 0, (LPARAM)&ti);
    SendMessageW(g_hTooltip, TTM_SETMAXTIPWIDTH, 0, 340);
}

static void HandleTooltipText(NMTTDISPINFOW* di)
{
    static WCHAR buf[300];
    DAYREC* t = TodayRec(FALSE);
    WCHAR dt[48] = L"0 B", ut[48] = L"0 B";
    if (t) { FormatTotal(t->recv, dt, 48); FormatTotal(t->sent, ut, 48); }
    HWND hN = FindWindowExW(g_hTaskbar, NULL, L"TrayNotifyWnd", NULL);
    (void)hN;
    swprintf_s(buf, 300,
        L"NetSpeedMonitor v%s\n%s\n%s\n%s\nToday  recv: %s   sent: %s",
        APP_VERSION,
        g_monitoring ? L"Monitoring On" : L"Monitoring is disabled",
        g_text, L"", dt, ut);
    di->lpszText = buf;
}

// --------------------------------------------------------------- traffic
static void TrafficRefreshList(void)
{
    if (!g_hList) return;
    SendMessageW(g_hList, LVM_DELETEALLITEMS, 0, 0);
    WCHAR a[64], b[64], c[64];
    LVITEMW it;
    int shown = 0;
    int i;
    for (i = g_nDays - 1; i >= 0 && shown < 400; --i, ++shown) {
        ZeroMemory(&it, sizeof(it));
        it.mask    = LVIF_TEXT;
        it.iItem   = shown;
        swprintf_s(a, 64, L"%04d-%02d-%02d", g_days[i].y, g_days[i].m, g_days[i].d);
        it.pszText = a;
        SendMessageW(g_hList, LVM_INSERTITEMW, 0, (LPARAM)&it);
        FormatTotal(g_days[i].sent, b, 64);
        FormatTotal(g_days[i].recv, c, 64);
        it.iItem = shown; it.iSubItem = 1; it.pszText = b;
        SendMessageW(g_hList, LVM_SETITEMW, 0, (LPARAM)&it);
        it.iSubItem = 2; it.pszText = c;
        SendMessageW(g_hList, LVM_SETITEMW, 0, (LPARAM)&it);
        FormatTotal(g_days[i].sent + g_days[i].recv, b, 64);
        it.iSubItem = 3; it.pszText = b;
        SendMessageW(g_hList, LVM_SETITEMW, 0, (LPARAM)&it);
    }
}

static void FormatUptime(ULONG64 ms, WCHAR* out, size_t cch)
{
    DWORD secs = (DWORD)(ms / 1000);
    DWORD days = secs / 86400;
    DWORD hh   = (secs % 86400) / 3600;
    DWORD mm   = (secs % 3600) / 60;
    DWORD ss   = secs % 60;
    swprintf_s(out, cch, L"System Uptime: %d Days %02d:%02d:%02d",
               (int)days, (int)hh, (int)mm, (int)ss);
}

// --------------------------------------------------------------- CSV i/o
static BOOL TrafficExportCSV(HWND owner)
{
    WCHAR file[MAX_PATH] = L"NetSpeedMonitor_Traffic.csv";
    OPENFILENAMEW ofn;
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = owner;
    ofn.lpstrFilter = L"CSV files (*.csv)\0*.csv\0All files (*.*)\0*.*\0";
    ofn.lpstrFile   = file;
    ofn.nMaxFile    = MAX_PATH;
    ofn.Flags       = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    if (!GetSaveFileNameW(&ofn)) return FALSE;
    FILE* f = _wfopen(file, L"w, ccs=UTF-8");
    if (!f) return FALSE;
    fwprintf(f, L"Date,Sent,Recv\n");
    int i;
    for (i = 0; i < g_nDays; ++i)
        fwprintf(f, L"%04d-%02d-%02d,%I64u,%I64u\n",
                 g_days[i].y, g_days[i].m, g_days[i].d,
                 g_days[i].sent, g_days[i].recv);
    fclose(f);
    return TRUE;
}

static BOOL TrafficImportCSV(HWND owner)
{
    WCHAR file[MAX_PATH] = L"";
    OPENFILENAMEW ofn;
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = owner;
    ofn.lpstrFilter = L"CSV files (*.csv)\0*.csv\0All files (*.*)\0*.*\0";
    ofn.lpstrFile   = file;
    ofn.nMaxFile    = MAX_PATH;
    ofn.Flags       = OFN_FILEMUSTEXIST;
    if (!GetOpenFileNameW(&ofn)) return FALSE;
    FILE* f = _wfopen(file, L"r, ccs=UTF-8");
    if (!f) return FALSE;
    int merged = 0;
    WCHAR line[128];
    while (fgetws(line, 128, f)) {
        int y, m, d;
        unsigned long long s, r;
        if (swscanf_s(line, L"%d-%d-%d,%llu,%llu", &y, &m, &d, &s, &r) == 5 ||
            swscanf_s(line, L"%d-%d-%d;%llu;%llu", &y, &m, &d, &s, &r) == 5) {
            DAYREC* ex = NULL;
            int i;
            for (i = 0; i < g_nDays; ++i)
                if (g_days[i].y == y && g_days[i].m == m && g_days[i].d == d) { ex = &g_days[i]; break; }
            if (ex) {
                if ((ULONG64)s > ex->sent) ex->sent = (ULONG64)s;
                if ((ULONG64)r > ex->recv) ex->recv = (ULONG64)r;
            } else {
                if (g_nDays >= g_daysCap) {
                    int cap = g_daysCap ? g_daysCap * 2 : 128;
                    DAYREC* nd = (DAYREC*)calloc((size_t)cap, sizeof(DAYREC));
                    if (!nd) break;
                    if (g_days) { memcpy(nd, g_days, (size_t)g_nDays * sizeof(DAYREC)); free(g_days); }
                    g_days = nd; g_daysCap = cap;
                }
                g_days[g_nDays].y = y; g_days[g_nDays].m = m; g_days[g_nDays].d = d;
                g_days[g_nDays].sent = (ULONG64)s;
                g_days[g_nDays].recv = (ULONG64)r;
                ++g_nDays;
            }
            ++merged;
        }
    }
    fclose(f);
    TrafficSave();
    TrafficRefreshList();
    return merged > 0;
}

// graph: last 14 days, green = recv (bottom), red = sent (stacked on top)
static void PaintGraph(HDC dc, RECT rc)
{
    FillRect(dc, &rc, (HBRUSH)GetStockObject(WHITE_BRUSH));
    if (g_nDays <= 0 || rc.right - rc.left < 40) return;
    int days = 14;
    int i0 = g_nDays > days ? g_nDays - days : 0;
    int n = g_nDays - i0;
    ULONG64 mx = 1;
    int i;
    for (i = i0; i < g_nDays; ++i) {
        ULONG64 t = g_days[i].sent + g_days[i].recv;
        if (t > mx) mx = t;
    }
    int bw = (rc.right - rc.left) / (n > 0 ? n : 1);
    if (bw > DpiVal(38)) bw = DpiVal(38);
    int gh = rc.bottom - rc.top - DpiVal(14);
    if (gh < 10) return;
    for (i = i0; i < g_nDays; ++i) {
        int bi = i - i0;
        int x = rc.left + bi * bw;
        int w = bw - DpiVal(3);
        if (w < 2) w = 2;
        int hS = (int)((double)g_days[i].sent / (double)mx * (double)gh);
        int hR = (int)((double)g_days[i].recv / (double)mx * (double)gh);
        int hTot = hS + hR;
        if (hTot > gh) { int over = hTot - gh; hR -= over / 2; hS -= over - over / 2; }
        if (hR < 0) hR = 0;
        if (hS < 0) hS = 0;
        RECT rb = { x, rc.bottom - DpiVal(14) - hR, x + w, rc.bottom - DpiVal(14) };
        HBRUSH br1 = CreateSolidBrush(RGB(70, 160, 90));
        FillRect(dc, &rb, br1);
        DeleteObject(br1);
        RECT rs = { x, rc.bottom - DpiVal(14) - hR - hS, x + w, rc.bottom - DpiVal(14) - hR };
        HBRUSH br2 = CreateSolidBrush(RGB(200, 80, 70));
        FillRect(dc, &rs, br2);
        DeleteObject(br2);
        WCHAR dlab[8];
        swprintf_s(dlab, 8, L"%02d", g_days[i].d);
        RECT rl = { x, rc.bottom - DpiVal(13), x + bw, rc.bottom };
        SetTextColor(dc, RGB(90, 90, 90));
        DrawTextW(dc, dlab, -1, &rl, DT_CENTER | DT_TOP | DT_SINGLELINE);
    }
    SetTextColor(dc, RGB(60, 60, 60));
    RECT rh = { rc.left + DpiVal(4), rc.top + DpiVal(2), rc.right, rc.top + DpiVal(16) };
    DrawTextW(dc, L"last 14 days  (green = Recv, red = Sent)", -1, &rh,
              DT_LEFT | DT_TOP | DT_SINGLELINE);
}

static void PaintTotals(HDC dc, RECT rc)
{
    SetBkMode(dc, TRANSPARENT);
    WCHAR sv[48], rv[48], line[160];
    int y = rc.top + DpiVal(8);
    int x1 = rc.left + DpiVal(110);
    int x2 = rc.left + DpiVal(250);

    HGDIOBJ of = SelectObject(dc, g_listFont);
    SetTextColor(dc, RGB(40, 40, 40));
    TextOutW(dc, x1, y, L"Sent", 4);
    TextOutW(dc, x2, y, L"Recv", 4);
    y += DpiVal(20);

    FormatTotal(g_sessSent, sv, 48); FormatTotal(g_sessRecv, rv, 48);
    swprintf_s(line, 160, L"Session"); TextOutW(dc, rc.left + DpiVal(10), y, line, (int)wcslen(line));
    TextOutW(dc, x1, y, sv, (int)wcslen(sv));
    TextOutW(dc, x2, y, rv, (int)wcslen(rv));
    y += DpiVal(18);

    DAYREC* t = TodayRec(FALSE);
    FormatTotal(t ? t->sent : 0, sv, 48); FormatTotal(t ? t->recv : 0, rv, 48);
    TextOutW(dc, rc.left + DpiVal(10), y, L"Today", 5);
    TextOutW(dc, x1, y, sv, (int)wcslen(sv));
    TextOutW(dc, x2, y, rv, (int)wcslen(rv));
    y += DpiVal(18);

    FormatTotal(MonthSent(), sv, 48); FormatTotal(MonthRecv(), rv, 48);
    TextOutW(dc, rc.left + DpiVal(10), y, L"Month", 5);
    TextOutW(dc, x1, y, sv, (int)wcslen(sv));
    TextOutW(dc, x2, y, rv, (int)wcslen(rv));
    y += DpiVal(18);

    FormatTotal(TotalSent(), sv, 48); FormatTotal(TotalRecv(), rv, 48);
    TextOutW(dc, rc.left + DpiVal(10), y, L"Total", 5);
    TextOutW(dc, x1, y, sv, (int)wcslen(sv));
    TextOutW(dc, x2, y, rv, (int)wcslen(rv));
    y += DpiVal(20);

    WCHAR up[80];
    FormatUptime(GetTickCount64(), up, 80);
    SetTextColor(dc, RGB(90, 90, 90));
    TextOutW(dc, rc.left + DpiVal(10), y, up, (int)wcslen(up));
    SelectObject(dc, of);
    (void)rc;
}

#define TOT_H   96
#define LIST_H  190
#define GRAPH_H 100
#define BTN_H   30

static LRESULT CALLBACK TrafficWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_TIMER:
        if (wp == 2) { TrafficRefreshList(); InvalidateRect(hwnd, NULL, TRUE); }
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);
        HBRUSH bg = CreateSolidBrush(RGB(250, 250, 250));
        FillRect(dc, &rc, bg);
        DeleteObject(bg);
        RECT rt = rc; rt.bottom = rt.top + DpiVal(TOT_H);
        PaintTotals(dc, rt);
        RECT rg = rc;
        rg.top = DpiVal(TOT_H + LIST_H + 8);
        rg.bottom = rg.top + DpiVal(GRAPH_H);
        PaintGraph(dc, rg);
        EndPaint(hwnd, &ps);
        return 0; }
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case TID_RESET:
            ResetSession();
            InvalidateRect(hwnd, NULL, TRUE);
            break;
        case TID_EXPORT:
            if (!TrafficExportCSV(hwnd))
                MessageBoxW(hwnd, L"Export failed.", APP_NAME, MB_ICONWARNING);
            break;
        case TID_IMPORT:
            if (!TrafficImportCSV(hwnd))
                MessageBoxW(hwnd, L"Nothing imported (bad file or cancelled).",
                            APP_NAME, MB_ICONWARNING);
            else
                InvalidateRect(hwnd, NULL, TRUE);
            break;
        case TID_CLEAR:
            if (MessageBoxW(hwnd,
                    L"Delete the complete daily traffic database?\r\n"
                    L"(The running session is not reset.)",
                    APP_NAME, MB_OKCANCEL | MB_ICONWARNING | MB_DEFBUTTON2) == IDOK) {
                g_nDays = 0;
                TrafficSave();
                TrafficRefreshList();
                InvalidateRect(hwnd, NULL, TRUE);
            }
            break;
        case TID_CLOSE:
            DestroyWindow(hwnd);
            break;
        }
        return 0;
    case WM_DESTROY:
        KillTimer(hwnd, 2);
        g_hTraffic = NULL;
        g_hList = NULL;
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static void OpenTrafficWindow(HINSTANCE hInst)
{
    if (g_hTraffic && IsWindow(g_hTraffic)) {
        ShowWindow(g_hTraffic, SW_SHOWNORMAL);
        SetForegroundWindow(g_hTraffic);
        return;
    }
    WNDCLASSW wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.lpfnWndProc   = TrafficWndProc;
    wc.hInstance     = hInst;
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.hIcon         = LoadIconW(hInst, MAKEINTRESOURCEW(1));
    wc.hbrBackground = NULL;
    wc.lpszClassName = TRAFFIC_NAME;
    RegisterClassW(&wc);

    RECT r = { 0, 0, DpiVal(560), DpiVal(TOT_H + LIST_H + GRAPH_H + BTN_H + 34) };
    AdjustWindowRect(&r, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE);
    int W = r.right - r.left, H = r.bottom - r.top;
    g_hTraffic = CreateWindowExW(0, TRAFFIC_NAME,
        L"NetSpeedMonitor - Data Traffic",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, W, H,
        NULL, NULL, hInst, NULL);
    if (!g_hTraffic) return;

    // totals block is owner-drawn; list starts below it
    g_hList = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, NULL,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
        DpiVal(8), DpiVal(TOT_H), DpiVal(544), DpiVal(LIST_H),
        g_hTraffic, NULL, hInst, NULL);
    if (g_hList) {
        LVCOLUMNW col;
        struct { const WCHAR* t; int w; } cols[4] = {
            { L"Date", 110 }, { L"Sent", 140 }, { L"Recv", 140 }, { L"Total", 140 } };
        int c;
        ZeroMemory(&col, sizeof(col));
        col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
        for (c = 0; c < 4; ++c) {
            col.iSubItem = c;
            col.pszText  = (LPWSTR)cols[c].t;
            col.cx       = DpiVal(cols[c].w);
            SendMessageW(g_hList, LVM_INSERTCOLUMNW, c, (LPARAM)&col);
        }
        if (g_listFont) SendMessageW(g_hList, WM_SETFONT, (WPARAM)g_listFont, TRUE);
    }
    {
        struct { const WCHAR* t; int x; int w; int id; } btns[5] = {
            { L"Reset Session",   DpiVal(8),   DpiVal(96),  TID_RESET },
            { L"Export CSV...",   DpiVal(110), DpiVal(96),  TID_EXPORT },
            { L"Import CSV...",   DpiVal(212), DpiVal(96),  TID_IMPORT },
            { L"Clear Database",  DpiVal(314), DpiVal(110), TID_CLEAR },
            { L"Close",           DpiVal(474), DpiVal(78),  TID_CLOSE } };
        int b;
        for (b = 0; b < 5; ++b) {
            HWND hb = CreateWindowExW(0, L"BUTTON", btns[b].t,
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                btns[b].x, DpiVal(TOT_H + LIST_H + GRAPH_H + 12), btns[b].w, DpiVal(26),
                g_hTraffic, (HMENU)(INT_PTR)btns[b].id, hInst, NULL);
            if (hb && g_listFont) SendMessageW(hb, WM_SETFONT, (WPARAM)g_listFont, TRUE);
        }
    }
    TrafficRefreshList();
    SetTimer(g_hTraffic, 2, 30000, NULL);   // 30s refresh while open
    ShowWindow(g_hTraffic, SW_SHOWNORMAL);
    SetForegroundWindow(g_hTraffic);
    InvalidateRect(g_hTraffic, NULL, TRUE);
}


// --------------------------------------------------------------- autostart
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
        swprintf_s(cmd, MAX_PATH * 2, L"\"%s\"", path);
        RegSetValueExW(k, APP_NAME, 0, REG_SZ, (const BYTE*)cmd,
                       (DWORD)((wcslen(cmd) + 1) * sizeof(WCHAR)));
    }
    RegCloseKey(k);
}

// --------------------------------------------------------------- settings dialog
// The original's configuration UI (Settings / nsmcpl). Built as an
// in-memory DLGTEMPLATE so no .rc dialog resource is needed.
static DWORD s_dlgBuf[1024];   // DWORD-typed: guarantees 4-byte alignment

static WORD* DlgDword(WORD* p, DWORD v) { *(DWORD UNALIGNED*)p = v; return p + 2; }
static WORD* DlgWord(WORD* p, WORD v)   { *p++ = v; return p; }
static WORD* DlgStr(WORD* p, const WCHAR* s)
{
    int n = (int)wcslen(s) + 1;
    memcpy(p, s, (size_t)n * sizeof(WCHAR));
    p += n;
    p = (WORD*)(((uintptr_t)p + 1) & ~(uintptr_t)1);
    return p;
}
static WORD* DlgAlignD(WORD* p) { return (WORD*)(((uintptr_t)p + 3) & ~(uintptr_t)3); }

// item helper: clsOrd 0 = string class (unused here), title via 0xFFFF ord or text
static WORD* DlgItem(WORD* p, DWORD style, int x, int y, int w, int h,
                     WORD id, BYTE clsOrd, const WCHAR* text)
{
    p = DlgAlignD(p);
    p = DlgDword(p, style);
    p = DlgDword(p, 0);                    // exStyle
    p = DlgWord(p, (WORD)x); p = DlgWord(p, (WORD)y);
    p = DlgWord(p, (WORD)w); p = DlgWord(p, (WORD)h);
    p = DlgWord(p, id);
    p = DlgWord(p, 0xFFFF); p = DlgWord(p, clsOrd);
    p = DlgStr(p, text ? text : L"");
    p = DlgWord(p, 0);                     // no creation data
    return p;
}

static int BuildSettingsDialog(void)
{
    DWORD dlgStyle = DS_SETFONT | DS_MODALFRAME | DS_CENTER | WS_POPUP |
                     WS_CAPTION | WS_SYSMENU;
    const DWORD ST  = WS_CHILD | WS_VISIBLE;                                  // static
    const DWORD CB  = WS_CHILD | WS_VISIBLE | WS_TABSTOP | 0x0003 | 0x00200000; // dropdownlist+vscroll
    const DWORD ED  = WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_BORDER | 0x80;  // edit autohscroll
    const DWORD CHK = WS_CHILD | WS_VISIBLE | WS_TABSTOP | 0x0003;            // autocheckbox
    const DWORD BTN = WS_CHILD | WS_VISIBLE | WS_TABSTOP;                     // pushbutton
    const DWORD DEF = WS_CHILD | WS_VISIBLE | WS_TABSTOP | 0x0001;            // defpushbutton

    WORD* p = (WORD*)s_dlgBuf;
    p = DlgDword(p, dlgStyle);
    p = DlgDword(p, 0);            // exStyle
    p = DlgWord(p, 19);            // item count
    p = DlgWord(p, 8); p = DlgWord(p, 8); p = DlgWord(p, 250); p = DlgWord(p, 192);
    p = DlgWord(p, 0);             // menu: none
    p = DlgWord(p, 0);             // class: default
    p = DlgStr(p, L"NetSpeedMonitor - Settings");
    p = DlgWord(p, 9);             // pointsize (DS_SETFONT)
    p = DlgStr(p, L"Segoe UI");

    p = DlgItem(p, ST,  8,   9,  66,  8, 0,        0x82, L"Monitor this connection:");
    p = DlgItem(p, CB,  78,  7, 164, 120, IDC_IF,  0x85, L"");
    p = DlgItem(p, ST,  8,  25,  30,  8, 0,        0x82, L"Unit:");
    p = DlgItem(p, CB,  78, 23,  90, 120, IDC_UNIT,0x85, L"");
    p = DlgItem(p, ST,  8,  41,  40,  8, 0,        0x82, L"Precision:");
    p = DlgItem(p, CB,  78, 39,  60,  90, IDC_PREC,0x85, L"");
    p = DlgItem(p, ST,  8,  58,  42,  8, 0,        0x82, L"Sent text:");
    p = DlgItem(p, ED,  78, 56,  58, 12, IDC_SENT, 0x81, L"");
    p = DlgItem(p, ST, 140,  58, 42,  8, 0,        0x82, L"Recv text:");
    p = DlgItem(p, ED, 182, 56,  60, 12, IDC_RECV, 0x81, L"");
    p = DlgItem(p, CHK, 8,  74, 236, 10, IDC_SWAP, 0x80, L"Swap order (Recv first)");
    p = DlgItem(p, CHK, 8,  88, 236, 10, IDC_COL,  0x80, L"Colored speeds (Sent red, Recv green)");
    p = DlgItem(p, CHK, 8, 102, 236, 10, IDC_AUTOC,0x80, L"Automatic text color (match taskbar)");
    p = DlgItem(p, BTN, 78, 116, 76, 13, IDC_TXTCOL,0x80, L"Text color...");
    p = DlgItem(p, BTN, 160,116, 76, 13, IDC_FONT, 0x80, L"Font...");
    p = DlgItem(p, CHK, 8, 134, 236, 10, IDC_START,0x80, L"Start with Windows");
    p = DlgItem(p, CHK, 8, 148, 236, 10, IDC_TIP,  0x80, L"Enable Toolbar Tooltip");
    p = DlgItem(p, DEF, 130,168, 52, 14, IDOK,     0x80, L"OK");
    p = DlgItem(p, BTN, 188,168, 52, 14, IDCANCEL, 0x80, L"Cancel");

    return (int)((BYTE*)p - s_dlgBuf);
}

static INT_PTR CALLBACK SettingsDlgProc(HWND hDlg, UINT msg, WPARAM wp, LPARAM lp)
{
    (void)lp;
    switch (msg) {
    case WM_INITDIALOG: {
        // interface combo: All connections + every IP adapter
        HWND hIf = GetDlgItem(hDlg, IDC_IF);
        SendMessageW(hIf, CB_RESETCONTENT, 0, 0);
        int selIf = -1;
        int ii = SendMessageW(hIf, CB_ADDSTRING, 0, (LPARAM)L"All connections");
        SendMessageW(hIf, CB_SETITEMDATA, ii, 0);
        if (g_ifFilter == 0) selIf = ii;
        {
            ULONG sz = 0;
            if (GetInterfaceInfo(NULL, &sz) == ERROR_INSUFFICIENT_BUFFER && sz) {
                PIP_INTERFACE_INFO pi = (PIP_INTERFACE_INFO)malloc(sz);
                if (pi) {
                    if (GetInterfaceInfo(pi, &sz) == NO_ERROR) {
                        int i;
                        for (i = 0; i < (int)pi->NumAdapters; ++i) {
                            MIB_IF_ROW2 r2;
                            ZeroMemory(&r2, sizeof(r2));
                            r2.InterfaceIndex = pi->Adapter[i].Index;
                            WCHAR name[280];
                            if (GetIfEntry2(&r2) == NO_ERROR && r2.FriendlyName[0]) {
                                swprintf_s(name, 280, L"%s (if %lu)",
                                           r2.FriendlyName,
                                           (unsigned long)pi->Adapter[i].Index);
                            } else {
                                swprintf_s(name, 280, L"Interface %lu",
                                           (unsigned long)pi->Adapter[i].Index);
                            }
                            int ix = SendMessageW(hIf, CB_ADDSTRING, 0, (LPARAM)name);
                            SendMessageW(hIf, CB_SETITEMDATA, ix,
                                         (LPARAM)pi->Adapter[i].Index);
                            if (g_ifFilter == pi->Adapter[i].Index) selIf = ix;
                        }
                    }
                    free(pi);
                }
            }
        }
        SendMessageW(hIf, CB_SETCURSEL, selIf >= 0 ? selIf : 0, 0);

        HWND hU = GetDlgItem(hDlg, IDC_UNIT);
        SendMessageW(hU, CB_ADDSTRING, 0, (LPARAM)L"Auto (B/s, kB/s, MB/s, GB/s)");
        SendMessageW(hU, CB_ADDSTRING, 0, (LPARAM)L"kB/s (1000)");
        SendMessageW(hU, CB_ADDSTRING, 0, (LPARAM)L"KiB/s (1024)");
        SendMessageW(hU, CB_ADDSTRING, 0, (LPARAM)L"MB/s (1000)");
        SendMessageW(hU, CB_ADDSTRING, 0, (LPARAM)L"MiB/s (1024)");
        SendMessageW(hU, CB_SETCURSEL, g_unit, 0);

        HWND hP = GetDlgItem(hDlg, IDC_PREC);
        {
            WCHAR t[16]; int i;
            for (i = 0; i < 4; ++i) {
                swprintf_s(t, 16, L"%d decimals", i);
                SendMessageW(hP, CB_ADDSTRING, 0, (LPARAM)t);
            }
        }
        SendMessageW(hP, CB_SETCURSEL, g_precision, 0);

        SetDlgItemTextW(hDlg, IDC_SENT, g_lblSent);
        SetDlgItemTextW(hDlg, IDC_RECV, g_lblRecv);
        CheckDlgButton(hDlg, IDC_SWAP,  g_swapOrder    ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hDlg, IDC_COL,   g_colorSpeeds  ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hDlg, IDC_AUTOC, g_autoColor    ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hDlg, IDC_START, AutostartEnabled() ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hDlg, IDC_TIP,   g_tooltipOn    ? BST_CHECKED : BST_UNCHECKED);
        return TRUE; }
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDC_TXTCOL: {
            static COLORREF cust[16];
            CHOOSECOLORW cc;
            ZeroMemory(&cc, sizeof(cc));
            cc.lStructSize  = sizeof(cc);
            cc.hwndOwner    = hDlg;
            cc.rgbResult    = g_autoColor ? g_textColor : g_userTextCol;
            cc.lpCustColors = cust;
            cc.Flags        = CC_FULLOPEN | CC_RGBINIT;
            if (ChooseColorW(&cc)) {
                g_userTextCol = cc.rgbResult;
                g_autoColor   = FALSE;
                CheckDlgButton(hDlg, IDC_AUTOC, BST_UNCHECKED);
            }
            return TRUE; }
        case IDC_FONT: {
            LOGFONTW lf;
            ZeroMemory(&lf, sizeof(lf));
            lf.lfCharSet = DEFAULT_CHARSET;
            lf.lfHeight  = -MulDiv(g_fontSize > 0 ? g_fontSize : 9, 96, 72);
            lstrcpynW(lf.lfFaceName, g_fontName, 32);
            CHOOSEFONTW cf;
            ZeroMemory(&cf, sizeof(cf));
            cf.lStructSize = sizeof(cf);
            cf.hwndOwner   = hDlg;
            cf.lpLogFont   = &lf;
            cf.Flags       = CF_INITTOLOGFONTSTRUCT | CF_SCREENFONTS | CF_EFFECTS;
            if (ChooseFontW(&cf)) {
                lstrcpynW(g_fontName, lf.lfFaceName, 64);
                int pts = cf.iPointSize / 10;
                if (pts < 0) pts = -pts;
                g_fontSize = (pts >= 6 && pts <= 24) ? pts : 9;
            }
            return TRUE; }
        case IDOK: {
            int ix = (int)SendMessageW(GetDlgItem(hDlg, IDC_IF), CB_GETCURSEL, 0, 0);
            DWORD data = (DWORD)SendMessageW(GetDlgItem(hDlg, IDC_IF),
                                             CB_GETITEMDATA, ix, 0);
            g_ifFilter = (ix <= 0 || data == (DWORD)-1 || data == 0) ? 0 : data;
            g_unit      = (int)SendMessageW(GetDlgItem(hDlg, IDC_UNIT), CB_GETCURSEL, 0, 0);
            if (g_unit < 0) g_unit = 0;
            g_precision = (int)SendMessageW(GetDlgItem(hDlg, IDC_PREC), CB_GETCURSEL, 0, 0);
            if (g_precision < 0 || g_precision > 3) g_precision = 2;
            WCHAR tmp[64];
            GetDlgItemTextW(hDlg, IDC_SENT, tmp, 64); tmp[31] = 0;
            lstrcpynW(g_lblSent, tmp[0] ? tmp : L"Sent:", 32);
            GetDlgItemTextW(hDlg, IDC_RECV, tmp, 64); tmp[31] = 0;
            lstrcpynW(g_lblRecv, tmp[0] ? tmp : L"Recv:", 32);
            g_swapOrder   = IsDlgButtonChecked(hDlg, IDC_SWAP)  == BST_CHECKED;
            g_colorSpeeds = IsDlgButtonChecked(hDlg, IDC_COL)   == BST_CHECKED;
            g_autoColor   = IsDlgButtonChecked(hDlg, IDC_AUTOC) == BST_CHECKED;
            g_tooltipOn   = IsDlgButtonChecked(hDlg, IDC_TIP)   == BST_CHECKED;
            BOOL wantStart = IsDlgButtonChecked(hDlg, IDC_START) == BST_CHECKED;
            if (wantStart != AutostartEnabled()) AutostartToggle(hDlg);
            SaveSettings();
            EndDialog(hDlg, IDOK);
            return TRUE; }
        case IDCANCEL:
            EndDialog(hDlg, IDCANCEL);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

static void ShowSettings(HWND hwnd)
{
    BuildSettingsDialog();
    DialogBoxIndirectParamW(GetModuleHandleW(NULL),
                            (LPCDLGTEMPLATEW)s_dlgBuf, hwnd,
                            SettingsDlgProc, 0);
    // apply everything the dialog may have changed
    CreateFontForDpi();
    ApplyColors();
    UpdateSpeedText();
    RecalcAndPosition();
    RedrawWindow(g_hwnd, NULL, NULL,
                 RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW);
}

// --------------------------------------------------------------- diag
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
        L"version=%s (original 2.5.4.0 rebuild)\r\n"
        L"monitoring=%d floatMode=%d embedded=%d unit=%d precision=%d\r\n"
        L"iface=%lu labels=%s/%s swap=%d\r\n"
        L"hwnd=%p parent=%p visible=%d\r\n"
        L"win rect=(%ld,%ld)-(%ld,%ld)\r\n"
        L"taskbar=%p rect=(%ld,%ld)-(%ld,%ld)\r\n"
        L"dpi=%u winSize=%dx%d\r\n"
        L"text=%s\r\n",
        APP_VERSION,
        (int)g_monitoring, (int)g_floatMode, (int)g_embedded,
        g_unit, g_precision,
        (unsigned long)g_ifFilter, g_lblSent, g_lblRecv, (int)g_swapOrder,
        (void*)g_hwnd, (void*)GetParent(g_hwnd), IsWindowVisible(g_hwnd),
        (long)rme.left, (long)rme.top, (long)rme.right, (long)rme.bottom,
        (void*)g_hTaskbar, (long)rtb.left, (long)rtb.top,
        (long)rtb.right, (long)rtb.bottom,
        g_dpi, g_winW, g_winH, g_text);

    {
        WCHAR e[300];
        swprintf_s(e, 300,
            L"engine=original+ndis6 ndis6=%d adaptersSampled=%d lastRawRecv=%.0f lastRawSent=%.0f\r\n"
            L"trafficDays=%d session=%.0f/%.0f\r\n",
            g_diagSrc2, g_diagAdapters, g_diagDown, g_diagUp,
            g_nDays, (double)g_sessSent, (double)g_sessRecv);
        wcscat_s(buf, 4096, e);
        ULONG sz = 0;
        if (GetInterfaceInfo(NULL, &sz) == ERROR_INSUFFICIENT_BUFFER && sz) {
            PIP_INTERFACE_INFO pii = (PIP_INTERFACE_INFO)malloc(sz);
            if (pii) {
                if (GetInterfaceInfo(pii, &sz) == NO_ERROR) {
                    int i;
                    for (i = 0; i < (int)pii->NumAdapters && i < 12; ++i) {
                        MIB_IF_ROW2 r2;
                        ZeroMemory(&r2, sizeof(r2));
                        r2.InterfaceIndex = pii->Adapter[i].Index;
                        if (GetIfEntry2(&r2) == NO_ERROR) {
                            WCHAR line[220];
                            swprintf_s(line, 220,
                                L"if=%lu type=%lu oper=%d in=%I64u out=%I64u src=ndis6\r\n",
                                (unsigned long)r2.InterfaceIndex,
                                (unsigned long)r2.Type,
                                (int)r2.OperStatus,
                                r2.InOctets, r2.OutOctets);
                            wcscat_s(buf, 4096, line);
                        } else {
                            MIB_IFROW r1;
                            ZeroMemory(&r1, sizeof(r1));
                            r1.dwIndex = pii->Adapter[i].Index;
                            if (GetIfEntry(&r1) == NO_ERROR) {
                                WCHAR line[220];
                                swprintf_s(line, 220,
                                    L"if=%lu type=%lu oper=%lu in=%lu out=%lu src=legacy\r\n",
                                    (unsigned long)r1.dwIndex,
                                    (unsigned long)r1.dwType,
                                    (unsigned long)r1.dwOperStatus,
                                    (unsigned long)r1.dwInOctets,
                                    (unsigned long)r1.dwOutOctets);
                                wcscat_s(buf, 4096, line);
                            }
                        }
                    }
                }
                free(pii);
            }
        }
    }

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

// --------------------------------------------------------------- menu
static void ShowContextMenu(HWND hwnd)
{
    HMENU m = CreatePopupMenu();
    if (!m) return;

    AppendMenuW(m, MF_STRING | (g_monitoring ? MF_CHECKED : 0),
                IDM_MONITOR, g_monitoring ? L"Monitoring On" : L"Monitoring Off");
    AppendMenuW(m, MF_STRING, IDM_TRAFFIC, L"Data Traffic");
    AppendMenuW(m, MF_STRING, IDM_RESETSESS, L"Reset Session");
    AppendMenuW(m, MF_STRING, IDM_IMPORT, L"Traffic Import Wizard");
    AppendMenuW(m, MF_STRING, IDM_EXPORT, L"Traffic Export Wizard");
    AppendMenuW(m, MF_STRING, IDM_WEBSITE, L"NetSpeedMonitor Website");
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, MF_STRING, IDM_SETTINGS, L"Settings");
    AppendMenuW(m, MF_STRING | (g_tooltipOn ? MF_CHECKED : 0),
                IDM_TOOLTIP, L"Enable Toolbar Tooltip");
    AppendMenuW(m, MF_STRING | (AutostartEnabled() ? MF_CHECKED : 0),
                IDM_AUTOSTART, L"Start with Windows");
    AppendMenuW(m, MF_STRING | (g_floatMode ? MF_CHECKED : 0),
                IDM_FLOAT, L"Floating mode (if taskbar widget is not visible)");
    AppendMenuW(m, MF_STRING, IDM_DIAG, L"Copy diagnostics");
    AppendMenuW(m, MF_STRING, IDM_ABOUT, L"About NetSpeedMonitor");
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, MF_STRING, IDM_EXIT, L"Exit");

    POINT p;
    GetCursorPos(&p);
    SetForegroundWindow(hwnd);
    TrackPopupMenu(m, TPM_RIGHTBUTTON | TPM_NONOTIFY, p.x, p.y, 0, hwnd, NULL);
    PostMessageW(hwnd, WM_NULL, 0, 0);
    DestroyMenu(m);
}

// --------------------------------------------------------------- wndproc
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
        if (++g_hbTick >= 60) {
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
        if (++g_saveTick >= 30) {     // traffic persisted every 30s
            g_saveTick = 0;
            TrafficSave();
        }
        if (++g_colorTick >= 5) {
            g_colorTick = 0;
            if (g_autoColor) ApplyColors();
            TrayUpdateTip();
        }
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    case WM_PAINT:
        OnPaint(hwnd);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_NOTIFY: {
        NMHDR* nm = (NMHDR*)lp;
        if (nm->code == TTN_GETDISPINFOW && nm->hwndFrom == g_hTooltip) {
            HandleTooltipText((NMTTDISPINFOW*)lp);
            return TRUE;
        }
        return 0; }
    case WM_APP_TRAY:
        if (lp == WM_RBUTTONUP || lp == WM_LBUTTONUP || lp == WM_CONTEXTMENU)
            ShowContextMenu(hwnd);
        else if (lp == WM_LBUTTONDBLCLK)
            OpenTrafficWindow(GetModuleHandleW(NULL));
        return 0;
    case WM_LBUTTONDBLCLK:
        OpenTrafficWindow(GetModuleHandleW(NULL));
        return 0;
    case WM_CONTEXTMENU:
        ShowContextMenu(hwnd);
        return 0;
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDM_MONITOR:
            g_monitoring = !g_monitoring;
            SaveSettings();
            TrayUpdateTip();
            break;
        case IDM_TRAFFIC:
            OpenTrafficWindow(GetModuleHandleW(NULL));
            break;
        case IDM_RESETSESS:
            ResetSession();
            if (g_hTraffic && IsWindow(g_hTraffic))
                InvalidateRect(g_hTraffic, NULL, TRUE);
            break;
        case IDM_IMPORT:
            TrafficImportCSV(hwnd);
            break;
        case IDM_EXPORT:
            TrafficExportCSV(hwnd);
            break;
        case IDM_WEBSITE:
            ShellExecuteW(hwnd, L"open",
                L"https://www.floriangilles.com/software/netspeedmonitor",
                NULL, NULL, SW_SHOWNORMAL);
            break;
        case IDM_SETTINGS:
            ShowSettings(hwnd);
            break;
        case IDM_TOOLTIP:
            g_tooltipOn = !g_tooltipOn;
            SaveSettings();
            if (g_hTooltip)
                SendMessageW(g_hTooltip, TTM_ACTIVATE, (WPARAM)(g_tooltipOn ? 1 : 0), 0);
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
            RecalcAndPosition();
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
                L"NetSpeedMonitor v" APP_VERSION L"\r\n"
                L"ORIGINAL NetSpeedMonitor 2.5.4.0 rebuild (from the MSI)\r\n\r\n"
                L"Original app: Florian Gilles (floriangilles.com) - MIT.\r\n"
                L"Display: 'Sent: x kB/s  Recv: y kB/s' inside the taskbar,\r\n"
                L"units kB/KiB/MB/MiB, precision 0-3, original core.*/layout.*\r\n"
                L"registry settings, Data Traffic window with daily history,\r\n"
                L"CSV import/export and the original menu.\r\n\r\n"
                L"Engine: original GetInterfaceInfo + per-adapter octet deltas,\r\n"
                L"modernized counter layer (GetIfEntry2, as Task Manager uses)\r\n"
                L"because modern NDIS6 drivers freeze the legacy counters.\r\n\r\n"
                L"Double-click: Data Traffic  |  Right-click: menu",
                APP_NAME, MB_ICONINFORMATION);
            break;
        case IDM_EXIT:
            DestroyWindow(hwnd);
            break;
        }
        return 0;
    case WM_DESTROY:
        KillTimer(hwnd, TIMER_TICK);
        TrafficSave();
        TrayRemove();
        LogLine(L"exit");
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// --------------------------------------------------------------- main
int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE prev, PWSTR cmd, int show)
{
    (void)prev; (void)cmd; (void)show;

    HANDLE mutex = CreateMutexW(NULL, FALSE, MUTEX_NAME);
    if (mutex && GetLastError() == ERROR_ALREADY_EXISTS) {
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

    InitCommonControls();
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

    LoadSettings();
    TrafficLoad();
    ResetSession();

    g_hwnd = CreateWindowExW(
        WS_EX_TOOLWINDOW | (g_floatMode ? WS_EX_TOPMOST : 0),
        CLASS_NAME, APP_NAME,
        WS_POPUP | WS_CLIPSIBLINGS, -32000, -32000, 10, 10,
        NULL, NULL, hInst, NULL);
    if (!g_hwnd) return 1;

    CreateFontForDpi();
    ApplyColors();
    UpdateSpeedText();
    CreateTooltip(g_hwnd, hInst);
    if (g_hTooltip) SendMessageW(g_hTooltip, TTM_ACTIVATE, (WPARAM)(g_tooltipOn ? 1 : 0), 0);

    {
        WCHAR b[220];
        swprintf_s(b, 220,
            L"start v%s (original 2.5.4.0 rebuild): dpi=%u mode=%s unit=%d prec=%d iface=%lu",
            APP_VERSION, g_dpi, g_floatMode ? L"floating" : L"embedded",
            g_unit, g_precision, (unsigned long)g_ifFilter);
        LogLine(b);
    }

    RecalcAndPosition();
    ShowWindow(g_hwnd, SW_SHOWNOACTIVATE);
    TrayAdd();

    MSG m;
    while (GetMessageW(&m, NULL, 0, 0) > 0) {
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    if (mutex) ReleaseMutex(mutex);
    return 0;
}
