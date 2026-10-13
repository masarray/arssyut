#ifndef AppVersion
  #define AppVersion "0.1.1-preview"
#endif
#ifndef AppPublishDir
  #error AppPublishDir must be defined as the ABSOLUTE path to the verified portable publish folder.
#endif

#define AppName "Arssyut"
#define AppExe "Arssyut.UI.exe"

; Install files built by the same verified commit as the portable package.
; Never compile a different application or substitute a stale bridge binary.
[Setup]
AppId={{F09A3126-1AFB-4433-93E5-5E09D02302CB}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher=Arssyut
AppPublisherURL=https://github.com/masarray/arssyut
AppSupportURL=https://github.com/masarray/arssyut/issues
DefaultDirName={localappdata}\Programs\Arssyut
DefaultGroupName=Arssyut
PrivilegesRequired=lowest
; The same exact ICO is embedded as the Avalonia PE icon, window icon,
; and native Win32 recorder icon. Uninstall and shortcuts inherit the GUI PE.
SetupIconFile={#SourcePath}..\..\assets\favicon\favicon.ico
UninstallDisplayIcon={app}\{#AppExe}
OutputDir={#SourcePath}..\..\dist\installer
OutputBaseFilename=Arssyut-Setup-{#AppVersion}-win-x64
Compression=lzma2
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
WizardStyle=modern
DisableProgramGroupPage=yes
CloseApplications=yes
RestartApplications=no

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Additional shortcuts:"; Flags: unchecked

[Files]
Source: "{#AppPublishDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\Arssyut"; Filename: "{app}\{#AppExe}"; WorkingDir: "{app}"
Name: "{autodesktop}\Arssyut"; Filename: "{app}\{#AppExe}"; WorkingDir: "{app}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#AppExe}"; Description: "Launch Arssyut"; Flags: nowait postinstall skipifsilent
