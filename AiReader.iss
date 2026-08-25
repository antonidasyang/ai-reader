; ── Inno Setup script for AI Reader ────────────────────────────────
;
; Run with:
;   "C:\Program Files\Inno Setup 7\ISCC.exe" AiReader.iss
;
; Inputs:
;   dist\         a fully-staged portable folder produced by:
;                 1. cmake --build build --config Release
;                    (POST_BUILD copies ai-reader.exe into dist\)
;                 2. windeploy.bat
;                    (windeployqt fills in Qt DLLs, plugins, QML modules)
;
; Output:
;   installer\AiReader-Setup-<version>.exe   single-file installer.
;
; The version is read from dist\ai-reader.exe's resource (CMake stamps it from
; project(... VERSION ...)); there is nothing to bump in this file.

#define MyAppName       "AI Reader"
; The brand, not the product: this is the vendor Windows shows in
; Apps & features and in the wizard.
#define MyAppPublisher  "D2S"
#define MyAppExeName    "ai-reader.exe"

; App version — SINGLE-SOURCED from the built exe's version resource, which
; CMake fills from project(... VERSION ...) via resources\version.rc.in. So
; AppVersion AND the setup filename (OutputBaseFilename below) both track
; CMakeLists.txt with nothing to bump here. Build + windeploy first so
; dist\ai-reader.exe exists. Override for a one-off build:
;   iscc /DMyAppVersion=1.2.3 AiReader.iss
#ifndef MyAppVersion
  #define MyAppVersion GetStringFileInfo("dist\" + MyAppExeName, "ProductVersion")
#endif
; A stale exe built before the VERSIONINFO resource existed returns "" here,
; which would otherwise fail cryptically at VersionInfoVersion below. Fail loud.
#if MyAppVersion == ""
  #error dist\ai-reader.exe has no embedded version. Rebuild first (package.bat build); CMake stamps the version from project(... VERSION ...).
#endif

[Setup]
AppId={{6E0AF8DC-4F36-4A6B-9F38-9E91D9E32F9D}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppVerName={#MyAppName} {#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL=https://github.com/antonidasyang/ai-reader
AppSupportURL=https://github.com/antonidasyang/ai-reader/issues
AppUpdatesURL=https://github.com/antonidasyang/ai-reader/releases
AppCopyright=Copyright © AI Reader contributors
; Embed version + product fields into the setup .exe's PE resource
; so Windows Properties → Details and Add/Remove Programs both show
; the same version string. VersionInfoVersion must be a 4-component
; numeric tuple — anything else is rejected by the Windows resource
; compiler. MyAppVersion is the human-readable string shown in the
; wizard chrome, dialogs, and install registry entries.
VersionInfoVersion={#MyAppVersion}.0
VersionInfoProductName={#MyAppName}
VersionInfoProductVersion={#MyAppVersion}
VersionInfoCompany={#MyAppPublisher}
VersionInfoDescription={#MyAppName} installer
VersionInfoCopyright=Copyright © AI Reader contributors
; Brand folder, product folder, no space -- the same convention the
; app now uses for its data directories (StorageIdentity). Existing
; installs keep their own folder: AppId is unchanged, so this is
; only the default for a first install.
DefaultDirName={autopf}\D2S\AIReader
DefaultGroupName=AI Reader
DisableProgramGroupPage=yes
UninstallDisplayIcon={app}\{#MyAppExeName}
UninstallDisplayName={#MyAppName} {#MyAppVersion}
; Per-user install — no admin prompt. Switch to "admin" + DefaultDirName=
; "{autopf}\D2S\AIReader" (already the default for admin) if you'd rather
; install under Program Files.
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
; Qt 6.5+ requires Windows 10 1809; reject older systems at install
; time instead of letting them install and crash on launch.
; (SafePacker lesson: fail BEFORE install, not after.)
MinVersion=10.0.17763
; One-click in-app updates: the running app launches this installer
; silently; the Restart Manager closes the app, files get swapped,
; and the app relaunches automatically.
CloseApplications=yes
RestartApplications=yes
WizardStyle=modern
Compression=lzma2/max
SolidCompression=yes
OutputDir=installer
OutputBaseFilename=AiReader-Setup-{#MyAppVersion}
SetupIconFile=resources\icons\app.ico

[Languages]
Name: "english";  MessagesFile: "compiler:Default.isl"
Name: "chinese";  MessagesFile: "compiler:Languages\ChineseSimplified.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
; Recursively pull in everything dist\ contains (ai-reader.exe + Qt DLLs
; + plugins + QML modules). recursesubdirs/createallsubdirs preserves
; the layout windeployqt produced.
Source: "dist\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs
; Belt and braces on top of the app-local CRT DLLs: run the official
; VC++ redistributable silently when it was staged by package.bat
; (installer\cache\, downloaded once and reused). Idempotent — a
; newer runtime already on the system makes it a no-op. When the
; cache is absent the installer still builds; the app-local DLLs
; carry stock Win10/11 machines on their own.
Source: "installer\cache\vc_redist.x64.exe"; DestDir: "{tmp}"; Flags: deleteafterinstall skipifsourcedoesntexist

[Icons]
Name: "{group}\{#MyAppName}";        Filename: "{app}\{#MyAppExeName}"
Name: "{group}\Uninstall {#MyAppName}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#MyAppName}";  Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Run]
Filename: "{tmp}\vc_redist.x64.exe"; Parameters: "/install /quiet /norestart"; StatusMsg: "Installing Microsoft VC++ runtime..."; Flags: waituntilterminated; Check: VcRedistStaged
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#StringChange(MyAppName, '&', '&&')}}"; Flags: nowait postinstall skipifsilent
; Silent installs come from the in-app auto-updater (the app exits
; right after launching us, per the official self-update pattern), so
; relaunch the new version explicitly. RestartApplications can't do
; it: it relies on RegisterApplicationRestart, which Qt apps don't
; call. runasoriginaluser keeps the app de-elevated if the update
; ever needed a UAC prompt (harmless when Setup ran non-elevated).
Filename: "{app}\{#MyAppExeName}"; Flags: nowait skipifnotsilent runasoriginaluser

[Code]
function VcRedistStaged: Boolean;
begin
  Result := FileExists(ExpandConstant('{tmp}\vc_redist.x64.exe'));
end;

[UninstallDelete]
; QSettings + cache live under %LOCALAPPDATA%; leave them alone so
; reinstalling preserves the user's library, chat history, and API
; key. To wipe state on uninstall, replace this section with:
;   Type: filesandordirs; Name: "{localappdata}\AI Reader"
