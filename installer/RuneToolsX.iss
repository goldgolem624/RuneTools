; RuneToolsX installer -- Inno Setup 6 (get it free: https://jrsoftware.org/isdl.php)
;
; Build:   ISCC.exe RuneToolsX.iss          (run from this installer\ folder)
;   or open this file in the Inno Setup IDE and press Build (F9).
;
; Output:  installer\Output\RuneToolsXSetup.exe  (~75 MB)
;          Upload THAT single .exe to the website's "Upload New Client Version".
;
; It bundles only the runtime files from ..\x64\Release (no build intermediates).
; Per-user install -- no administrator rights required.

#define MyAppName "RuneTools"
#define MyAppPublisher "RuneTools"
#define MyAppURL "https://runetools.io"
#define MyAppExe "RuneToolsXLauncher.exe"
#define SrcDir "..\x64\Release"
; Version is read from the built launcher exe (its app.rc FILEVERSION) so the setup and the
; bundled launcher can never desync. Bump app.rc only -- this follows automatically.
#define MyAppVersion GetFileVersion(AddBackslash(SourcePath) + SrcDir + "\" + MyAppExe)

[Setup]
AppId={{8F2C0C8A-1B2C-4D3E-9F40-A1B2C3D4E5F6}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppVerName={#MyAppName} {#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}
AppUpdatesURL={#MyAppURL}
; Install dir keeps the "RuneToolsX" name to match the per-user data path the app uses.
DefaultDirName={autopf}\RuneToolsX
DisableProgramGroupPage=yes
UninstallDisplayIcon={app}\{#MyAppExe}
; The Ultralight SDK licence requires end-user distribution of its runtime to be
; governed by its EULA (Exhibit A), so it is shown and accepted at install time.
; Staged next to the runtime by the launcher's StageRuntime target.
LicenseFile={#SrcDir}\Ultralight\license\EULA.txt
OutputDir=Output
OutputBaseFilename=RuneToolsXSetup
Compression=lzma2/max
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
VersionInfoVersion={#MyAppVersion}
VersionInfoProductVersion={#MyAppVersion}
; Brand-level file metadata from the defines above.
VersionInfoCompany={#MyAppPublisher}
VersionInfoProductName={#MyAppName}
VersionInfoDescription={#MyAppName} Setup
VersionInfoCopyright=Copyright (C) 2026 {#MyAppPublisher}
; Code signing is opt-in: pass /DSign to ISCC. An unsigned local build works unchanged.
;   ISCC.exe /DSign "/Srtsign=signtool sign /sha1 <THUMB> /fd sha256 /tr http://timestamp.digicert.com /td sha256 $f" RuneToolsX.iss
#ifdef Sign
SignTool=rtsign
SignedUninstaller=yes
#endif
WizardStyle=modern
PrivilegesRequired=lowest
; Silent in-place update: the launcher holds the "RuneToolsXLauncher" mutex and runs this
; setup with /VERYSILENT. InitializeSetup (see [Code]) waits for that mutex to clear (the
; old launcher to exit) before proceeding; AppMutex is intentionally not used, as its prompt
; aborts the install under /SUPPRESSMSGBOXES. CloseApplications is a backstop;
; RestartApplications=no because the [Run] entry relaunches the app.
CloseApplications=yes
RestartApplications=no

[Tasks]
Name: "desktopicon"; Description: "Create a &desktop shortcut"; GroupDescription: "Additional shortcuts:"

[Files]
; Launcher only. The reader + cache modules are compiled into the launcher exe, so there
; is no separate runtime DLL to ship.
Source: "{#SrcDir}\{#MyAppExe}";    DestDir: "{app}"; Flags: ignoreversion
; Scene-data module: read-only, loaded into the client to surface runtime scene
; objects the launcher can't read on its own. Staged next to the exe by the build.
Source: "{#SrcDir}\rtxscene.dll";   DestDir: "{app}"; Flags: ignoreversion
; UI + data assets
Source: "{#SrcDir}\client.html";    DestDir: "{app}"; Flags: ignoreversion
Source: "{#SrcDir}\quest_guides.js"; DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist
; Split panel modules (spliced inline into client.html at load by inject_panel_scripts)
Source: "{#SrcDir}\panel_*.js";     DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist
Source: "{#SrcDir}\launcher.html";  DestDir: "{app}"; Flags: ignoreversion
Source: "{#SrcDir}\items.pack";     DestDir: "{app}"; Flags: ignoreversion
Source: "{#SrcDir}\wd_table.bin";   DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist
Source: "{#SrcDir}\pdb_5554.bin";   DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist
Source: "{#SrcDir}\sounds\*";       DestDir: "{app}\sounds"; Flags: ignoreversion recursesubdirs createallsubdirs
; Ultralight runtime (DLLs + resources + its licence folder: EULA, LICENSE, NOTICES)
Source: "{#SrcDir}\Ultralight\*";   DestDir: "{app}\Ultralight"; Flags: ignoreversion recursesubdirs createallsubdirs
; RuneTools' own licence + third-party attributions, alongside the installed app
Source: "..\LICENSE";                    DestDir: "{app}"; Flags: ignoreversion
Source: "..\THIRD-PARTY-NOTICES.md";     DestDir: "{app}"; Flags: ignoreversion
; CS2 extractor sidecar (GPL rsmv-based, runs as a separate process; ships node.exe + npm dep closure)
Source: "{#SrcDir}\cs2sidecar\*";   DestDir: "{app}\cs2sidecar"; Flags: ignoreversion recursesubdirs createallsubdirs skipifsourcedoesntexist

[Icons]
Name: "{group}\{#MyAppName}";       Filename: "{app}\{#MyAppExe}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExe}"; Tasks: desktopicon

[Run]
; Two entries so the launcher reopens in BOTH install modes:
;  - Interactive: the usual "Launch RuneToolsX" checkbox on the Finished page.
;  - Silent auto-update (/VERYSILENT from the launcher): postinstall entries do NOT
;    run under /VERYSILENT (their Finished page never shows), so relaunch explicitly
;    whenever WizardSilent is true. skipifsilent on the first entry prevents a
;    double launch in silent mode.
Filename: "{app}\{#MyAppExe}"; Description: "Launch {#MyAppName}"; Flags: nowait postinstall skipifsilent
Filename: "{app}\{#MyAppExe}"; Flags: nowait; Check: WizardSilent

[Code]
function InitializeSetup(): Boolean;
var
  i: Integer;
  rc: Integer;
begin
  { The auto-updater launches us (/VERYSILENT) while the old launcher is still running;
    it exits ~1.5s after kicking us off. Wait up to ~15s for its mutex to clear so its
    files are unlocked and we don't trip the running-app abort. Returns immediately when
    no launcher is running (e.g. a fresh interactive install). }
  for i := 1 to 75 do
  begin
    if not CheckForMutexes('RuneToolsXLauncher') then
      break;
    Sleep(200);
  end;
  { If the mutex is STILL held after the wait, the old launcher hung on shutdown (older builds
    could deadlock in ExitProcess's DLL-detach teardown) and will never release the lock on its
    own exe -- CloseApplications can't close a deadlocked process either. Force-kill it so the
    file-copy step can replace the exe; otherwise the copy fails and the update is silently lost.
    This is what recovers a client that is running a pre-fix (hanging) launcher. }
  if CheckForMutexes('RuneToolsXLauncher') then
  begin
    Exec(ExpandConstant('{sys}\taskkill.exe'), '/F /IM {#MyAppExe}', '', SW_HIDE, ewWaitUntilTerminated, rc);
    { taskkill returns while the OS is still reaping the launcher's threads + Ultralight/GPU state,
      which can hold the exe lock for a few seconds. Wait for the mutex to ACTUALLY clear (process
      fully gone) before the [Files] copy runs. }
    for i := 1 to 50 do
    begin
      if not CheckForMutexes('RuneToolsXLauncher') then
        break;
      Sleep(200);
    end;
  end;
  { Settle margin -- MUST run on BOTH exit paths, not just after a force-kill. The launcher exits
    the normal auto-update via TerminateProcess, which is just as abrupt as taskkill /F: the
    "RuneToolsXLauncher" mutex is released when the process object is torn down, but the launcher
    exe's image lock lingers a beat longer while the OS reaps its threads + Ultralight/GPU/SSE
    state. If [Files] copies {#MyAppExe} while it is still locked the copy silently fails (there
    is no restartreplace fallback), so the OLD exe survives and [Run] relaunches it -- the
    "download, restart, still on the old version" bug distributed users hit. This margin used to
    run ONLY inside the force-kill branch; a launcher that terminated normally raced unguarded. }
  Sleep(1200);
  Result := True;
end;
