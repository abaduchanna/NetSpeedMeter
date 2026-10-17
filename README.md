# NetSpeedMonitor

NetSpeedMonitor displays the current sent and received network speed in the Windows taskbar.

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
- Supports embedded taskbar mode and a floating fallback.
- Can start automatically when you sign in to Windows.
- Provides a tray icon with access to monitoring, traffic, settings, diagnostics, and exit controls.

## Install

Download the x64 setup file from the repository's Releases page and run it. The installer works per user and can optionally create a desktop shortcut and enable start-with-Windows.

## Portable use

Download the portable x64 executable from the Releases page and run it directly. No background service is installed.

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
