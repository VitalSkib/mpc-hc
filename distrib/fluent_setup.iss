// Apply the selected appearance through MPC-HC's existing profile keys.
// Never import a user's INI, history, paths or stream credentials.
var
  FluentHoverMigrationNeeded: Boolean;
  FluentTasksInitialized: Boolean;

procedure CurPageChanged(CurPageID: Integer);
var
  I: Integer;
begin
  if (CurPageID <> wpSelectTasks) or FluentTasksInitialized or WizardSilent then Exit;
  FluentTasksInitialized := True;
  // Respect explicit unattended task selections, without resetting other tasks.
  for I := 1 to ParamCount do
    if (Pos('/TASKS=', Uppercase(ParamStr(I))) = 1) or
       (Pos('/MERGETASKS=', Uppercase(ParamStr(I))) = 1) then Exit;
  WizardSelectTasks('fluent_style');
end;

procedure CaptureFluentHoverMigration();
var
  PreviousVersion: String;
begin
  FluentHoverMigrationNeeded :=
    (GetPreviousData('FluentHoverDefaultsVersion', '') <> '2') and
    RegQueryStringValue(HKLM, 'Software\Microsoft\Windows\CurrentVersion\Uninstall\{2ACBF1FA-F5C3-4B19-A774-B22A31F231B9}_is1', 'DisplayVersion', PreviousVersion) and
    ((PreviousVersion = '2.8.4.10006') or (PreviousVersion = '2.8.4.10008'));
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
var
  Version: TWindowsVersion;
  Code: Integer;
  Probe, Bootstrap: String;
begin
  Result := '';
  GetWindowsVersionEx(Version);
  if (Version.Major < 10) or ((Version.Major = 10) and (Version.Build < 17763)) then Exit;
  ExtractTemporaryFile('FluentRuntimeCheck.exe');
  ExtractTemporaryFile('Microsoft.WindowsAppRuntime.Bootstrap.dll');
  Probe := ExpandConstant('{tmp}\FluentRuntimeCheck.exe');
  Bootstrap := '"' + ExpandConstant('{tmp}\Microsoft.WindowsAppRuntime.Bootstrap.dll') + '"';
  if Exec(Probe, Bootstrap, '', SW_HIDE, ewWaitUntilTerminated, Code) and (Code = 0) then Exit;
  // Install the shared Microsoft prerequisite only when a compatible runtime is missing.
  // Never use --force: other Windows App SDK applications must not be terminated.
  ExtractTemporaryFile('WindowsAppRuntimeInstall-x64.exe');
  if not Exec(ExpandConstant('{tmp}\WindowsAppRuntimeInstall-x64.exe'), '--quiet', '', SW_HIDE, ewWaitUntilTerminated, Code) then begin
    Result := 'Could not start Windows App Runtime setup.';
    Exit;
  end;
  if Code = 3010 then NeedsRestart := True
  else if Code <> 0 then begin
    Result := 'Windows App Runtime setup failed (' + IntToStr(Code) + '). Close applications and run setup again.';
    Exit;
  end;
  if not Exec(Probe, Bootstrap, '', SW_HIDE, ewWaitUntilTerminated, Code) or (Code <> 0) then
    Result := 'Windows App Runtime 1.8 could not be activated. Restart Windows and run setup again.';
end;

