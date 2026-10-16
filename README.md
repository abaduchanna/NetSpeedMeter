# NetSpeedMeter — NetSpeedMonitor for Windows 10 / 11

## ✅ NEW in v2.5.0 — ONE row, black background

1. **ONE row** — the v2.4.0 four-row stacked layout is REMOVED (owner:
   "ek row chaiye sab ek sath nahi"). Every units mode now draws a single
   taskbar-height row: `↑ 350.0 KB/s  ↓ 2.35 MB/s` style. Upload and
   download stay **separate readouts** — each with its own arrow and color
   (upload red, download green) — never attached into one value, never
   stacked into multiple rows. The window is back to normal taskbar height.
2. **Units menu: both families, one row each** — BYTES family (capital B):
   `Auto (B/s, KB/s, MB/s)`, `KB/s`, `MB/s`; BITS family (lowercase b):
   `Auto (b/s, Kb/s, Mb/s)`, `Kb/s`, `Mb/s`. The 8-bits-equal-1-byte rule is
   built in: KB/s = Kb/s ÷ 8 and MB/s = Mb/s ÷ 8. **Default = BYTES Auto**
   (the unit your browser shows for file downloads).
3. **Black background** — the Dark theme now uses pure black (`#000000`)
   and Dark is the default theme (owner: "bg black").
   `Auto (match taskbar)` and `Light` remain in the Theme menu, and theme
   changes still apply and persist instantly.
4. Old saved settings migrate automatically: legacy `sep` / `both` / `auto`
   unit values and any saved theme keep working (unknown unit values fall
   back to BYTES Auto).

## v2.4.0 (superseded by v2.5.0) — four separate rows, bits + bytes families

1. ~~Four stacked rows (Mb/s, Kb/s, MB/s, KB/s)~~ — replaced by the v2.5.0
   one-row layout above.
2. Kept: the BYTES vs BITS distinction with correct casing (MB/s capital B,
   Mb/s lowercase b) — now available as one-row modes in the Units menu.

## v2.3.0 (superseded) — separate Kb/s + Mb/s lines, small "b" everywhere

1. **"Both" is REPLACED by "Kb/s + Mb/s (separate lines)"** — the v2.2.0
   mashed one-line format (`↓ 1582.4 KB/s (1.54 MB/s)`) is gone. The widget
   now draws **two separate lines**: Mb/s on top, Kb/s below, each with its
   own up/down arrows. This separate-lines mode is the **default**; the
   window automatically grows to two text rows (and is clamped to the real
   taskbar height, so it can never overflow). The other modes
   (`Auto`, `Kb/s`, `Mb/s`) are still in the menu — everything stays
   persisted in `HKCU\Software\NetSpeedMonitor` (the value is stored as
   `sep` now; the legacy `both`/`auto` values migrate automatically).
2. **Small b everywhere** — all unit strings now use the lowercase-b style:
   `b/s`, `Kb/s`, `Mb/s`, `Gb/s` (menu labels, formats, tray tooltip,
   About box). No capital-B `KB/s` / `MB/s` anywhere.

## v2.2.0 — Both units + theme toggle fix (superseded format, kept fixes)

1. ~~Units: "Both (KB/s + MB/s)"~~ — replaced by the v2.3.0 separate-lines
   mode above.
2. **Theme toggle fix** — the `Auto (match taskbar)` sampler used to pass
   screen coordinates into the taskbar's client-area DC, so `GetPixel`
   returned nothing and Auto silently kept the startup color forever. The
   sampler now uses proper client coordinates with a composited-screen
   fallback (works with XAML-composited taskbars), and switching
   `Dark`/`Light` repaints immediately instead of waiting for the next
   timer tick. Dark/Light also contrast-derive their text and arrow colors
   automatically, so both themes stay readable on any taskbar.

## v2.1.1 — a real SETUP INSTALLER

Downloads now come in two flavors on the [Releases](../../releases) page:

- **`NetSpeedMeter-2.5.0-x64-Setup.exe`** (recommended) — proper installer:
  per-user install to `%LOCALAPPDATA%\NetSpeedMeter` (**no admin/UAC**),
  Start Menu shortcut, optional Desktop shortcut, optional
  **start-with-Windows** checkbox (writes the HKCU Run value for you), and a
  clean uninstaller (your speed settings are kept). App code is unchanged
  from v2.1.0 — same exe, same features, now with Explorer file-properties
  version info (2.5.0.0).
- **`NetSpeedMonitor-v2.5.0-x64.exe`** — the classic portable single exe,
  as before.

