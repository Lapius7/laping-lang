; Laping (.lp) インストーラー (Inno Setup)
; CI(GitHub Actions)からビルドされたWindowsバイナリを取り込んでセットアップ.exeを作る。
; ビルド前提: laping.exe と libcurl-x64.dll が installer.iss と同じフォルダにあること。

#define MyAppName "Laping"
#define MyAppVersion GetEnv("LAPING_VERSION")
#define MyAppExeName "laping.exe"

[Setup]
AppId={{8F2C1B4E-5A6D-4E3F-9C7A-2D8E1F6B9A3C}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher=Lapius7
AppPublisherURL=https://github.com/Lapius7/laping-lang
DefaultDirName={autopf}\Laping
DefaultGroupName=Laping
DisableProgramGroupPage=yes
OutputBaseFilename=laping-windows-x86_64-setup
OutputDir=.
Compression=lzma
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
ChangesEnvironment=yes
UninstallDisplayIcon={app}\{#MyAppExeName}

[Languages]
Name: "japanese"; MessagesFile: "compiler:Languages\Japanese.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[Files]
Source: "laping.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "libcurl-x64.dll"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\Laping"; Filename: "{app}\{#MyAppExeName}"
Name: "{group}\Lapingをアンインストール"; Filename: "{uninstallexe}"
Name: "{autodesktop}\Laping"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Tasks]
Name: "desktopicon"; Description: "デスクトップにショートカットを作成する"; GroupDescription: "追加のショートカット:"

[Registry]
; ユーザーPATH環境変数にインストール先を追加する。
Root: HKCU; Subkey: "Environment"; ValueType: expandsz; ValueName: "Path"; \
    ValueData: "{olddata};{app}"; Flags: preservestringtype uninsdeletevalue; \
    Check: NeedsAddPath(ExpandConstant('{app}'))

[Code]
function NeedsAddPath(Param: string): boolean;
var
  OrigPath: string;
begin
  if not RegQueryStringValue(HKEY_CURRENT_USER, 'Environment', 'Path', OrigPath) then
  begin
    Result := True;
    exit;
  end;
  Result := Pos(';' + Uppercase(Param) + ';', ';' + Uppercase(OrigPath) + ';') = 0;
end;

[Run]
Filename: "{app}\{#MyAppExeName}"; Parameters: "--version"; Description: "Lapingのバージョンを確認"; Flags: postinstall nowait skipifsilent runascurrentuser
