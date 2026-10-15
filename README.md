# NetSpeedMeter — NetSpeedMonitor for Windows 10 / 11

## ✅ NEW in v2.0.0 — NetSpeedMonitor.exe: the speed monitor that WORKS inside the Windows 11 taskbar

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
  counters via `GetIfTable2`, all connected non-loopback adapters)
- Per-monitor DPI aware (font and size scale with the taskbar)
- Double-click → Task Manager
- Right-click menu → **Start with Windows** (HKCU Run toggle, no admin needed),
  About, Exit
- Single instance; floating topmost fallback if embedding is refused
- ~25 KB exe, no installer required, no background service, no telemetry

**Install / run**

1. Download `NetSpeedMonitor.exe` from
   [Releases](../../releases) (v2.0.0 or later)
2. Run it — the speed display appears in the taskbar, just left of the clock
3. Right-click it → *Start with Windows* if you want it at every login

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