## v2.1.0 — Units menu, Theme menu, colored speeds (up RED, down GREEN)

Right-click the tray icon (or the widget) — three new controls at the top of
the menu, all persisted in `HKCU\Software\NetSpeedMonitor`:

1. **Units** — `Auto` (scales B/s → KB/s → MB/s → GB/s automatically, the old
   behavior), `KB/s` (always), or `MB/s` (always, shown with 2 decimals, e.g.
   `↓ 2.47 MB/s`). Applies instantly, survives restarts.
2. **Theme** — `Auto (match taskbar)` (pixel-samples the taskbar background
   every 5 s so the widget is seamless — the old behavior), `Dark` (fixed
   near-black bar), or `Light` (fixed near-white bar). Dark/Light are perfect
   for the floating bar, and the floating bar now uses the theme background
   instead of a black guess.
3. **Colored speeds** — checked by default: the upload arrow + value is drawn
   **RED** (↑) and the download arrow + value is drawn **GREEN** (↓) — the
   universal convention. Arrow colors automatically pick bright variants on
   dark backgrounds and dark variants on light backgrounds so they always
   stay readable. Uncheck for the classic single-color look.

## v2.0.2 — floating mode that is ACTUALLY topmost + one-paste diagnostics

v2.0.1 users reported: **"Floating mode is checked but still nothing shows."**
Root cause found — two real bugs:

1. **`WS_EX_TOPMOST` via `SetWindowLongPtr()` does not re-band a window.**
   Windows only honors the topmost layer when it is applied through
   `SetWindowPos(..., HWND_TOPMOST, ...)`. The v2.0.1 floating bar was moved
   to the right spot but stayed in the *normal* z-band — hidden behind every
   open window. v2.0.2 calls `SetWindowPos(HWND_TOPMOST, x, y, w, h)` every
   second, which positions **and** re-bands in one call. The floating bar now
   stays on top of everything, just above the taskbar, and it is applied
   instantly when you toggle the menu item (no 1-second wait).
2. **A fresh start in floating mode never found the taskbar** — the
   positioning code depended on a handle that is only resolved in embedded
   mode, so `GetWindowRect(NULL)` failed every tick and the bar stayed parked
   offscreen at (−32000,−32000) forever. v2.0.2 resolves `Shell_TrayWnd` in
   both modes and falls back to the work area if the taskbar rect cannot be
   measured — the bar can no longer strand offscreen.

Also new in v2.0.2:

- **“Copy diagnostics” menu item** — right-click the tray icon → *Copy
  diagnostics (bug reports)* → version, mode, window/taskbar rects,
  visibility and the last 12 log lines land on your clipboard for a
  one-paste bug report.
- **60-second heartbeat** in `%LOCALAPPDATA%\NetSpeedMonitor\log.txt` with
  the full window state, so “invisible” reports can be diagnosed from the
  log alone.

## v2.0.1 — visibility hardening: tray icon + never-buried widget

Some Windows 11 machines reported v2.0.0 as **"running but invisible"** — the
process lived, but nothing appeared in the taskbar and there was no tray icon
either (the old instance could not even be exited normally). v2.0.1 fixes that
class of problem:

1. **Tray icon is always added** — NetSpeedMonitor now shows up in the tray
   (the `^` hidden-icons chevron near the clock). Right-click it for the menu
   (Task Manager / Start with Windows / Floating mode / About / Exit); hover
   shows the live speeds. The app can never be "lost" again.
2. **Never-buried widget** — every second the widget re-asserts its z-order
   (`HWND_TOP` among taskbar children) and visibility, so the taskbar's
   composited (XAML) surface cannot permanently cover it. Show order now
   matches TrafficMonitor exactly: `SetParent` → `ShowWindow` → position.
3. **Floating mode** — if embedding still does not display on some future
   Windows 11 build, right-click the tray icon (or widget) → **Floating
   mode**: the bar runs topmost, docked just above the taskbar. The choice
   is saved in `HKCU\Software\NetSpeedMonitor` and survives restarts.
4. **Second-instance dialog with an escape hatch** — starting a second copy
   now explains where the tray icon lives and offers **OK = open Task
   Manager** so an older stuck instance can be ended in one click.
5. **Diagnostic log** — `%LOCALAPPDATA%\NetSpeedMonitor\log.txt` records the
   taskbar handle, `SetParent` result, position and visibility transitions,
   so any remaining "invisible" report can be diagnosed from the log alone.

**Upgrading from a stuck v2.0.0:** run the new exe → the dialog appears →
click **OK** (opens Task Manager) → end `NetSpeedMonitor.exe` → run the new
exe again. Or simply end it in Task Manager yourself first.

