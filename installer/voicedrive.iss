#define AppVersion "1.0.0"

[Setup]
AppName=VoiceDrive
AppVersion={#AppVersion}
DefaultDirName={autopf}\Common Files\VST3
DisableProgramGroupPage=yes
Uninstallable=no
CreateUninstallRegKey=no
OutputDir=..\build\VoiceDrive_artefacts\Release\VST3
OutputBaseFilename=VoiceDrive-{#AppVersion}-setup
Compression=lzma2/max
SolidCompression=yes
ArchitecturesInstallIn64BitMode=x64compatible
DirExistsWarning=no

[Files]
Source: "..\build\VoiceDrive_artefacts\Release\VST3\VoiceDrive.vst3"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs
