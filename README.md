# NetSpeedMonitor

> Repository name: NetSpeedMeter. Product name: NetSpeedMonitor.

NetSpeedMonitor displays the current sent and received network speed in the Windows taskbar.

---

**NetSpeedMeter** is part of the [3SVerse](https://3sverse.com) suite — software, systems & operations for wireless dealers and beyond. More free tools at [3sverse.com](https://3sverse.com).

## Features

- Shows live `Sent` and `Recv` transfer rates in one taskbar row.
- Monitors all detected network interfaces or one selected interface.
- Supports kB/s, KiB/s, MB/s, and MiB/s with configurable decimal precision.
- Lets you change the Sent/Recv labels, display order, font, text color, and tooltip setting.
- Can pause and resume monitoring without closing the application.
- Tracks session, today, month, and total sent/received traffic.
- Shows daily traffic history and a 14-day graph.
- Imports and exports traffic history as CSV.
- Can reset the current session or clear saved traffic history.
- Supports embedded taskbar mode and a floating fallback. On Windows 11, embedding reserves real space directly in Explorer's own taskbar layout (via `NetSpeedTaskbarXaml.dll`, shipped alongside the exe) instead of only the legacy compatibility reparenting trick, for pixel-accurate placement; Windows 10, or any Windows 11 case the bridge can't reach, falls back to the proven reparenting technique automatically.
- Can start automatically when you sign in to Windows.
- Provides a tray icon with access to monitoring, traffic, settings, diagnostics, and exit controls.

## Install

Download `NetSpeedMonitor-3.0.2-x64-Setup.exe` from the [latest release](https://github.com/abaduchanna/NetSpeedMeter/releases/latest) and run it. The installer works per user and can optionally create a desktop shortcut and enable start-with-Windows.

## Portable use

Download `NetSpeedMonitor-v3.0.2-x64-portable.zip` from the Releases page, unzip it, and run `NetSpeedMonitor.exe`. Keep `NetSpeedTaskbarXaml.dll` in the same folder - it enables the Windows 11 taskbar reservation; the app still runs and falls back automatically without it, just without the pixel-accurate Windows 11 placement. No background service is installed.

## Controls

- Double-click the taskbar display to open Data Traffic.
- Right-click the taskbar display or tray icon to open the menu.
- Open Settings to select the network interface and change display preferences.

## Data locations

- Settings: `HKEY_CURRENT_USER\Software\NetSpeedMonitor`
- Traffic history: `%LOCALAPPDATA%\NetSpeedMonitor\traffic.db`
- Diagnostic log: `%LOCALAPPDATA%\NetSpeedMonitor\log.txt`

The traffic history file is maintained by this application and is not an SQLite database.

## Build

The Windows workflow builds the application with MSVC and packages it with Inno Setup.

## License

The source code in this repository is provided under the [MIT License](LICENSE).