## v2.0.0 — NetSpeedMonitor.exe: the speed monitor that WORKS inside the Windows 11 taskbar

The original NetSpeedMonitor cannot ever appear in the Windows 11 taskbar —
Microsoft removed taskbar *Toolbars* (the deskband mechanism the 2012 DLL used),
so no patched installer can fix that (details below).

**v2.0.0 fixes this properly.** `src/main.c` is a small, dependency-free Win32
application that embeds into the taskbar using the same technique as
[TrafficMonitor](https://github.com/zhongyang219/TrafficMonitor) (by zhongyang219):

1. `FindWindow("Shell_TrayWnd")` — get the taskbar window
2. Create an `WS_EX_TOOLWINDOW` window (no taskbar/alt-tab button)
3. `SetParent(mine, taskbar)` — live inside the taskbar (this is standard Win32,
   still fully supported on Windows 11 — no shell extension, no DLL registration)
4. Position: left of `TrayNotifyWnd` (the clock/tray area), vertically centred
   on the Start-button height — the exact placement TrafficMonitor uses on Win11
5. 1-second timer: speed update + reposition + **auto-recover when Explorer
   restarts** (re-finds the taskbar and re-embeds)
6. Background colour is sampled from the rendered taskbar (seamless look,
   adapts to dark/light theme and accent colours), text colour auto-contrast

**Features**

- Live upload/download speed (↑ / ↓), updated every second (64-bit interface
  counters via `GetIfTable` with wrap-safe deltas, all connected non-loopback adapters)
- Per-monitor DPI aware (font and size scale with the taskbar)
- Double-click → Task Manager
- Right-click menu → **Start with Windows** (HKCU Run toggle, no admin needed),
  About, Exit
- Single instance; floating topmost fallback if embedding is refused
- ~25 KB exe, no installer required, no background service, no telemetry

**Install / run**

1. Download `NetSpeedMeter-2.3.0-x64-Setup.exe` from
   [Releases](../../releases) and run it — that's it. (Prefer portable?
   Grab `NetSpeedMonitor-v2.3.0-x64.exe` instead and skip to step 3.)
2. The installer adds a Start Menu shortcut and, if you tick the box, the
   start-with-Windows entry — no admin rights needed at any point.
3. Run it — the speed display appears in the taskbar, just left of the clock
4. Portable users: right-click it → *Start with Windows* if you want it at
   every login (the installer can do this for you).

Works on **Windows 10 and Windows 11** (and Windows Server equivalents).
To remove: right-click → Exit, and delete the exe (plus the
`HKCU\...\Run\NetSpeedMonitor` value if you enabled autostart).

**Build from source** (see `.github/workflows/windows.yml` for the exact CI):

```
rc icon.rc
cl /O2 /W3 /DUNICODE /D_UNICODE main.c icon.res /Fe:NetSpeedMonitor.exe ^
   /link /SUBSYSTEM:WINDOWS user32.lib gdi32.lib shell32.lib advapi32.lib iphlpapi.lib
```

---

**The classic taskbar network speed monitor (NetSpeedMonitor 2.5.4.0) — patched so it
actually installs on Windows 10 and Windows 11.**

Shows live **upload / download speed** directly inside the Windows taskbar, with
per-connection tooltips, daily/monthly traffic statistics and a lightweight
configuration tool. No background service, no telemetry, ~2 MB on disk.

