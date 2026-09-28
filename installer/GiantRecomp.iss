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
