; 工时簿更新服务 Windows 安装程序。
#ifndef AppVersion
  #define AppVersion "v2026.09.08"
#endif
#ifndef SourceDir
  #define SourceDir "..\dist\AttendanceUpdateService"
#endif
#ifndef OutputDir
  #define OutputDir "..\dist\installers"
#endif

#define AppVersionNumber Copy(AppVersion, 2, 32)

[Setup]
AppId={{99F6A2EA-DF0B-4824-9A36-B0344E961E83}
AppName=工时簿更新服务
AppVersion={#AppVersionNumber}
AppVerName=工时簿更新服务 {#AppVersion}
AppPublisher=工时簿
DefaultDirName={autopf}\AttendanceUpdateService
DefaultGroupName=工时簿更新服务
DisableProgramGroupPage=yes
OutputDir={#OutputDir}
OutputBaseFilename=AttendanceUpdateService-Setup-{#AppVersion}
SetupIconFile=..\resources\Icons\logo.ico
UninstallDisplayIcon={app}\AttendanceUpdateService.exe
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "..\service\updateservice.ini"; DestDir: "{app}"; Flags: onlyifdoesntexist

[Run]
Filename: "{app}\vc_redist.x64.exe"; Parameters: "/install /quiet /norestart"; StatusMsg: "正在安装 Microsoft Visual C++ 运行库..."; Flags: waituntilterminated skipifdoesntexist

[UninstallRun]
Filename: "{sys}\sc.exe"; Parameters: "stop AttendanceUpdateService"; Flags: runhidden waituntilterminated; RunOnceId: "StopAttendanceUpdateService"
Filename: "{sys}\sc.exe"; Parameters: "delete AttendanceUpdateService"; Flags: runhidden waituntilterminated; RunOnceId: "DeleteAttendanceUpdateService"

[Code]
function ServiceIsRemoved: Boolean;
var
  OutputPath: String;
  OutputText: AnsiString;
  ResultCode: Integer;
begin
  OutputPath := ExpandConstant('{tmp}\AttendanceUpdateService-query.txt');
  DeleteFile(OutputPath);
  Exec(ExpandConstant('{cmd}'), '/C ' + ExpandConstant('{sys}\sc.exe') +
    ' query AttendanceUpdateService > ' + AddQuotes(OutputPath) + ' 2>&1', '', SW_HIDE,
    ewWaitUntilTerminated, ResultCode);
  if LoadStringFromFile(OutputPath, OutputText) then
    Result := Pos('1060', OutputText) > 0
  else
    Result := False;
end;

function WaitForServiceRemoval: Boolean;
var
  Index: Integer;
begin
  for Index := 1 to 20 do begin
    if ServiceIsRemoved() then begin
      Result := True;
      exit;
    end;
    Sleep(500);
  end;
  Result := False;
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
var
  ResultCode: Integer;
begin
  { 旧版本的 DLL 可能已不兼容，升级时不能依赖旧 EXE 执行卸载。 }
  Exec(ExpandConstant('{sys}\sc.exe'), 'stop AttendanceUpdateService', '', SW_HIDE,
    ewWaitUntilTerminated, ResultCode);
  Sleep(500);
  Exec(ExpandConstant('{sys}\sc.exe'), 'delete AttendanceUpdateService', '', SW_HIDE,
    ewWaitUntilTerminated, ResultCode);
  if WaitForServiceRemoval() then
    Result := ''
  else
    Result := '旧的工时簿更新服务尚未停止，请稍后重试安装。';
end;

procedure CurStepChanged(CurStep: TSetupStep);
var
  ResultCode: Integer;
begin
  if CurStep <> ssPostInstall then
    exit;

  WizardForm.StatusLabel.Caption := '正在注册更新服务...';
  if (not Exec(ExpandConstant('{app}\AttendanceUpdateService.exe'), '--install', '', SW_HIDE,
      ewWaitUntilTerminated, ResultCode)) or (ResultCode <> 0) then begin
    MsgBox('更新服务注册失败，错误码：' + IntToStr(ResultCode), mbError, MB_OK);
    Abort;
  end;

  WizardForm.StatusLabel.Caption := '正在启动更新服务...';
  if (not Exec(ExpandConstant('{sys}\sc.exe'), 'start AttendanceUpdateService', '', SW_HIDE,
      ewWaitUntilTerminated, ResultCode)) or (ResultCode <> 0) then begin
    MsgBox('更新服务启动失败，错误码：' + IntToStr(ResultCode), mbError, MB_OK);
    Abort;
  end;
end;
