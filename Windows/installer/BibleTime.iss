; BibleTime for Windows: Inno Setup 6 installer script.
;
; Builds BibleTime-Windows-Setup.exe, which copies BibleTime.scr into System32 (where Windows' Screen Saver
; Settings looks for screen savers), registers an uninstaller under Settings > Apps, and on the last page offers
; to make BibleTime the current user's screen saver and to open Screen Saver Settings.
;
;   "C:\Program Files (x86)\Inno Setup 6\ISCC.exe" /DAppVersion=1.1.0 /DSourceScr=..\..\build\windows\bin\BibleTime.scr ^
;       /O..\..\build\windows\installer Windows\installer\BibleTime.iss
;
; Every define has a default, so plain "ISCC Windows\installer\BibleTime.iss" works after a CMake build in
; build\windows. Relative paths are relative to this script's folder.

#if Ver < EncodeVer(6, 3, 0)
  #error Inno Setup 6.3 or later is required (for ArchitecturesAllowed=x64compatible)
#endif

#ifndef AppVersion
  ; Default to the repository's VERSION file.
  #define VersionFile FileOpen(AddBackslash(SourcePath) + "..\..\VERSION")
  #define AppVersion Trim(FileRead(VersionFile))
  #expr FileClose(VersionFile)
#endif
#ifndef SourceScr
  #define SourceScr "..\..\build\windows\bin\BibleTime.scr"
#endif
#ifndef OutputDir
  #define OutputDir "..\..\build\windows\installer"
#endif

#define AppName "BibleTime"
#define AppURL  "https://github.com/calebtonn/BibleTime"

[Setup]
; Never change AppId: Windows and Inno Setup use it to recognise an existing install when upgrading.
AppId={{C27634AB-908F-4E39-8EA6-8B846FDCC1C6}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher=Caleb Tonn
AppPublisherURL={#AppURL}
AppSupportURL={#AppURL}
AppUpdatesURL={#AppURL}/releases/latest
AppCopyright=Copyright (c) Caleb Tonn. MIT License.
VersionInfoVersion={#AppVersion}
VersionInfoDescription={#AppName} Screen Saver Setup
VersionInfoProductName={#AppName}

; The screen saver itself goes to System32; this folder only holds the uninstaller.
DefaultDirName={autopf}\{#AppName}
DisableDirPage=yes
DisableProgramGroupPage=yes
Uninstallable=yes
UninstallDisplayName={#AppName} Screen Saver
UninstallDisplayIcon={sys}\BibleTime.scr

; System32 needs administrator rights; 64-bit mode so {sys} is the real 64-bit System32 (on ARM64 the x64 build
; runs under emulation).
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0

; A short wizard: Ready -> Installing -> Finished (with the two options). No welcome, folder or license pages.
WizardStyle=modern
DisableWelcomePage=yes
DisableReadyMemo=yes
ShowLanguageDialog=no
SetupIconFile=..\res\BibleTime.ico

; Upgrading while the saver (or its preview in Screen Saver Settings) is running: the Code section stops it first;
; if something else still holds the file, Restart Manager offers to close it (Inno's default filter ignores .scr),
; and as a last resort restartreplace swaps the file at the next reboot.
CloseApplications=yes
CloseApplicationsFilter=*.exe,*.dll,*.chm,*.scr
RestartApplications=no

OutputDir={#OutputDir}
OutputBaseFilename=BibleTime-Windows-Setup
Compression=lzma2/max
SolidCompression=yes

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Messages]
FinishedLabelNoIcons=BibleTime is installed. You can change how it looks any time from Screen Saver Settings > Settings...

[Files]
Source: "{#SourceScr}"; DestDir: "{sys}"; DestName: "BibleTime.scr"; Flags: ignoreversion restartreplace uninsrestartdelete 64bit

[Run]
; Both run as the user who started Setup (not the elevated administrator account), because the screen saver choice
; lives in that user's registry (HKCU\Control Panel\Desktop). /setactive runs first and is waited for, so Screen
; Saver Settings opens with BibleTime already selected.
Filename: "{sys}\BibleTime.scr"; Parameters: "/setactive"; Description: "Use BibleTime as my screen saver"; \
    StatusMsg: "Choosing BibleTime as your screen saver..."; Flags: postinstall runasoriginaluser runhidden
Filename: "{sys}\control.exe"; Parameters: "desk.cpl,,@screensaver"; Description: "Open Screen Saver Settings"; \
    Flags: postinstall runasoriginaluser nowait skipifsilent

[UninstallRun]
; [UninstallRun] has no runasoriginaluser flag (the uninstaller runs entirely elevated), so this clears the choice
; for the account the uninstaller runs as. With ordinary UAC that is the same person who installed it. If another
; administrator account approved the prompt, the original user's setting is left pointing at a missing file, which
; Windows treats as "(None)". /unsetactive only touches the setting when it points at this BibleTime.scr.
Filename: "{sys}\BibleTime.scr"; Parameters: "/unsetactive"; RunOnceId: "UnsetActive"; Flags: runhidden skipifdoesntexist

[Code]
{ Stop any running copy of the saver (full screen, or the preview inside Screen Saver Settings) so its file can be
  replaced or removed right away instead of at the next reboot. Errors (nothing running) are ignored. }
procedure StopRunningSaver();
var
  ResultCode: Integer;
begin
  Exec(ExpandConstant('{sys}\taskkill.exe'), '/F /IM BibleTime.scr', '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  StopRunningSaver();
  Result := '';
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usUninstall then
    StopRunningSaver();
end;
