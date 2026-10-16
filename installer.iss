; NetSpeedMeter installer (Inno Setup 6) - per-user, no UAC needed
; Build: ISCC.exe /DMyAppVersion=2.3.0 installer.iss

#define MyAppName "NetSpeedMeter"
#define MyAppExe "NetSpeedMonitor.exe"
#define MyAppPublisher "3S Verse"
#define MyAppURL "https://github.com/abaduchanna/NetSpeedMeter"
#ifndef MyAppVersion
#define MyAppVersion "2.3.0"
#endif

[Setup]
AppId={{8E1A7B52-6D3E-4B9C-9F2A-3C5D7E0F1A64}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}/issues
DefaultDirName={localappdata}\{#MyAppName}
DisableDirPage=no
DisableProgramGroupPage=yes
LicenseFile=LICENSE
OutputDir=dist
OutputBaseFilename=NetSpeedMeter-{#MyAppVersion}-x64-Setup
SetupIconFile=src\icon.ico
UninstallDisplayIcon={app}\{#MyAppExe}
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
PrivilegesRequired=lowest
CloseApplications=yes

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked
Name: "autostart"; Description: "Start NetSpeedMeter automatically when I sign in"; GroupDescription: "Startup:"

[Files]
Source: "src\NetSpeedMonitor.exe"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\{#MyAppName}"; Filename: "{app}\{#MyAppExe}"; Comment: "Taskbar network speed display (download green / upload red)"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExe}"; Tasks: desktopicon

[Registry]
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: string; ValueName: "NetSpeedMeter"; ValueData: """{app}\{#MyAppExe}"""; Flags: uninsdeletevalue; Tasks: autostart

[Run]
Filename: "{app}\{#MyAppExe}"; Description: "{cm:LaunchProgram,{#MyAppName}}"; Flags: nowait postinstall skipifsilent
