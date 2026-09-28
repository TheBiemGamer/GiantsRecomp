#define MyAppName "GiantRecomp"
#define MyAppVersion "0.1.0"
#define MyAppExeName "giantrecomp.exe"

[Setup]
AppId={{817B5C32-5040-493F-8D21-566420901C26}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
DefaultDirName={localappdata}\Programs\{#MyAppName}
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
ArchitecturesInstallIn64BitMode=x64compatible
InfoBeforeFile=disclaimer.txt
OutputDir=Output
OutputBaseFilename=GiantRecompSetup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern

[Files]
Source: "staging\giantrecomp.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "staging\*.dll"; DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist
Source: "staging\giantrecomp_xexcheck.exe"; DestDir: "{tmp}"; Flags: dontcopy
Source: "settings_template.toml"; DestDir: "{tmp}"; Flags: dontcopy
Source: "extract-xiso.exe"; DestDir: "{tmp}"; Flags: dontcopy
; Vendored Visual C++ runtime DLLs (from the maintainer's own VS install, redistributable per
; Microsoft's VC++ Redistributable license): giantrecomp.exe and its own DLLs need these permanently
; installed in {app}, and giantrecomp_xexcheck.exe -- run from {tmp} before {app} even exists, for
; the ROM version check -- needs its own copies alongside it there, since Windows' DLL search order
; checks the running executable's own directory first.
Source: "redist\msvcp140.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "redist\vcruntime140.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "redist\vcruntime140_1.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "redist\msvcp140_atomic_wait.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "redist\msvcp140.dll"; DestDir: "{tmp}"; Flags: dontcopy
Source: "redist\vcruntime140.dll"; DestDir: "{tmp}"; Flags: dontcopy

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{group}\Uninstall {#MyAppName}"; Filename: "{uninstallexe}"

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "Launch {#MyAppName}"; Flags: nowait postinstall skipifsilent

[Code]
var
  RomPage: TWizardPage;
  RomIsIsoRadio, RomIsFolderRadio: TNewRadioButton;
  RomPathEdit: TNewEdit;
  RomBrowseButton: TNewButton;
  RomStatusLabel: TNewStaticText;
  RomProgressBar: TNewProgressBar;
  SettingsPage: TWizardPage;
  PortalModeLabel, ResolutionLabel, ResolutionScaleLabel: TNewStaticText;
  PortalModeCombo: TNewComboBox;
  ResolutionCombo: TNewComboBox;
  ResolutionScaleEdit: TNewEdit;

procedure RomBrowseButtonClick(Sender: TObject);
var
  Path: String;
begin
  if RomIsIsoRadio.Checked then begin
    Path := '';
    if GetOpenFileName('Select the Skylanders Giants ISO', Path, '', 'ISO files|*.iso|All files|*.*', 'iso') then
      RomPathEdit.Text := Path;
  end else begin
    Path := '';
    if BrowseForFolder('Select the extracted game folder (must contain default.xex)', Path, False) then
      RomPathEdit.Text := Path;
  end;
end;

// giantrecomp_xexcheck.exe runs from {tmp} before {app} exists, so it needs its own copies of the
// two VC++ runtime DLLs it imports sitting next to it there -- Windows checks the running
// executable's own directory first when resolving DLL imports.
procedure ExtractXexCheckWithRuntime;
begin
  ExtractTemporaryFile('giantrecomp_xexcheck.exe');
  ExtractTemporaryFile('msvcp140.dll');
  ExtractTemporaryFile('vcruntime140.dll');
end;

// GetSpaceOnDisk64 requires an existing path (a drive root, or an existing directory); {app}
// itself doesn't exist on disk yet at the point this is called (before the Install step creates
// it), so querying {app} directly always fails and silently reports 0 MB free. Querying the drive
// root instead works regardless of whether {app} has been created yet.
function GetFreeSpaceMB(const Drive: String): Int64;
var
  Free, Total: Int64;
  Root: String;
begin
  Root := ExtractFileDrive(Drive) + '\';
  if not GetSpaceOnDisk64(Root, Free, Total) then
    Free := 0;
  Result := Free div (1024 * 1024);
end;

// Copies SourceDir's contents into {app}\rom using robocopy. Robocopy's exit codes are a bitmask
// where 0-7 all mean success (e.g. 1 = "files copied") and only 8+ means a real failure -- a plain
// "ResultCode <> 0" check would wrongly treat a normal successful copy as an error. A crashed
// robocopy (e.g. from unbounded recursion) returns a negative NTSTATUS, which must also be treated
// as failure, not silently accepted by "< 8".
//
// The guard rejects SourceDir when it equals DestDir OR either one is nested inside the other, in
// either direction (a prior version only checked exact equality, reasoning that PreflightCheckRom's
// earlier default.xex-must-exist-at-source-root check already screens out any ancestor pick -- that
// reasoning missed the case where the user installs into, or a subfolder of, their own extracted
// game folder, so SourceDir itself contains default.xex AND is an ancestor of {app}\rom. Confirmed
// by reproducing it: robocopy recurses into the destination it just created, thousands of nested
// "rom\rom\rom\..." directories deep, until it crashes -- and the old exit-code check accepted that
// crash as success).
function CopyRomFolder(const SourceDir: String; var ErrorMsg: String): Boolean;
var
  DestDir, SourceNorm, DestNorm, RobocopySource: String;
  ExecOk: Boolean;
  ResultCode: Integer;
begin
  DestDir := ExpandConstant('{app}') + '\rom';
  SourceNorm := AddBackslash(ExpandFileName(SourceDir));
  DestNorm := AddBackslash(ExpandFileName(DestDir));
  if (CompareText(SourceNorm, DestNorm) = 0) or
     (CompareText(Copy(DestNorm, 1, Length(SourceNorm)), SourceNorm) = 0) or
     (CompareText(Copy(SourceNorm, 1, Length(DestNorm)), DestNorm) = 0) then begin
    ErrorMsg := 'The selected folder is the installed game folder, or contains it (or is contained by it). Choose your original extracted disc folder instead.';
    Result := False;
    Exit;
  end;
  ForceDirectories(DestDir);

  // robocopy misreads a source path ending in a backslash (e.g. a drive root like "E:\", which
  // BrowseForFolder can return) as an escaped quote, breaking its argument parsing. A bare drive
  // root needs "E:\." instead; any other trailing backslash can just be dropped.
  RobocopySource := SourceDir;
  if (Length(RobocopySource) = 3) and (RobocopySource[2] = ':') and (RobocopySource[3] = '\') then
    RobocopySource := RobocopySource + '.'
  else
    while (Length(RobocopySource) > 1) and (RobocopySource[Length(RobocopySource)] = '\') do
      Delete(RobocopySource, Length(RobocopySource), 1);

  // /R:2 /W:1 overrides robocopy's default of a million retries with a 30-second wait between each
  // -- without this, a single locked or flaky source file makes the wizard hang effectively forever.
  ExecOk := Exec(ExpandConstant('{sys}\robocopy.exe'),
      '"' + RobocopySource + '" "' + DestDir + '" /E /R:2 /W:1 /NFL /NDL /NJH /NJS /NC /NS /NP',
      '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
  Result := ExecOk and (ResultCode >= 0) and (ResultCode < 8);
  if not Result then begin
    ErrorMsg := 'Copying the game files failed (robocopy exit code ' + IntToStr(ResultCode) + ').';
    DelTree(DestDir, True, True, True);
  end;
end;

// -x extract mode, -d destination directory, -s skips the $SystemUpdate folder (irrelevant to
// running the game, and ~100 MB smaller) -- confirmed against this vendored build's own
// `extract-xiso -h` output (v2.7.1) in Task 3, Step 1.
function ExtractRomFromIso(const IsoPath: String; var ErrorMsg: String): Boolean;
var
  DestDir, ExtractXisoExe: String;
  ResultCode: Integer;
begin
  DestDir := ExpandConstant('{app}') + '\rom';
  ForceDirectories(DestDir);
  ExtractXisoExe := ExpandConstant('{tmp}') + '\extract-xiso.exe';
  ExtractTemporaryFile('extract-xiso.exe');
  Result := Exec(ExtractXisoExe, '-x -s -d "' + DestDir + '" "' + IsoPath + '"', '',
                 SW_HIDE, ewWaitUntilTerminated, ResultCode) and (ResultCode = 0);
  if not Result then begin
    ErrorMsg := 'Extracting the ISO into the install folder failed (extract-xiso exit code ' + IntToStr(ResultCode) + ').';
    DelTree(DestDir, True, True, True);
  end;
end;

// Validates the candidate ROM and, for ISO input, performs the actual extraction as part of that
// validation. extract-xiso has no documented "list/extract one file" mode, so there is no cheap
// way to peek at an ISO's default.xex without extracting the whole disc -- unlike the folder-input
// case (SourcePath\default.xex can just be read in place, no copying needed to check it), ISO
// input's "preflight" IS the real extraction, done straight into {app}\rom. If the version check
// then fails, the partial extraction is rolled back with DelTree so no wrong-version game files
// are left behind. Callers must NOT call ExtractRomFromIso again after this returns True for ISO
// input -- it already happened here.
function PreflightCheckRom(const SourcePath: String; IsIso: Boolean; var ErrorMsg: String): Boolean;
var
  CandidateXex, XexCheckExe: String;
  ResultCode: Integer;
begin
  Result := False;

  if IsIso then begin
    if not ExtractRomFromIso(SourcePath, ErrorMsg) then Exit;
    CandidateXex := ExpandConstant('{app}') + '\rom\default.xex';
  end else begin
    CandidateXex := AddBackslash(SourcePath) + 'default.xex';
  end;

  if not FileExists(CandidateXex) then begin
    ErrorMsg := 'default.xex was not found. Make sure this is the extracted Skylanders Giants disc.';
    if IsIso then DelTree(ExpandConstant('{app}') + '\rom', True, True, True);
    Exit;
  end;

  XexCheckExe := ExpandConstant('{tmp}') + '\giantrecomp_xexcheck.exe';
  ExtractXexCheckWithRuntime;
  if not Exec(XexCheckExe, '"' + CandidateXex + '"', '', SW_HIDE, ewWaitUntilTerminated, ResultCode) then begin
    ErrorMsg := 'Could not run the version-check tool.';
    if IsIso then DelTree(ExpandConstant('{app}') + '\rom', True, True, True);
    Exit;
  end;
  // Exit codes match giantrecomp_xexcheck's own giantrecomp::XexCheckExitCode (src/tools/xex_check_exit_code.h):
  // 0 = match, 2 = version mismatch, 3 = unreadable, 4 = bad pinned hash (should never happen -- a
  // build-time invariant, not a user-triggerable state). A negative code means the process itself
  // crashed before printing anything meaningful (e.g. a missing Visual C++ runtime DLL) -- that must
  // not be reported as "wrong game version", which wrongly blames the user's legitimate disc.
  if ResultCode = 0 then begin
    Result := True;
    Exit;
  end;
  if ResultCode < 0 then
    ErrorMsg := 'The version-check tool failed to run (code ' + IntToStr(ResultCode) + '). ' +
      'This usually means the Visual C++ runtime is missing on this PC.'
  else if ResultCode = 3 then
    ErrorMsg := 'Could not read ' + CandidateXex + ' to check its version.'
  else if ResultCode = 2 then
    ErrorMsg := 'This is not the supported Skylanders Giants version (1.0, USA or Europe). ' +
      'The installer will not continue with an unsupported copy of the game.'
  else
    ErrorMsg := 'The version-check tool reported an unexpected error (code ' + IntToStr(ResultCode) + ').';
  if IsIso then DelTree(ExpandConstant('{app}') + '\rom', True, True, True);
end;

function PortalModeTomlValue: AnsiString;
begin
  case PortalModeCombo.ItemIndex of
    1: Result := 'usb';
    2: Result := 'none';
  else
    Result := 'software';
  end;
end;

// Replaces every occurrence of FromStr in S with ToStr. Used instead of Inno's builtin
// StringChangeEx: its exact parameter types didn't match what the plan assumed (a "Type mismatch"
// compile error on the SupportEnvVars argument), so this avoids depending on a builtin whose exact
// signature in this Inno version wasn't as documented.
function ReplaceAll(const S, FromStr, ToStr: AnsiString): AnsiString;
var
  P: Integer;
  Work: AnsiString;
begin
  Work := S;
  P := Pos(FromStr, Work);
  while P > 0 do begin
    Delete(Work, P, Length(FromStr));
    Insert(ToStr, Work, P);
    P := Pos(FromStr, Work);
  end;
  Result := Work;
end;

// Writes {app}\giantsrecomp.toml from the vendored template, substituting the three wizard-chosen
// values. Never overwrites an existing file: an update run (Task 7) relies on this exact check to
// leave a previously-configured, possibly hand-edited toml untouched.
procedure WriteSettingsFile;
var
  TemplatePath, DestPath: String;
  Contents: AnsiString;
  ResolutionScale: String;
  ScaleValue: Integer;
begin
  DestPath := ExpandConstant('{app}') + '\giantsrecomp.toml';
  if FileExists(DestPath) then Exit;

  ExtractTemporaryFile('settings_template.toml');
  TemplatePath := ExpandConstant('{tmp}') + '\settings_template.toml';
  LoadStringFromFile(TemplatePath, Contents);

  // Re-serialize through IntToStr rather than writing the trimmed text as-is: StrToIntDef accepts
  // forms like "007" or "$10" (hex) that would either look odd or break TOML parsing, and applying
  // no upper bound would let a value outside the documented 1-8 range reach the settings file.
  ScaleValue := StrToIntDef(Trim(ResolutionScaleEdit.Text), 1);
  if ScaleValue < 1 then ScaleValue := 1;
  if ScaleValue > 8 then ScaleValue := 8;
  ResolutionScale := IntToStr(ScaleValue);

  Contents := ReplaceAll(Contents, '__PORTAL_MODE__', PortalModeTomlValue);
  Contents := ReplaceAll(Contents, '__RESOLUTION__', ResolutionCombo.Items[ResolutionCombo.ItemIndex]);
  Contents := ReplaceAll(Contents, '__RESOLUTION_SCALE__', ResolutionScale);

  SaveStringToFile(DestPath, Contents, False);
end;

// True when {app}\rom\default.xex already exists and passes the fingerprint check -- the signal
// used to treat this run as an update rather than a fresh install (spec's "Update runs" section).
function HasValidExistingRom: Boolean;
var
  ExistingXex, XexCheckExe: String;
  ResultCode: Integer;
begin
  Result := False;
  ExistingXex := ExpandConstant('{app}') + '\rom\default.xex';
  if not FileExists(ExistingXex) then Exit;

  XexCheckExe := ExpandConstant('{tmp}') + '\giantrecomp_xexcheck.exe';
  ExtractXexCheckWithRuntime;
  Result := Exec(XexCheckExe, '"' + ExistingXex + '"', '', SW_HIDE, ewWaitUntilTerminated, ResultCode)
    and (ResultCode = 0);
end;

// RomPage is always skipped when a valid rom/ already exists. SettingsPage is only skipped when
// the toml ALSO still exists -- if a user deleted just giantsrecomp.toml (Review Focus: this
// project's own docs/build.md notes settings are user-editable), skipping this page too would
// leave the install with rom/ but no settings file at all. Skipping settings independently of the
// rom check lets WriteSettingsFile (Task 6) regenerate a fresh default file for that case.
function ShouldSkipPage(PageID: Integer): Boolean;
begin
  Result := False;
  if PageID = RomPage.ID then
    Result := HasValidExistingRom;
  if PageID = SettingsPage.ID then
    Result := HasValidExistingRom and FileExists(ExpandConstant('{app}') + '\giantsrecomp.toml');
end;

procedure InitializeWizard;
begin
  RomPage := CreateCustomPage(wpSelectDir, 'Game Files',
    'Locate your Skylanders Giants disc (version 1.0, USA or Europe)');

  RomIsIsoRadio := TNewRadioButton.Create(RomPage);
  RomIsIsoRadio.Parent := RomPage.Surface;
  RomIsIsoRadio.Caption := 'ISO file';
  RomIsIsoRadio.Checked := True;
  RomIsIsoRadio.Top := 0;
  RomIsIsoRadio.Width := RomPage.SurfaceWidth;

  RomIsFolderRadio := TNewRadioButton.Create(RomPage);
  RomIsFolderRadio.Parent := RomPage.Surface;
  RomIsFolderRadio.Caption := 'Already-extracted folder';
  RomIsFolderRadio.Top := RomIsIsoRadio.Top + RomIsIsoRadio.Height + 4;
  RomIsFolderRadio.Width := RomPage.SurfaceWidth;

  RomPathEdit := TNewEdit.Create(RomPage);
  RomPathEdit.Parent := RomPage.Surface;
  RomPathEdit.Top := RomIsFolderRadio.Top + RomIsFolderRadio.Height + 12;
  RomPathEdit.Width := RomPage.SurfaceWidth - 90;

  RomBrowseButton := TNewButton.Create(RomPage);
  RomBrowseButton.Parent := RomPage.Surface;
  RomBrowseButton.Caption := 'Browse...';
  RomBrowseButton.Left := RomPathEdit.Width + 8;
  RomBrowseButton.Top := RomPathEdit.Top - 2;
  RomBrowseButton.Width := 80;
  RomBrowseButton.OnClick := @RomBrowseButtonClick;

  RomStatusLabel := TNewStaticText.Create(RomPage);
  RomStatusLabel.Parent := RomPage.Surface;
  RomStatusLabel.Top := RomPathEdit.Top + RomPathEdit.Height + 16;
  RomStatusLabel.Width := RomPage.SurfaceWidth;
  RomStatusLabel.AutoSize := False;
  RomStatusLabel.WordWrap := True;
  RomStatusLabel.Height := 40;
  RomStatusLabel.Caption := '';

  RomProgressBar := TNewProgressBar.Create(RomPage);
  RomProgressBar.Parent := RomPage.Surface;
  RomProgressBar.Top := RomStatusLabel.Top + RomStatusLabel.Height + 8;
  RomProgressBar.Width := RomPage.SurfaceWidth;
  RomProgressBar.Visible := False;

  SettingsPage := CreateCustomPage(RomPage.ID, 'Settings',
    'Choose your Portal of Power and display settings (everything else can be changed later with F4 in-game)');

  PortalModeLabel := TNewStaticText.Create(SettingsPage);
  PortalModeLabel.Parent := SettingsPage.Surface;
  PortalModeLabel.Caption := 'Portal of Power:';
  PortalModeLabel.Top := 0;

  PortalModeCombo := TNewComboBox.Create(SettingsPage);
  PortalModeCombo.Parent := SettingsPage.Surface;
  PortalModeCombo.Style := csDropDownList;
  PortalModeCombo.Items.Add('software (virtual Portal of Power)');
  PortalModeCombo.Items.Add('usb (real Portal of Power over USB)');
  PortalModeCombo.Items.Add('none (no portal)');
  PortalModeCombo.ItemIndex := 0;
  PortalModeCombo.Top := PortalModeLabel.Top + PortalModeLabel.Height + 4;
  PortalModeCombo.Width := SettingsPage.SurfaceWidth;

  ResolutionLabel := TNewStaticText.Create(SettingsPage);
  ResolutionLabel.Parent := SettingsPage.Surface;
  ResolutionLabel.Caption := 'Display resolution:';
  ResolutionLabel.Top := PortalModeCombo.Top + PortalModeCombo.Height + 16;

  ResolutionCombo := TNewComboBox.Create(SettingsPage);
  ResolutionCombo.Parent := SettingsPage.Surface;
  ResolutionCombo.Style := csDropDownList;
  ResolutionCombo.Items.Add('1920x1080');
  ResolutionCombo.Items.Add('2560x1440');
  ResolutionCombo.Items.Add('3840x2160');
  ResolutionCombo.ItemIndex := 0;
  ResolutionCombo.Top := ResolutionLabel.Top + ResolutionLabel.Height + 4;
  ResolutionCombo.Width := SettingsPage.SurfaceWidth;

  ResolutionScaleLabel := TNewStaticText.Create(SettingsPage);
  ResolutionScaleLabel.Parent := SettingsPage.Surface;
  ResolutionScaleLabel.Caption := 'Supersample scale (1-8, 1 = off):';
  ResolutionScaleLabel.Top := ResolutionCombo.Top + ResolutionCombo.Height + 16;

  ResolutionScaleEdit := TNewEdit.Create(SettingsPage);
  ResolutionScaleEdit.Parent := SettingsPage.Surface;
  ResolutionScaleEdit.Text := '1';
  ResolutionScaleEdit.Top := ResolutionScaleLabel.Top + ResolutionScaleLabel.Height + 4;
  ResolutionScaleEdit.Width := 60;
end;

function NextButtonClick(CurPageID: Integer): Boolean;
var
  ErrorMsg: String;
  FreeMB: Int64;
begin
  Result := True;

  if CurPageID = SettingsPage.ID then begin
    WriteSettingsFile;
    Exit;
  end;

  if CurPageID <> RomPage.ID then Exit;

  if Trim(RomPathEdit.Text) = '' then begin
    MsgBox('Choose your ISO file or extracted game folder first.', mbError, MB_OK);
    Result := False;
    Exit;
  end;

  // The real extracted disc (with $SystemUpdate skipped via -s) plus the installed binary is
  // close to 8000 MB itself, so 8000 MB left no real headroom -- checked before any extraction/copy
  // starts (see Review Focus in the plan: the installer must not fail partway through a
  // multi-gigabyte extraction). Checked here regardless of ISO vs. folder input, since for ISO
  // input the extraction happens inside PreflightCheckRom below, not in a separate later step.
  FreeMB := GetFreeSpaceMB(ExpandConstant('{app}'));
  if FreeMB < 9000 then begin
    MsgBox('Not enough free disk space. At least 9000 MB free is needed; ' + IntToStr(FreeMB) +
      ' MB is available.', mbError, MB_OK);
    Result := False;
    Exit;
  end;

  RomStatusLabel.Caption := 'Checking game version and copying files -- this can take several minutes...';
  RomProgressBar.Visible := True;
  RomProgressBar.Style := npbstMarquee;
  WizardForm.Repaint;

  // For ISO input, PreflightCheckRom already performs the full extraction into {app}\rom as part
  // of validating the version (see its own comment) -- CopyRomFolder only runs for folder input.
  Result := PreflightCheckRom(RomPathEdit.Text, RomIsIsoRadio.Checked, ErrorMsg);
  if Result and not RomIsIsoRadio.Checked then
    Result := CopyRomFolder(RomPathEdit.Text, ErrorMsg);

  RomProgressBar.Style := npbstNormal;
  RomProgressBar.Visible := False;

  if not Result then
    MsgBox(ErrorMsg, mbError, MB_OK)
  else
    RomStatusLabel.Caption := 'Done.';
end;

var
  DeleteRomAndSettingsOnUninstall: Boolean;

function InitializeUninstall: Boolean;
begin
  Result := True;
  DeleteRomAndSettingsOnUninstall := False;
  // WizardSilent-equivalent for the uninstaller: never block on a dialog nobody can answer during
  // an unattended uninstall (e.g. `unins000.exe /VERYSILENT`) -- default to "do not delete".
  if UninstallSilent then Exit;

  if MsgBox('Also delete the extracted game files and settings (rom\ and giantsrecomp.toml)?' + #13#10 +
      'Your saves are stored separately and are never deleted.',
      mbConfirmation, MB_YESNO) = IDYES then
    DeleteRomAndSettingsOnUninstall := True;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if (CurUninstallStep = usPostUninstall) and DeleteRomAndSettingsOnUninstall then begin
    DelTree(ExpandConstant('{app}') + '\rom', True, True, True);
    DeleteFile(ExpandConstant('{app}') + '\giantsrecomp.toml');
  end;
end;
