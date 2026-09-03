; Inno Setup 6 脚本 — 用 windeployqt 后的目录生成安装包
; 用法：
;   1. 本地或 CI 得到 dist\GoldPriceBarLite\（含 exe + Qt DLL）
;   2. 安装 Inno Setup，编译本脚本：
;      "C:\Program Files (x86)\Inno Setup 6\ISCC.exe" packaging\GoldPriceBarLite.iss
;
; 输出：packaging\Output\GoldPriceBarLite-Setup-x64.exe

#define MyAppName "GoldPriceBarLite"
#ifndef MyAppVersion
  #define MyAppVersion "0.6.0"
#endif
#define MyAppPublisher "kzq0717"
#define MyAppURL "https://github.com/kzq0717/GoldPriceBar"
#define MyAppExeName "GoldPriceBarLite.exe"

[Setup]
AppId={{A3C8E2F1-9B4D-4E7A-8C21-GOLDPRICEBARLITE}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
OutputDir=Output
OutputBaseFilename=GoldPriceBarLite-Setup-x64
Compression=lzma
SolidCompression=yes
WizardStyle=modern
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
UninstallDisplayIcon={app}\{#MyAppExeName}

[Languages]
Name: "chinesesimplified"; MessagesFile: "compiler:Languages\ChineseSimplified.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked
Name: "autostart"; Description: "开机自动启动"; GroupDescription: "附加选项:"; Flags: unchecked

[Files]
; 发布前请把 windeployqt 目录放到 packaging\app\ 
Source: "app\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{group}\{cm:UninstallProgram,{#MyAppName}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Registry]
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: string; ValueName: "{#MyAppName}"; ValueData: """{app}\{#MyAppExeName}"""; Flags: uninsdeletevalue; Tasks: autostart

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#StringChange(MyAppName, '&', '&&')}}"; Flags: nowait postinstall skipifsilent
