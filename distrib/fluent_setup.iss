; Apply the selected appearance through MPC-HC's existing profile keys.
; Never import a user's INI, history, paths or stream credentials.
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

procedure ApplyFluentStyle();
var
  Encoded, Section: String;
  Bytes: AnsiString;
  I: Integer;
begin
  FluentProfileInt('Settings', 'HideCaptionMenu', 3);
  FluentProfileInt('Settings', 'ControlState', 3);
  FluentProfileInt('Settings', 'HideWindowedControls', 1);
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