> Windows 7 users can use the [original installer](https://www.floriangilles.com/software/netspeedmonitor/).
> This package exists because the original one **refuses to run on Windows 10/11**.

---

## Why the original installer fails on Windows 10/11

The original 2012 installer (`netspeedmonitor_2_5_4_0_setup.msi`) contains a hard
operating-system gate in its `LaunchCondition` table:

```
(VersionNT = 501) OR (VersionNT = 502) OR (VersionNT = 600) OR (VersionNT = 601)
```

That whitelist allows only **Windows XP, Server 2003, Vista and Windows 7**.
Windows 10 and Windows 11 report `VersionNT = 603`, so the installer aborts with:

> *"Operating System not supported (Windows XP, Windows Server 2003, Windows Vista, or Windows 7)"*

### Why compatibility mode does NOT work

The Windows *"Run this program in compatibility mode for: Windows 7"* option only
changes the Windows version that a normal program sees. An MSI is processed by the
**Windows Installer service (msiexec)**, which always evaluates its internal
`VersionNT` property from the **real** operating system — the shim is never applied.
No compatibility setting can get the original MSI past its own gate.

## What this package changes

Three **same-length text edits** inside the installer database — nothing else:

| # | Table / condition | Original | Patched |
|---|---|---|---|
| 1 | `LaunchCondition` | `(VersionNT = 501) OR … OR (VersionNT = 601)` | `(VersionNT >= 501)` — accepts XP through Windows 11 |
| 2 | Component `Core601` condition | `VersionNT = 601` | `VersionNT>=601` — the newest (Windows 7) build of the taskbar module `nsm.dll` is also selected on Windows 8/8.1/10/11 |
| 3 | Components `Core501/502/600` | `VersionNT = 50x/600` | `NOT VersionNT` — XP and Vista variants disabled, so exactly one matching build installs on any machine |

**The payload files themselves are byte-identical to the originals** (MD5-verified,
see checksums below). No other table, file, shortcut or registry entry was changed.

## Install (Windows 10)

1. Double-click **`netspeedmonitor_2_5_4_0_x64_setup_WIN10-11.msi`**
   (64-bit Windows — what virtually every modern PC runs).
   The x86 MSI is only for 32-bit Windows.
   The x64 MSI registers the 64-bit taskbar module that the 64-bit
   `explorer.exe` can actually load — **do not** use the x86 MSI on 64-bit Windows.
2. Approve the UAC prompt.
3. When setup finishes, the NetSpeedMonitor configuration window opens.
   Choose language / font / tooltip options and click **OK**.
4. Right-click an **empty spot of the taskbar → Toolbars → NetSpeedMonitor**.
   The upload/download speed display appears in the taskbar.

If *NetSpeedMonitor* does not appear under **Toolbars**:

```
regsvr32 /u "C:\Program Files\NetSpeedMonitor\nsm.dll"
regsvr32    "C:\Program Files\NetSpeedMonitor\nsm.dll"
```

Then restart `explorer.exe` (Task Manager → right-click *Windows Explorer* →
Restart) and right-click the taskbar again.

## Windows 11 — why the patched installer alone is not enough

Microsoft removed taskbar **Toolbars** (deskband hosting) when they rewrote the
taskbar in Windows 11. The patched installer installs and registers everything
cleanly on Windows 11, but Windows 11 offers **no supported way** to display a
deskband inside the taskbar anymore. This is a Windows limitation — no patched
installer can bring the Toolbars menu back.

**This is solved by `NetSpeedMonitor.exe` (v2.0.0, see top of this file)** — it
does not use the removed deskband mechanism at all; it embeds via `SetParent`
like TrafficMonitor does. On Windows 10 you can use either the classic MSI
(below) or the new exe; on Windows 11 use the new exe.

## Portable / manual method (no MSI needed)

The `Portable-Backup-Method/` folder contains the same payload plus two scripts:

* **`Install-NetSpeedMonitor.bat`** — right-click → *Run as administrator*
* **`Uninstall-NetSpeedMonitor.bat`**

The install script detects your Windows bitness, copies the matching x64 or x86
`nsm.dll` + `nsmc.exe` into `C:\Program Files\NetSpeedMonitor`, registers the DLL
with `regsvr32` and opens the configuration window. It performs exactly the steps
the original MSI performs — nothing more, nothing less.

## Checksums (MD5)

| File | MD5 |
|---|---|
| `netspeedmonitor_2_5_4_0_x64_setup_WIN10-11.msi` | `1777f737ac35566fc69a3fbe787c1d50` |
| `netspeedmonitor_2_5_4_0_x86_setup_WIN10-11.msi` | `5fba5fb1ea6e2ebda71248b07d93bc42` |
| Full package zip | `2bad64935e7e14b69242d1d213fe280f` |
| `bin/x64/nsm.dll` (payload, byte-identical to original) | `8cf768306bf942a31169fabe70b7f5dd` |
| `bin/x64/nsmc.exe` (payload, byte-identical to original) | `6db419be9ccaf7ebc0a9ac540263c799` |
| `bin/x86/nsm.dll` (payload, byte-identical to original) | `96eaca129ab0e36831f8daf02a140767` |
| `bin/x86/nsmc.exe` (payload, byte-identical to original) | `a0ced760ae56eea57c345b914e507bbe` |

## Credits & license

* **NetSpeedMonitor 2.5.4.0** © [Florian Gilles](http://www.floriangilles.com)
  (donationware). This package only removes the operating-system gate of the
  2012-era installer; the application and its files are untouched.
* The installer patches, portable scripts and documentation in this repository
  are released under the **MIT License** (see [LICENSE](LICENSE)).

---

Packaged by [www.3sverse.com](https://www.3sverse.com)
