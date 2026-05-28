; build_installer.iss
; -------------------
; Inno Setup script for PDF → JPG Batch Converter.
;
; Prerequisites:
;   1. Run build_windows.bat first to produce dist\pdf_to_jpg\
;   2. Install Inno Setup 6 (free): https://jrsoftware.org/isinfo.php
;
; To compile:
;   Open this file in the Inno Setup IDE and press F9, OR run:
;     "C:\Program Files (x86)\Inno Setup 6\ISCC.exe" build_installer.iss
;
; Output:
;   installer_output\PDF_to_JPG_Setup.exe
;
; What the installer does:
;   - Installs per-user (no admin required)
;   - Copies app to %LOCALAPPDATA%\PDF to JPG Converter\
;   - Creates Start Menu shortcut
;   - Optionally creates Desktop shortcut
;   - Registers proper Add/Remove Programs entry with uninstaller
;   - Shows a Poppler install reminder page before finishing
;   - Handles upgrades cleanly (no manual uninstall needed)

#define AppName      "PDF to JPG Converter"
#define AppVersion   "1.0.0"
#define AppPublisher "Mark"
#define AppExeName   "pdf_to_jpg.exe"
#define SourceDir    "dist\pdf_to_jpg"
#define PopplerURL   "https://github.com/oschwartz10612/poppler-windows/releases"

[Setup]
AppId={{B3E7F1C2-55A9-4E8B-A2D4-9F3C2B6E8A51}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher={#AppPublisher}

; Install location — per-user, no admin required
DefaultDirName={localappdata}\{#AppName}
DefaultGroupName={#AppName}
DisableDirPage=yes

; No admin rights needed
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog

; Output
OutputDir=installer_output
OutputBaseFilename=PDF_to_JPG_Setup
Compression=lzma2/ultra64
SolidCompression=yes

; Appearance
WizardStyle=modern
WizardSizePercent=110
DisableWelcomePage=no
DisableReadyPage=no

; Handle upgrades — close running instance if needed
CloseApplications=yes
CloseApplicationsFilter=*{#AppExeName}
RestartApplications=no

; Uncomment to add a custom icon:
; SetupIconFile=assets\pdf_to_jpg.ico
; UninstallDisplayIcon={app}\{#AppExeName}

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
; Optional Desktop shortcut — unchecked by default
Name: "desktopicon"; Description: "Create a &Desktop shortcut"; \
    GroupDescription: "Additional icons:"; Flags: unchecked

[Files]
; Copy the full PyInstaller onedir bundle
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
; Start Menu shortcut
Name: "{group}\{#AppName}";        Filename: "{app}\{#AppExeName}"
Name: "{group}\Uninstall {#AppName}"; Filename: "{uninstallexe}"

; Desktop shortcut (only if user selected the task above)
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExeName}"; \
    Tasks: desktopicon

[Run]
; Offer to launch the app immediately after install
Filename: "{app}\{#AppExeName}"; \
    Description: "Launch {#AppName} now"; \
    Flags: nowait postinstall skipifsilent

[UninstallDelete]
; Clean up any leftover files on uninstall
Type: filesandordirs; Name: "{app}"

; ---------------------------------------------------------------------------
; Custom page — Poppler requirement reminder
; ---------------------------------------------------------------------------
; Shown before the Ready page so users know what they still need to install.

[Code]

var
  PopplerPage: TWizardPage;
  PopplerLabel: TLabel;
  PopplerLinkLabel: TNewStaticText;

procedure InitializeWizard();
begin
  // Insert custom info page before the Ready page
  PopplerPage := CreateCustomPage(
    wpSelectTasks,
    'Poppler Required',
    'PDF to JPG Converter needs Poppler to render PDFs.'
  );

  PopplerLabel := TLabel.Create(PopplerPage);
  PopplerLabel.Parent := PopplerPage.Surface;
  PopplerLabel.Left   := 0;
  PopplerLabel.Top    := 0;
  PopplerLabel.Width  := PopplerPage.SurfaceWidth;
  PopplerLabel.Height := 140;
  PopplerLabel.WordWrap := True;
  PopplerLabel.Caption :=
    'This application uses Poppler to convert PDF pages to images. ' +
    'Poppler is a free, open-source PDF rendering library that must be ' +
    'installed separately on your machine.' + #13#10 + #13#10 +
    'If you have not already done so, please:' + #13#10 +
    '  1. Download the latest Poppler for Windows release from the link below.' + #13#10 +
    '  2. Extract it to a permanent location (e.g. C:\poppler).' + #13#10 +
    '  3. Add the \Library\bin subfolder to your system PATH.' + #13#10 + #13#10 +
    'The converter will show an error on startup if Poppler is not found.' + #13#10 + #13#10 +
    'Poppler download page:';

  PopplerLinkLabel := TNewStaticText.Create(PopplerPage);
  PopplerLinkLabel.Parent := PopplerPage.Surface;
  PopplerLinkLabel.Left   := 0;
  PopplerLinkLabel.Top    := PopplerLabel.Top + PopplerLabel.Height;
  PopplerLinkLabel.Width  := PopplerPage.SurfaceWidth;
  PopplerLinkLabel.Caption := '{#PopplerURL}';
  PopplerLinkLabel.Font.Color := clBlue;
  PopplerLinkLabel.Font.Style := [fsUnderline];
  PopplerLinkLabel.Cursor := crHand;
end;

// Open the Poppler URL when the link is clicked
procedure PopplerLinkLabelClick(Sender: TObject);
var
  ErrorCode: Integer;
begin
  ShellExecAsOriginalUser('open', '{#PopplerURL}', '', '', SW_SHOWNORMAL, ewNoWait, ErrorCode);
end;

procedure CurPageChanged(CurPageID: Integer);
begin
  if CurPageID = PopplerPage.ID then
    PopplerLinkLabel.OnClick := @PopplerLinkLabelClick;
end;
