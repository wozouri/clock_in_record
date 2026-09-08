; 工时簿 Windows 安装程序。发布脚本通过 ISCC 的 /D 参数注入构建产物和版本号。
#ifndef AppVersion
  #define AppVersion "v2026.09.08"
#endif
#ifndef SourceDir
  #define SourceDir "..\dist\AttendanceApp"
#endif
#ifndef OutputDir
  #define OutputDir "..\dist\installers"
#endif

#define AppVersionNumber Copy(AppVersion, 2, 32)

[Setup]
AppId={{A2B13D47-0F13-447E-9BF7-40EB576C6F0E}
AppName=工时簿
AppVersion={#AppVersionNumber}
AppVerName=工时簿 {#AppVersion}
AppPublisher=工时簿
DefaultDirName={localappdata}\Programs\工时簿
DefaultGroupName=工时簿
DisableWelcomePage=yes
DisableProgramGroupPage=yes
OutputDir={#OutputDir}
OutputBaseFilename=AttendanceApp-Setup-{#AppVersion}
SetupIconFile=..\resources\Icons\logo.ico
UninstallDisplayIcon={app}\AttendanceApp.exe
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible

[Tasks]
Name: "desktopicon"; Description: "创建桌面快捷方式"; GroupDescription: "附加选项："

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\工时簿"; Filename: "{app}\AttendanceApp.exe"; WorkingDir: "{app}"
Name: "{autodesktop}\工时簿"; Filename: "{app}\AttendanceApp.exe"; WorkingDir: "{app}"; Tasks: desktopicon

[Run]
Filename: "{app}\vc_redist.x64.exe"; Parameters: "/install /quiet /norestart"; StatusMsg: "正在安装 Microsoft Visual C++ 运行库..."; Flags: waituntilterminated skipifdoesntexist
Filename: "{app}\AttendanceApp.exe"; Description: "启动工时簿"; Flags: nowait postinstall skipifsilent

[Code]
var
  ModePage: TWizardPage;
  QuickInstallRadio: TRadioButton;
  CustomInstallRadio: TRadioButton;
  QuickInstallDescription: TNewStaticText;
  CustomInstallDescription: TNewStaticText;

procedure UpdateInstallModeButtonCaption;
begin
  if (ModePage <> nil) and (WizardForm.CurPageID = ModePage.ID) then begin
    if QuickInstallRadio.Checked then
      WizardForm.NextButton.Caption := SetupMessage(msgButtonInstall)
    else
      WizardForm.NextButton.Caption := SetupMessage(msgButtonNext);
  end else if (WizardForm.CurPageID <> wpReady) and (WizardForm.CurPageID <> wpFinished) then
    WizardForm.NextButton.Caption := SetupMessage(msgButtonNext);
end;

procedure InstallModeClick(Sender: TObject);
begin
  UpdateInstallModeButtonCaption;
end;

procedure CreateInstallModePage;
begin
  ModePage := CreateCustomPage(wpWelcome, '安装选项', '选择安装方式');

  QuickInstallRadio := TRadioButton.Create(ModePage);
  QuickInstallRadio.Parent := ModePage.Surface;
  QuickInstallRadio.Left := ScaleX(40);
  QuickInstallRadio.Top := ScaleY(32);
  QuickInstallRadio.Width := ModePage.SurfaceWidth - ScaleX(40);
  QuickInstallRadio.Caption := '快速安装（推荐）';
  QuickInstallRadio.Checked := True;
  QuickInstallRadio.Font.Style := [fsBold];
  QuickInstallRadio.OnClick := @InstallModeClick;

  QuickInstallDescription := TNewStaticText.Create(ModePage);
  QuickInstallDescription.Parent := ModePage.Surface;
  QuickInstallDescription.Left := ScaleX(56);
  QuickInstallDescription.Top := QuickInstallRadio.Top + ScaleY(24);
  QuickInstallDescription.Width := ModePage.SurfaceWidth - ScaleX(72);
  QuickInstallDescription.Caption := '使用默认目录和默认选项，直接开始安装。';
  QuickInstallDescription.Font.Color := clGray;

  CustomInstallRadio := TRadioButton.Create(ModePage);
  CustomInstallRadio.Parent := ModePage.Surface;
  CustomInstallRadio.Left := ScaleX(40);
  CustomInstallRadio.Top := QuickInstallDescription.Top + ScaleY(52);
  CustomInstallRadio.Width := ModePage.SurfaceWidth - ScaleX(40);
  CustomInstallRadio.Caption := '自定义安装';
  CustomInstallRadio.Font.Style := [fsBold];
  CustomInstallRadio.OnClick := @InstallModeClick;

  CustomInstallDescription := TNewStaticText.Create(ModePage);
  CustomInstallDescription.Parent := ModePage.Surface;
  CustomInstallDescription.Left := ScaleX(56);
  CustomInstallDescription.Top := CustomInstallRadio.Top + ScaleY(24);
  CustomInstallDescription.Width := ModePage.SurfaceWidth - ScaleX(72);
  CustomInstallDescription.Caption := '选择安装目录和桌面快捷方式。';
  CustomInstallDescription.Font.Color := clGray;
end;

function ShouldSkipPage(PageID: Integer): Boolean;
begin
  Result := False;
  if (ModePage <> nil) and QuickInstallRadio.Checked then begin
    case PageID of
      wpSelectDir,
      wpSelectComponents,
      wpSelectProgramGroup,
      wpSelectTasks,
      wpReady:
        Result := True;
    end;
  end;
end;

procedure InitializeWizard;
begin
  if not WizardSilent then begin
    CreateInstallModePage;
    UpdateInstallModeButtonCaption;
  end;
end;

procedure CurPageChanged(CurPageID: Integer);
begin
  UpdateInstallModeButtonCaption;
end;