procedure FluentProfileInt(Section, Name: String; Value: Cardinal);
begin
  if FileExists(ExpandConstant('{app}\{#mpchc_ini}')) then
    SetIniInt(Section, Name, Value, ExpandConstant('{app}\{#mpchc_ini}'))
  else
    RegWriteDWordValue(HKCU, 'Software\MPC-HC\MPC-HC\' + Section, Name, Value);
end;

procedure FluentProfileString(Section, Name, Value: String);
begin
  if FileExists(ExpandConstant('{app}\{#mpchc_ini}')) then
    SetIniString(Section, Name, Value, ExpandConstant('{app}\{#mpchc_ini}'))
  else
    RegWriteStringValue(HKCU, 'Software\MPC-HC\MPC-HC\' + Section, Name, Value);
end;

function FluentProfileIntValue(Section, Name: String; Default: Cardinal): Cardinal;
begin
  if FileExists(ExpandConstant('{app}\{#mpchc_ini}')) then
    Result := GetIniInt(Section, Name, Default, 0, $7FFFFFFF, ExpandConstant('{app}\{#mpchc_ini}'))
  else if not RegQueryDWordValue(HKCU, 'Software\MPC-HC\MPC-HC\' + Section, Name, Result) then
    Result := Default;
end;

procedure RepairFluentHoverDefaults();
begin
  if not FluentHoverMigrationNeeded then Exit;
  // Repair the incomplete native hover defaults independently of toolbar artwork.
  // Preserve other profiles and subsequent explicit edge-only preferences.
  if (FluentProfileIntValue('Settings', 'HideCaptionMenu', 0) = 3) and
      (FluentProfileIntValue('Settings', 'HideWindowedControls', 0) = 1) and
      (FluentProfileIntValue('Settings', 'HideFullscreenControlsPolicy', 1) = 1) and
      (FluentProfileIntValue('Settings', 'HideFullscreenControlsDelay', 0) = 0) then begin
    FluentProfileInt('Settings', 'HideFullscreenControls', 1);
    FluentProfileInt('Settings', 'HideFullscreenControlsPolicy', 2);
    FluentProfileInt('Settings', 'HideFullscreenControlsDelay', 4000);
  end;
end;

procedure RegisterPreviousData(PreviousDataKey: Integer);
begin
  SetPreviousData(PreviousDataKey, 'FluentHoverDefaultsVersion', '2');
end;

procedure ApplyFluentStyle();
var
  Encoded, Section: String;
  Bytes: AnsiString;
  I: Integer;
begin
  // Updating an active Fluent profile must preserve its artwork and layout.
  if (FluentProfileIntValue('Settings', 'HideCaptionMenu', 0) = 3) and
      (FluentProfileIntValue('Settings', 'HideWindowedControls', 0) = 1) and
      (FluentProfileIntValue('Settings', 'MPCTheme', 0) = 1) then Exit;
  FluentProfileInt('Settings', 'HideCaptionMenu', 3);
  FluentProfileInt('Settings', 'ControlState', 3);
  FluentProfileInt('Settings', 'HideWindowedControls', 1);
  FluentProfileInt('Settings', 'HideFullscreenControls', 1);
  FluentProfileInt('Settings', 'HideFullscreenControlsPolicy', 2);
  FluentProfileInt('Settings', 'HideFullscreenControlsDelay', 4000);
  FluentProfileInt('Settings', 'MPCTheme', 1);
  FluentProfileInt('Settings', 'ModernThemeMode', 0);
  FluentProfileInt('Settings', 'ModernThemeStyle', 2);
  Section := 'Toolbars\PlayerToolBar';
  FluentProfileInt(Section, 'DefaultToolbarSize', 24);
  FluentProfileInt(Section, 'ToolbarType', 2);
  FluentProfileInt(Section, 'ToolbarAlignment', 1);
  FluentProfileString(Section, 'ToolbarName', 'Custom Fluent');
  FluentProfileInt(Section, 'ButtonLayoutRevision', 1);
  FluentProfileInt(Section, 'FluentTransportLayoutVersion', 2);
  // Native MFC binary profile encoding of the accepted transport and optional buttons.
  Encoded := 'NLDAAAAAJJDAAAAAOHDAAAAAFIDAAAAAMHDAAAAAHHDAAAAAIHDAAAAALHDAAAAAGIDAAAAAPHDAAAAAKJDAAAAAMODAAAAAKODAAAAAOCDAAAAAPCDAAAAABLDAAAAANIDAAAAA';
  FluentProfileInt(Section, 'ButtonSequenceSize', Length(Encoded) div 2);
  if FileExists(ExpandConstant('{app}\{#mpchc_ini}')) then
    SetIniString(Section, 'ButtonSequence', Encoded, ExpandConstant('{app}\{#mpchc_ini}'))
  else begin
    SetLength(Bytes, Length(Encoded) div 2);
    for I := 1 to Length(Bytes) do
      Bytes[I] := Chr((Ord(Encoded[I * 2 - 1]) - Ord('A')) or ((Ord(Encoded[I * 2]) - Ord('A')) shl 4));
    RegWriteBinaryValue(HKCU, 'Software\MPC-HC\MPC-HC\' + Section, 'ButtonSequence', Bytes);
  end;
end;
