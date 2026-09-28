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
// "ResultCode <> 0" check would wrongly treat a normal successful copy as an error.
//
// The guard only needs an exact-match check, not a broader "SourceDir is an ancestor of DestDir"
// check: CopyRomFolder is only ever reached after PreflightCheckRom has confirmed
// SourceDir\default.xex exists, and default.xex always lives at {app}\rom\default.xex -- so
// picking {app} itself (or any other ancestor of {app}\rom) already fails that earlier check
// before CopyRomFolder is ever called. Verified empirically: pointing at {app} produces "default.xex
// was not found", never reaches this function.
function CopyRomFolder(const SourceDir: String; var ErrorMsg: String): Boolean;
var
  DestDir: String;
  ResultCode: Integer;
begin
  DestDir := ExpandConstant('{app}') + '\rom';
  if CompareText(AddBackslash(ExpandFileName(SourceDir)), AddBackslash(ExpandFileName(DestDir))) = 0 then begin
    ErrorMsg := 'The selected folder is the installed game folder itself. Choose your original extracted disc folder instead.';
    Result := False;
    Exit;
  end;
  ForceDirectories(DestDir);
  Exec(ExpandConstant('{cmd}'), '/C robocopy "' + SourceDir + '" "' + DestDir + '" /E /NFL /NDL /NJH /NJS /NC /NS /NP',
       '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
  Result := ResultCode < 8;
  if not Result then
    ErrorMsg := 'Copying the game files failed (robocopy exit code ' + IntToStr(ResultCode) + ').';
end;

// -x extract mode, -d destination directory -- confirmed against this vendored build's own
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
  Result := Exec(ExtractXisoExe, '-x -d "' + DestDir + '" "' + IsoPath + '"', '',
                 SW_HIDE, ewWaitUntilTerminated, ResultCode) and (ResultCode = 0);
  if not Result then
    ErrorMsg := 'Extracting the ISO into the install folder failed.';
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
  ExtractTemporaryFile('giantrecomp_xexcheck.exe');
  if not Exec(XexCheckExe, '"' + CandidateXex + '"', '', SW_HIDE, ewWaitUntilTerminated, ResultCode) then begin
    ErrorMsg := 'Could not run the version-check tool.';
    if IsIso then DelTree(ExpandConstant('{app}') + '\rom', True, True, True);
    Exit;
  end;
  if ResultCode <> 0 then begin
    ErrorMsg := 'This is not the supported Skylanders Giants version (1.0, USA or Europe). ' +
      'The installer will not continue with an unsupported copy of the game.';
    if IsIso then DelTree(ExpandConstant('{app}') + '\rom', True, True, True);
    Exit;
  end;

  Result := True;
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
begin
  DestPath := ExpandConstant('{app}') + '\giantsrecomp.toml';
  if FileExists(DestPath) then Exit;

  ExtractTemporaryFile('settings_template.toml');
  TemplatePath := ExpandConstant('{tmp}') + '\settings_template.toml';
  LoadStringFromFile(TemplatePath, Contents);

  ResolutionScale := Trim(ResolutionScaleEdit.Text);
  if (ResolutionScale = '') or (StrToIntDef(ResolutionScale, 0) < 1) then
    ResolutionScale := '1';

  Contents := ReplaceAll(Contents, '__PORTAL_MODE__', PortalModeTomlValue);
  Contents := ReplaceAll(Contents, '__RESOLUTION__', ResolutionCombo.Items[ResolutionCombo.ItemIndex]);
  Contents := ReplaceAll(Contents, '__RESOLUTION_SCALE__', ResolutionScale);

  SaveStringToFile(DestPath, Contents, False);
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

  PortalModeCombo := TNewComboBox.Create(SettingsPage);
  PortalModeCombo.Parent := SettingsPage.Surface;
  PortalModeCombo.Style := csDropDownList;
  PortalModeCombo.Items.Add('software (virtual Portal of Power)');
  PortalModeCombo.Items.Add('usb (real Portal of Power over USB)');
  PortalModeCombo.Items.Add('none (no portal)');
  PortalModeCombo.ItemIndex := 0;
  PortalModeCombo.Top := 0;
  PortalModeCombo.Width := SettingsPage.SurfaceWidth;

  ResolutionCombo := TNewComboBox.Create(SettingsPage);
  ResolutionCombo.Parent := SettingsPage.Surface;
  ResolutionCombo.Style := csDropDownList;
  ResolutionCombo.Items.Add('1920x1080');
  ResolutionCombo.Items.Add('2560x1440');
  ResolutionCombo.Items.Add('3840x2160');
  ResolutionCombo.ItemIndex := 0;
  ResolutionCombo.Top := PortalModeCombo.Top + PortalModeCombo.Height + 16;
  ResolutionCombo.Width := SettingsPage.SurfaceWidth;

  ResolutionScaleEdit := TNewEdit.Create(SettingsPage);
  ResolutionScaleEdit.Parent := SettingsPage.Surface;
  ResolutionScaleEdit.Text := '1';
  ResolutionScaleEdit.Top := ResolutionCombo.Top + ResolutionCombo.Height + 16;
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

  // ~7 GB for the extracted disc, plus headroom -- checked before any extraction/copy starts (see
  // Review Focus in the plan: the installer must not fail partway through a multi-gigabyte
  // extraction). Checked here regardless of ISO vs. folder input, since for ISO input the
  // extraction happens inside PreflightCheckRom below, not in a separate later step.
  FreeMB := GetFreeSpaceMB(ExpandConstant('{app}'));
  if FreeMB < 8000 then begin
    MsgBox('Not enough free disk space. At least 8 GB free is needed; ' + IntToStr(FreeMB) +
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
