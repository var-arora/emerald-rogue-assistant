#ifndef AppVersion
  #error AppVersion is required
#endif
#ifndef VersionCore
  #error VersionCore is required
#endif
#ifndef BuildLabel
  #error BuildLabel is required
#endif
#ifndef StageDir
  #error StageDir is required
#endif
#ifndef OutputPath
  #error OutputPath is required
#endif

[Setup]
AppId=assistant.emerald.rogue
AppName=Emerald Rogue Assistant
AppVersion={#AppVersion}
AppPublisher=Emerald Rogue Assistant Project
AppPublisherURL=https://github.com/var-arora/emerald-rogue-assistant
AppSupportURL=https://github.com/var-arora/emerald-rogue-assistant/issues
DefaultDirName={localappdata}\Programs\Emerald Rogue Assistant
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
DisableProgramGroupPage=yes
UninstallDisplayIcon={app}\RogueAssistant.exe
SetupIconFile={#StageDir}\resources\WobbuffetIcon.ico
OutputDir={#OutputPath}
OutputBaseFilename=RogueAssistant-{#BuildLabel}-windows-x64
VersionInfoVersion={#VersionCore}
VersionInfoProductTextVersion={#AppVersion}
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
CloseApplications=no
RestartApplications=no

[Files]
Source: "{#StageDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\Emerald Rogue Assistant"; Filename: "{app}\RogueAssistant.exe"; WorkingDir: "{app}"

[Run]
Filename: "{app}\RogueAssistant.exe"; Description: "Open Emerald Rogue Assistant"; Flags: nowait postinstall skipifsilent unchecked
