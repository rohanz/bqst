#define AppName "BQST"
#define AppVersion "1.0.0"
#define Publisher "Rohan Kulshrestha"
#define AppId "{{6DBFE011-DBD6-44B8-A7C4-47D8986320F2}"

[Setup]
AppId={#AppId}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher={#Publisher}
DefaultDirName={autopf}\BQST
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
DisableDirPage=yes
OutputDir=..\..\dist
OutputBaseFilename=BQST-{#AppVersion}-Windows
Compression=lzma2
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
WizardStyle=modern
WizardImageFile=wizard-image.bmp
WizardSmallImageFile=wizard-small-image.bmp
UninstallDisplayName={#AppName}

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Files]
Source: "..\..\build-windows\BQST_artefacts\Release\VST3\BQST.vst3\*"; DestDir: "{commoncf64}\VST3\BQST.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "..\..\build-windows\BQST_artefacts\Release\Standalone\BQST.exe"; DestDir: "{app}\Standalone"; Flags: ignoreversion skipifsourcedoesntexist

[Icons]
Name: "{group}\BQST"; Filename: "{app}\Standalone\BQST.exe"; Check: FileExists(ExpandConstant('{app}\Standalone\BQST.exe'))
Name: "{commondesktop}\BQST"; Filename: "{app}\Standalone\BQST.exe"; Tasks: desktopicon; Check: FileExists(ExpandConstant('{app}\Standalone\BQST.exe'))

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Run]
Filename: "{app}\Standalone\BQST.exe"; Description: "{cm:LaunchProgram,BQST}"; Flags: nowait postinstall skipifsilent; Check: FileExists(ExpandConstant('{app}\Standalone\BQST.exe'))
