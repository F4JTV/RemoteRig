; ============================================================================
;  RemoteRig - Inno Setup script (bilingual English / French)
;
;  Build the two executables first, then run build_all.bat, which gathers
;  everything the installer needs into installer\dist and compiles this
;  file with Inno Setup 6 (ISCC.exe RemoteRig.iss).
;
;  The user picks which programs to install: both, the server only, or the
;  client only.
; ============================================================================

#define AppName        "RemoteRig"
#define AppVersion     "1.0.0"
#define AppPublisher   "RemoteRig Project"
#define AppURL         "https://github.com/"
#define ServerExe      "remoterig-server.exe"
#define ClientExe      "remoterig-client.exe"

[Setup]
AppId={{D2CCCD5B-65F3-4B42-802D-FFCAC7507FBC}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher={#AppPublisher}
AppPublisherURL={#AppURL}
AppSupportURL={#AppURL}
AppUpdatesURL={#AppURL}
VersionInfoVersion={#AppVersion}.0
VersionInfoCompany={#AppPublisher}
VersionInfoDescription={#AppName} setup
VersionInfoCopyright=MIT licence
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
UninstallDisplayName={#AppName} {#AppVersion}
UninstallDisplayIcon={app}\{#ServerExe}
LicenseFile=..\LICENSE.txt
OutputDir=output
OutputBaseFilename=RemoteRig-{#AppVersion}-setup
SetupIconFile=..\icons\remoterig.ico
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
DisableProgramGroupPage=yes
ShowLanguageDialog=yes

; --------------------------------------------------------------- languages
[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "french";  MessagesFile: "compiler:Languages\French.isl"

[CustomMessages]
; ---- English
english.ComponentsTitle=Which programs do you want to install?
english.CompServer=Station server (runs next to the transceiver)
english.CompClient=Operator client (runs where you are)
english.CompShared=Shared runtime libraries
english.TypeFull=Both programs
english.TypeServer=Station server only
english.TypeClient=Operator client only
english.TypeCustom=Custom
english.TaskDesktopServer=Create a desktop shortcut for the server
english.TaskDesktopClient=Create a desktop shortcut for the client
english.TaskFirewall=Allow RemoteRig through the Windows firewall (TCP 7300, UDP 7301)
english.LaunchServer=Start the station server
english.LaunchClient=Start the operator client
english.NeedComponent=Please select at least one program to install.

; ---- Français
french.ComponentsTitle=Quels programmes voulez-vous installer ?
french.CompServer=Serveur de station (tourne à côté du poste)
french.CompClient=Client opérateur (tourne là où vous êtes)
french.CompShared=Bibliothèques d'exécution communes
french.TypeFull=Les deux programmes
french.TypeServer=Serveur de station seul
french.TypeClient=Client opérateur seul
french.TypeCustom=Personnalisé
french.TaskDesktopServer=Créer un raccourci du serveur sur le Bureau
french.TaskDesktopClient=Créer un raccourci du client sur le Bureau
french.TaskFirewall=Autoriser RemoteRig dans le pare-feu Windows (TCP 7300, UDP 7301)
french.LaunchServer=Démarrer le serveur de station
french.LaunchClient=Démarrer le client opérateur
french.NeedComponent=Veuillez sélectionner au moins un programme à installer.

; -------------------------------------------------------------- components
[Types]
Name: "full";   Description: "{cm:TypeFull}"
Name: "server"; Description: "{cm:TypeServer}"
Name: "client"; Description: "{cm:TypeClient}"
Name: "custom"; Description: "{cm:TypeCustom}"; Flags: iscustom

[Components]
Name: "server"; Description: "{cm:CompServer}"; Types: full server
Name: "client"; Description: "{cm:CompClient}"; Types: full client

[Tasks]
Name: "desktopserver"; Description: "{cm:TaskDesktopServer}"; \
    GroupDescription: "{cm:AdditionalIcons}"; Components: server
Name: "desktopclient"; Description: "{cm:TaskDesktopClient}"; \
    GroupDescription: "{cm:AdditionalIcons}"; Components: client
Name: "firewall";      Description: "{cm:TaskFirewall}"; Components: server

; ------------------------------------------------------------------- files
[Files]
; The two programs, each tied to its component.
Source: "dist\{#ServerExe}"; DestDir: "{app}"; Components: server; Flags: ignoreversion
Source: "dist\{#ClientExe}"; DestDir: "{app}"; Components: client; Flags: ignoreversion

; Hamlib runtime: only useful to the server.
Source: "dist\libhamlib-4.dll";     DestDir: "{app}"; Components: server; Flags: ignoreversion skipifsourcedoesntexist
Source: "dist\libusb-1.0.dll";      DestDir: "{app}"; Components: server; Flags: ignoreversion skipifsourcedoesntexist
Source: "dist\libgcc_s_seh-1.dll";  DestDir: "{app}"; Components: server; Flags: ignoreversion skipifsourcedoesntexist
Source: "dist\libwinpthread-1.dll"; DestDir: "{app}"; Components: server; Flags: ignoreversion skipifsourcedoesntexist

; Everything else (Qt, PortAudio, Opus, plugin folders) is shared by both
; programs and always installed.
Source: "dist\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs; \
    Excludes: "{#ServerExe},{#ClientExe},libhamlib-4.dll,libusb-1.0.dll,libgcc_s_seh-1.dll,libwinpthread-1.dll"

Source: "..\README.md";    DestDir: "{app}"; Flags: ignoreversion isreadme
Source: "..\README.fr.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\LICENSE.txt";  DestDir: "{app}"; Flags: ignoreversion

; ------------------------------------------------------------------- icons
[Icons]
Name: "{group}\RemoteRig Server"; Filename: "{app}\{#ServerExe}"; Components: server
Name: "{group}\RemoteRig Client"; Filename: "{app}\{#ClientExe}"; Components: client
Name: "{group}\{cm:UninstallProgram,{#AppName}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\RemoteRig Server"; Filename: "{app}\{#ServerExe}"; \
    Components: server; Tasks: desktopserver
Name: "{autodesktop}\RemoteRig Client"; Filename: "{app}\{#ClientExe}"; \
    Components: client; Tasks: desktopclient

; --------------------------------------------------------------------- run
[Run]
Filename: "{sys}\netsh.exe"; Tasks: firewall; Flags: runhidden; \
    Parameters: "advfirewall firewall add rule name=""RemoteRig control (TCP 7300)"" dir=in action=allow protocol=TCP localport=7300"
Filename: "{sys}\netsh.exe"; Tasks: firewall; Flags: runhidden; \
    Parameters: "advfirewall firewall add rule name=""RemoteRig audio (UDP 7301)"" dir=in action=allow protocol=UDP localport=7301"

Filename: "{app}\{#ServerExe}"; Description: "{cm:LaunchServer}"; \
    Components: server; Flags: nowait postinstall skipifsilent unchecked
Filename: "{app}\{#ClientExe}"; Description: "{cm:LaunchClient}"; \
    Components: client; Flags: nowait postinstall skipifsilent unchecked

[UninstallRun]
Filename: "{sys}\netsh.exe"; RunOnceId: "DelFwTcp"; Flags: runhidden; \
    Parameters: "advfirewall firewall delete rule name=""RemoteRig control (TCP 7300)"""
Filename: "{sys}\netsh.exe"; RunOnceId: "DelFwUdp"; Flags: runhidden; \
    Parameters: "advfirewall firewall delete rule name=""RemoteRig audio (UDP 7301)"""

; ------------------------------------------------------------------- code
[Code]
// Unticking both components would install runtime libraries and nothing else.
function NextButtonClick(CurPageID: Integer): Boolean;
begin
  Result := True;
  if CurPageID = wpSelectComponents then
    if not (IsComponentSelected('server') or IsComponentSelected('client')) then
    begin
      MsgBox(ExpandConstant('{cm:NeedComponent}'), mbError, MB_OK);
      Result := False;
    end;
end;
