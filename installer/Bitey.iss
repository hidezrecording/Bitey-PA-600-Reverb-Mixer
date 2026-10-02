; Bitey PA-600 Reverb Mixer — Windows installer script (Inno Setup 6)
; Built on CI: choco install innosetup, then ISCC.exe installer\Bitey.iss
; Paths are relative to this script's directory (installer/).

#define AppVersion "0.1.0"

[Setup]
AppName=Bitey PA-600 Reverb Mixer
AppVersion={#AppVersion}
AppPublisher=Sabatino Audio
AppPublisherURL=https://github.com/hidezrecording/Bitey-PA-600-Reverb-Mixer
DefaultDirName={autopf}\Sabatino Audio\Bitey
DefaultGroupName=Bitey
OutputDir=output
OutputBaseFilename=Bitey-{#AppVersion}-Windows-Setup
Compression=lzma2
SolidCompression=yes
ArchitecturesInstallIn64BitMode=x64
WizardStyle=modern
PrivilegesRequired=admin

[Files]
; VST3 plugin -> shared system VST3 folder
Source: "..\build\Bitey_artefacts\Release\VST3\Bitey.vst3\*"; DestDir: "{commoncf64}\VST3\Bitey.vst3"; Flags: recursesubdirs ignoreversion
; Standalone app -> program folder
Source: "..\build\Bitey_artefacts\Release\Standalone\Bitey.exe"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\Bitey Standalone"; Filename: "{app}\Bitey.exe"
Name: "{group}\Uninstall Bitey"; Filename: "{uninstallexe}"
