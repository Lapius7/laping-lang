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
DefaultDirName={autopf}\LapingLang
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

[Code]
// ユーザーPATH環境変数の操作はすべてここで明示的に行う。
// Inno Setupの [Registry] + olddataマクロ方式は、既存のPath値が
// レジストリに存在しない（空）場合に先頭の区切り文字だけが書き込まれる
// 事故が起きるため使わない。RegQueryStringValueの戻り値を必ず確認し、
// 「値が無い」場合と「値はあるが空文字列」の場合を区別して処理する。

function GetUserPath(): string;
var
  OrigPath: string;
begin
  if RegQueryStringValue(HKEY_CURRENT_USER, 'Environment', 'Path', OrigPath) then
    Result := OrigPath
  else
    Result := '';
end;

function NeedsAddPath(Param: string): boolean;
var
  OrigPath: string;
begin
  OrigPath := GetUserPath();
  if OrigPath = '' then
  begin
    Result := True;
    exit;
  end;
  Result := Pos(';' + Uppercase(Param) + ';', ';' + Uppercase(OrigPath) + ';') = 0;
end;

procedure AddAppToUserPath();
var
  OrigPath: string;
  NewPath: string;
  AppPath: string;
begin
  AppPath := ExpandConstant('{app}');
  if not NeedsAddPath(AppPath) then
    exit;
  OrigPath := GetUserPath();
  if OrigPath = '' then
    NewPath := AppPath
  else if OrigPath[Length(OrigPath)] = ';' then
    NewPath := OrigPath + AppPath
  else
    NewPath := OrigPath + ';' + AppPath;
  RegWriteStringValue(HKEY_CURRENT_USER, 'Environment', 'Path', NewPath);
end;

procedure RemoveAppFromUserPath();
var
  OrigPath: string;
  AppPath: string;
  Padded: string;
  P: Integer;
  Result_: string;
begin
  AppPath := ExpandConstant('{app}');
  OrigPath := GetUserPath();
  if OrigPath = '' then
    exit;
  // 前後に ; を補った文字列上で探し、見つかった範囲をそのまま削る。
  // 削除後に残る先頭/末尾の ; や ;; の重複は最後に正規化する。
  Padded := ';' + OrigPath + ';';
  P := Pos(';' + Uppercase(AppPath) + ';', Uppercase(Padded));
  if P = 0 then
    exit;
  Result_ := Copy(Padded, 1, P) + Copy(Padded, P + Length(AppPath) + 1, MaxInt);
  while Pos(';;', Result_) > 0 do
    Result_ := StringReplace(Result_, ';;', ';', [rfReplaceAll]);
  if (Length(Result_) > 0) and (Result_[1] = ';') then
    Delete(Result_, 1, 1);
  if (Length(Result_) > 0) and (Result_[Length(Result_)] = ';') then
    Delete(Result_, Length(Result_), 1);
  RegWriteStringValue(HKEY_CURRENT_USER, 'Environment', 'Path', Result_);
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssPostInstall then
    AddAppToUserPath();
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usPostUninstall then
    RemoveAppFromUserPath();
end;

[Run]
Filename: "{app}\{#MyAppExeName}"; Parameters: "--version"; Description: "Lapingのバージョンを確認"; Flags: postinstall nowait skipifsilent runascurrentuser
