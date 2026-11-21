#ifndef AppVersion
  #error "Build with Build-Installer.ps1 so AppVersion comes from the selected Release compiler."
#endif
#ifndef StageDirectory
  #error "Build with Build-Installer.ps1 so StageDirectory contains the staged local distribution."
#endif
#ifndef AppIcon
  #error "Build with Build-Installer.ps1 so AppIcon is generated from the repository logo."
#endif
#ifndef AppWizardImage
  #error "Build with Build-Installer.ps1 so AppWizardImage is generated from the repository logo."
#endif

#define AppName "Z+"
#define AppPublisher "Miroslav Aleksandrov"
#define AppURL "https://github.com/MiroXdev0/Z-lang-release"
#define ReleaseURL "https://github.com/MiroXdev0/ZPluse-release/releases"

[Setup]
AppId={{A474863A-1AE9-423A-BC40-A9D7DD3FA291}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher={#AppPublisher}
AppCopyright=Copyright (c) Miroslav Aleksandrov
AppPublisherURL={#AppURL}
AppSupportURL={#AppURL}/issues
AppUpdatesURL={#ReleaseURL}
DefaultDirName={localappdata}\Programs\ZPlus
UsePreviousAppDir=yes
DefaultGroupName=Z+
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64os
ArchitecturesInstallIn64BitMode=x64os
MinVersion=10.0
OutputDir=output
OutputBaseFilename=ZPlus-{#AppVersion}-Setup
SetupIconFile={#AppIcon}
WizardSmallImageFile={#AppWizardImage}
SetupLogging=yes
UninstallDisplayName=Z+ {#AppVersion}
UninstallDisplayIcon={app}\ZPlus.ico
WizardStyle=modern
CloseApplications=yes
RestartIfNeededByRun=no
LicenseFile={#StageDirectory}\LICENSE.txt
VersionInfoVersion={#AppVersion}
VersionInfoCompany={#AppPublisher}
VersionInfoDescription=Z+ {#AppVersion} Setup
VersionInfoProductName=Z+ Programming Language
VersionInfoProductVersion={#AppVersion}

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Additional shortcuts:"; Flags: unchecked

[Files]
Source: "{#StageDirectory}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#AppIcon}"; DestDir: "{app}"; DestName: "ZPlus.ico"; Flags: ignoreversion

[Icons]
Name: "{group}\Z+ Command Prompt"; Filename: "{cmd}"; Parameters: "/K """"{app}\bin\zp.exe"" --help"""; WorkingDir: "{app}"; IconFilename: "{app}\ZPlus.ico"
Name: "{group}\Z+ Installation Folder"; Filename: "{app}"
Name: "{group}\Uninstall Z+"; Filename: "{uninstallexe}"
Name: "{autodesktop}\Z+ Command Prompt"; Filename: "{cmd}"; Parameters: "/K """"{app}\bin\zp.exe"" --help"""; WorkingDir: "{app}"; IconFilename: "{app}\ZPlus.ico"; Tasks: desktopicon

[Code]
const
  EnvironmentKey = 'Environment';
  PathValueName = 'Path';
  UninstallRegistryPath = 'Software\Microsoft\Windows\CurrentVersion\Uninstall\{A474863A-1AE9-423A-BC40-A9D7DD3FA291}_is1';

function TryParseVersion(Version: String; var Major, Minor, Patch: Integer): Boolean;
var
  FirstDot, SecondDot: Integer;
  Remaining, MajorText, MinorText, PatchText: String;
begin
  Result := False;
  Major := -1;
  Minor := -1;
  Patch := -1;

  FirstDot := Pos('.', Version);
  if FirstDot <= 1 then Exit;
  Remaining := Copy(Version, FirstDot + 1, Length(Version));
  SecondDot := Pos('.', Remaining);
  if SecondDot <= 1 then Exit;
  PatchText := Copy(Remaining, SecondDot + 1, Length(Remaining));
  if (PatchText = '') or (Pos('.', PatchText) <> 0) then Exit;

  MajorText := Copy(Version, 1, FirstDot - 1);
  MinorText := Copy(Remaining, 1, SecondDot - 1);
  Major := StrToIntDef(MajorText, -1);
  Minor := StrToIntDef(MinorText, -1);
  Patch := StrToIntDef(PatchText, -1);
  Result := (Major >= 0) and (Minor >= 0) and (Patch >= 0);
end;

function CompareVersions(LeftVersion, RightVersion: String; var Comparison: Integer): Boolean;
var
  LeftMajor, LeftMinor, LeftPatch: Integer;
  RightMajor, RightMinor, RightPatch: Integer;
begin
  Result := False;
  Comparison := 0;
  if not TryParseVersion(LeftVersion, LeftMajor, LeftMinor, LeftPatch) then Exit;
  if not TryParseVersion(RightVersion, RightMajor, RightMinor, RightPatch) then Exit;

  if LeftMajor < RightMajor then Comparison := -1
  else if LeftMajor > RightMajor then Comparison := 1
  else if LeftMinor < RightMinor then Comparison := -1
  else if LeftMinor > RightMinor then Comparison := 1
  else if LeftPatch < RightPatch then Comparison := -1
  else if LeftPatch > RightPatch then Comparison := 1;
  Result := True;
end;

function GetRegisteredVersion(var Version: String): Boolean;
begin
  Result := RegQueryStringValue(HKCU, UninstallRegistryPath, 'DisplayVersion', Version);
  if not Result then
    Result := RegQueryStringValue(HKLM, UninstallRegistryPath, 'DisplayVersion', Version);
  if not Result then
    Result := RegQueryStringValue(
      HKLM,
      'Software\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\{A474863A-1AE9-423A-BC40-A9D7DD3FA291}_is1',
      'DisplayVersion',
      Version);
end;

function GetRegisteredInstallLocation(var InstallLocation: String): Boolean;
begin
  Result := RegQueryStringValue(HKCU, UninstallRegistryPath, 'InstallLocation', InstallLocation);
  if not Result then
    Result := RegQueryStringValue(HKLM, UninstallRegistryPath, 'InstallLocation', InstallLocation);
  if not Result then
    Result := RegQueryStringValue(
      HKLM,
      'Software\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\{A474863A-1AE9-423A-BC40-A9D7DD3FA291}_is1',
      'InstallLocation',
      InstallLocation);
end;

function InitializeSetup(): Boolean;
var
  InstalledVersion: String;
  Comparison: Integer;
begin
  Result := True;
  if not GetRegisteredVersion(InstalledVersion) then Exit;

  if not CompareVersions(InstalledVersion, '{#AppVersion}', Comparison) then
  begin
    MsgBox(
      'A Z+ installation is registered with an invalid version (' + InstalledVersion + '). Setup cannot safely determine whether this installer would downgrade it. Uninstall that installation before continuing.',
      mbError,
      MB_OK);
    Result := False;
    Exit;
  end;

  if Comparison > 0 then
  begin
    MsgBox(
      'Z+ ' + InstalledVersion + ' is already installed. This installer is Z+ {#AppVersion} and cannot downgrade the existing installation. Download the newer installer, or uninstall Z+ ' + InstalledVersion + ' before installing this version.',
      mbError,
      MB_OK);
    Result := False;
  end;
end;

function NormalizePathEntry(Value: string): string;
begin
  Result := Trim(Value);
  if (Length(Result) >= 2) and (Result[1] = '"') and
     (Result[Length(Result)] = '"') then
    Result := Copy(Result, 2, Length(Result) - 2);
  while (Length(Result) > 3) and (Result[Length(Result)] = '\') do
    Delete(Result, Length(Result), 1);
end;

function DirectoryHasEntries(Path: String): Boolean;
var
  SearchRecord: TFindRec;
begin
  Result := True;
  if FindFirst(Path + '\*', SearchRecord) then
  begin
    Result := False;
    repeat
      if (SearchRecord.Name <> '.') and (SearchRecord.Name <> '..') then
      begin
        Result := True;
        Break;
      end;
    until not FindNext(SearchRecord);
    FindClose(SearchRecord);
  end;
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
var
  InstalledVersion, ExistingLocation, SelectedLocation: String;
begin
  Result := '';
  if not GetRegisteredVersion(InstalledVersion) then
  begin
    SelectedLocation := WizardDirValue;
    if DirExists(SelectedLocation) and DirectoryHasEntries(SelectedLocation) then
      Result := 'The selected installation directory already contains files and is not a registered Z+ installation. Choose an empty directory or uninstall the existing application before continuing.';
    Exit;
  end;

  if not GetRegisteredInstallLocation(ExistingLocation) or (ExistingLocation = '') then
  begin
    Result := 'A previous Z+ installation is registered, but its installation directory could not be determined. Uninstall that installation before continuing.';
    Exit;
  end;

  SelectedLocation := WizardDirValue;
  if CompareText(
       NormalizePathEntry(ExistingLocation),
       NormalizePathEntry(SelectedLocation)) <> 0 then
    Result := 'Z+ is already installed at ' + ExistingLocation + '. Use that directory to upgrade or repair. To move Z+ to a different directory, uninstall the existing installation first.';
end;

function ExpandKnownPathVariables(Value: string): string;
begin
  Result := Value;
  StringChangeEx(Result, '%LOCALAPPDATA%', GetEnv('LOCALAPPDATA'), True);
  StringChangeEx(Result, '%USERPROFILE%', GetEnv('USERPROFILE'), True);
  StringChangeEx(Result, '%APPDATA%', GetEnv('APPDATA'), True);
end;

function HasPathEntry(PathValue, Expected: string): Boolean;
var
  Remaining, Entry: string;
  Separator: Integer;
begin
  Result := False;
  Remaining := PathValue;
  while Length(Remaining) > 0 do
  begin
    Separator := Pos(';', Remaining);
    if Separator = 0 then
    begin
      Entry := Remaining;
      Remaining := '';
    end
    else
    begin
      Entry := Copy(Remaining, 1, Separator - 1);
      Delete(Remaining, 1, Separator);
    end;
    if CompareText(
         NormalizePathEntry(ExpandKnownPathVariables(Entry)),
         NormalizePathEntry(ExpandKnownPathVariables(Expected))) = 0 then
    begin
      Result := True;
      Exit;
    end;
  end;
end;

function AddPathEntry(PathValue, EntryToAdd: string): string;
begin
  if HasPathEntry(PathValue, EntryToAdd) then
    Result := PathValue
  else if PathValue = '' then
    Result := EntryToAdd
  else
    Result := PathValue + ';' + EntryToAdd;
end;

function RemovePathEntry(PathValue, EntryToRemove: string): string;
var
  Remaining, Entry: string;
  Separator: Integer;
  FirstEntry, Removed: Boolean;
begin
  Result := '';
  Remaining := PathValue;
  FirstEntry := True;
  Removed := False;
  while True do
  begin
    Separator := Pos(';', Remaining);
    if Separator = 0 then
    begin
      Entry := Remaining;
      Remaining := '';
    end
    else
    begin
      Entry := Copy(Remaining, 1, Separator - 1);
      Delete(Remaining, 1, Separator);
    end;
    if not Removed and
       (CompareText(
         NormalizePathEntry(ExpandKnownPathVariables(Entry)),
         NormalizePathEntry(ExpandKnownPathVariables(EntryToRemove))) = 0) then
    begin
      Removed := True;
    end
    else
    begin
      if not FirstEntry then
        Result := Result + ';';
      Result := Result + Entry;
      FirstEntry := False;
    end;
    if Separator = 0 then Break;
  end;
end;

procedure CurStepChanged(CurStep: TSetupStep);
var
  CurrentPath, EntryToAdd, MarkerPath: string;
begin
  if CurStep = ssPostInstall then
  begin
    EntryToAdd := ExpandConstant('{app}\bin');
    MarkerPath := ExpandConstant('{app}\.zplus-path-added');
    if not RegQueryStringValue(HKCU, EnvironmentKey, PathValueName, CurrentPath) then
      CurrentPath := '';
    if not HasPathEntry(CurrentPath, EntryToAdd) then
    begin
      if not SaveStringToFile(MarkerPath, 'added', False) then
        RaiseException('Could not record the Z+ PATH change. Add {app}\bin to PATH manually.');
      CurrentPath := AddPathEntry(CurrentPath, EntryToAdd);
      if not RegWriteExpandStringValue(HKCU, EnvironmentKey, PathValueName, CurrentPath) then
      begin
        DeleteFile(MarkerPath);
        RaiseException('Could not update the current user PATH. Z+ is installed; add {app}\bin to PATH manually.');
      end;
    end;
  end;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  CurrentPath, UpdatedPath, MarkerPath, EntryToRemove: string;
begin
  if CurUninstallStep = usUninstall then
  begin
    MarkerPath := ExpandConstant('{app}\.zplus-path-added');
    EntryToRemove := ExpandConstant('{app}\bin');
    if FileExists(MarkerPath) then
    begin
      if RegQueryStringValue(HKCU, EnvironmentKey, PathValueName, CurrentPath) and
         HasPathEntry(CurrentPath, EntryToRemove) then
      begin
        UpdatedPath := RemovePathEntry(CurrentPath, EntryToRemove);
        if not RegWriteExpandStringValue(HKCU, EnvironmentKey, PathValueName, UpdatedPath) then
          MsgBox('Z+ was removed, but its PATH entry could not be removed.', mbError, MB_OK);
      end;
      DeleteFile(MarkerPath);
    end;
  end;
end;
