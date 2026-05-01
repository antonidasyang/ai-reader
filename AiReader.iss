; ── Inno Setup script for AI Reader ────────────────────────────────
;
; Run with:
;   "C:\Program Files (x86)\Inno Setup 6\ISCC.exe" AiReader.iss
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
; Bump MyAppVersion below to match CMakeLists.txt's project(... VERSION).

#define MyAppName       "AI Reader"
#define MyAppVersion    "0.1.0"
#define MyAppPublisher  "AI Reader"
#define MyAppExeName    "ai-reader.exe"

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
DefaultDirName={autopf}\AI Reader
DefaultGroupName=AI Reader
DisableProgramGroupPage=yes
UninstallDisplayIcon={app}\{#MyAppExeName}
UninstallDisplayName={#MyAppName} {#MyAppVersion}
; Per-user install — no admin prompt. Switch to "admin" + DefaultDirName=
; "{autopf}\AI Reader" (already the default for admin) if you'd rather
; install under Program Files.
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
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

[Icons]
Name: "{group}\{#MyAppName}";        Filename: "{app}\{#MyAppExeName}"
Name: "{group}\Uninstall {#MyAppName}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#MyAppName}";  Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#StringChange(MyAppName, '&', '&&')}}"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
; QSettings + cache live under %LOCALAPPDATA%; leave them alone so
; reinstalling preserves the user's library, chat history, and API
; key. To wipe state on uninstall, replace this section with:
;   Type: filesandordirs; Name: "{localappdata}\AI Reader"
