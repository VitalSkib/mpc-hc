[CmdletBinding()]
param([Parameter(Mandatory)][string]$Setup,[Parameter(Mandatory)][string]$Version)
$ErrorActionPreference='Stop'
if($env:GITHUB_ACTIONS -ne 'true'){throw 'Run this deployment test only on a disposable GitHub runner'}
$Setup=(Resolve-Path -LiteralPath $Setup).Path
$folder=Join-Path $env:RUNNER_TEMP ('Fluent Setup Test '+[guid]::NewGuid().ToString('N'))
$profile='HKCU:\Software\MPC-HC\MPC-HC'
if(Test-Path -LiteralPath $profile){throw 'Deployment test requires a clean user profile'}
function Install-Player([string]$Tasks) {
  $arguments="/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /SP- /DIR=`"$folder`" /TASKS=`"$Tasks`" /LOG=`"$folder-install.log`""
  $process=Start-Process -FilePath $Setup -ArgumentList $arguments -WindowStyle Hidden -Wait -PassThru
  if($process.ExitCode -ne 0){throw "Setup failed: $($process.ExitCode)"}
}
Install-Player 'fluent_style'
foreach($name in 'mpc-hc64.exe','mpciconlib.dll','d3dcompiler_47.dll','d3dx9_43.dll','MediaInfo.dll',
    'LAVFilters64/LAVAudio.ax','LAVFilters64/LAVVideo.ax','LAVFilters64/LAVSplitter.ax',
    'LAVFilters64/LAVFilters.Dependencies.manifest','MPCVR/MpcVideoRenderer64.ax',
    'Lang/mpcresources.ru.dll','Toolbars/Custom Fluent/buttons32.svg','unins000.exe') {
  if(-not(Test-Path -LiteralPath (Join-Path $folder $name) -PathType Leaf)){throw "Missing installed component: $name"}
}
$v=(Get-Item -LiteralPath "$folder/mpc-hc64.exe").VersionInfo
if("$($v.FileMajorPart).$($v.FileMinorPart).$($v.FileBuildPart).$($v.FilePrivatePart)" -ne $Version){throw 'Installed player version mismatch'}
if(Test-Path -LiteralPath "$folder/mpc-hc64.ini"){throw 'Fresh setup must use native registry settings'}
if((Get-ItemProperty "$profile/Settings").HideCaptionMenu -ne 3){throw 'Fresh Fluent appearance was not applied'}
if((Get-ItemProperty "$profile/Toolbars/PlayerToolBar").ToolbarName -ne 'Custom Fluent'){throw 'Toolbar theme not applied'}
function Assert-WindowHover {
  $settings=Get-ItemProperty "$profile/Settings"
  if($settings.HideFullscreenControls -ne 1 -or $settings.HideFullscreenControlsPolicy -ne 2 -or $settings.HideFullscreenControlsDelay -ne 4000){throw 'Fluent preset must use whole-window cursor movement and a four-second timeout'}
}
Assert-WindowHover
$uninstallKey='HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\{2ACBF1FA-F5C3-4B19-A774-B22A31F231B9}_is1'
function Simulate-LegacySetupMarker([string]$LegacyVersion='2.8.4.10006',[string]$Marker='') {
  $names=@((Get-ItemProperty -LiteralPath $uninstallKey).PSObject.Properties.Name|Where-Object {$_ -like '*FluentHoverDefaultsVersion*'})
  if($names.Count -ne 1){throw 'Native installer migration marker missing'}
  if($Marker){Set-ItemProperty -LiteralPath $uninstallKey -Name $names[0] -Value $Marker}
  else{Remove-ItemProperty -LiteralPath $uninstallKey -Name $names[0]}
  Set-ItemProperty -LiteralPath $uninstallKey -Name DisplayVersion -Value $LegacyVersion
}
Set-ItemProperty "$profile/Toolbars/PlayerToolBar" -Name ToolbarName -Value 'Fluent Soft'
Set-ItemProperty "$profile/Toolbars/PlayerToolBar" -Name ToolbarType -Type DWord -Value 1
Set-ItemProperty "$profile/Settings" -Name HideFullscreenControlsPolicy -Type DWord -Value 1
Set-ItemProperty "$profile/Settings" -Name HideFullscreenControlsDelay -Type DWord -Value 0
Simulate-LegacySetupMarker
Install-Player ''
Assert-WindowHover
if((Get-ItemProperty "$profile/Toolbars/PlayerToolBar").ToolbarName -ne 'Fluent Soft' -or (Get-ItemProperty "$profile/Toolbars/PlayerToolBar").ToolbarType -ne 1){throw 'Hover repair changed the selected artwork'}
# The first migration marker must not exclude profiles using another icon theme.
Set-ItemProperty "$profile/Settings" -Name HideFullscreenControlsPolicy -Type DWord -Value 1
Set-ItemProperty "$profile/Settings" -Name HideFullscreenControlsDelay -Type DWord -Value 0
Simulate-LegacySetupMarker -LegacyVersion '2.8.4.10008' -Marker '1'
Install-Player ''
Assert-WindowHover
# The migration is one-time; an intentional later change must survive upgrades.
Set-ItemProperty "$profile/Settings" -Name HideFullscreenControlsPolicy -Type DWord -Value 1
Set-ItemProperty "$profile/Settings" -Name HideFullscreenControlsDelay -Type DWord -Value 0
Install-Player ''
if((Get-ItemProperty "$profile/Settings").HideFullscreenControlsPolicy -ne 1){throw 'Upgrade overwrote a later explicit hover preference'}
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class CodecProbe {
 [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
 public static extern IntPtr LoadLibraryEx(string name, IntPtr file, uint flags);
 [DllImport("kernel32.dll")] public static extern bool FreeLibrary(IntPtr module);
}
'@
foreach($name in 'LAVFilters64/LAVSplitter.ax','LAVFilters64/LAVAudio.ax','LAVFilters64/LAVVideo.ax','MediaInfo.dll') {
  $module=[CodecProbe]::LoadLibraryEx((Join-Path $folder $name),[IntPtr]::Zero,0x1100)
  if($module -eq [IntPtr]::Zero){throw "Codec could not load without external installation: $name, error $([Runtime.InteropServices.Marshal]::GetLastWin32Error())"}
  [void][CodecProbe]::FreeLibrary($module)
}
Set-ItemProperty "$profile/Settings" -Name ControlState -Type DWord -Value 15
@('[Settings]','ControlState=11','SetupPreservationSentinel=keep','HideCaptionMenu=3','HideWindowedControls=1','HideFullscreenControlsPolicy=1','HideFullscreenControlsDelay=0','[Toolbars\PlayerToolBar]','ToolbarName=Fluent Soft','ToolbarType=1') | Set-Content -LiteralPath "$folder/mpc-hc64.ini" -Encoding unicode
Simulate-LegacySetupMarker
$cache=Join-Path $env:LOCALAPPDATA 'MPC-HC/EpgCache'
New-Item -ItemType Directory -Path $cache -Force | Out-Null
Set-Content -LiteralPath "$cache/setup-test.txt" -Value 'keep'
Install-Player ''
if((Get-ItemProperty "$profile/Settings").ControlState -ne 15){throw 'Upgrade reset existing registry settings'}
$ini=Get-Content -LiteralPath "$folder/mpc-hc64.ini" -Raw
if($ini -notmatch 'ControlState=11' -or $ini -notmatch 'SetupPreservationSentinel=keep'){throw 'Upgrade reset existing INI'}
if($ini -notmatch 'HideFullscreenControlsPolicy=2' -or $ini -notmatch 'HideFullscreenControlsDelay=4000'){throw 'Legacy INI hover preset was not repaired'}
if($ini -notmatch 'ToolbarName=Fluent Soft' -or $ini -notmatch 'ToolbarType=1'){throw 'INI hover migration changed the selected artwork'}
if((Get-Content -LiteralPath "$cache/setup-test.txt").Trim() -ne 'keep'){throw 'Upgrade changed EPG cache'}
$uninstall=Get-ItemProperty 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\{2ACBF1FA-F5C3-4B19-A774-B22A31F231B9}_is1'
if($uninstall.DisplayName -notlike '*Fluent*' -or $uninstall.DisplayVersion -ne $Version){throw 'Native uninstall registration missing'}
$process=Start-Process -FilePath "$folder/unins000.exe" -ArgumentList '/VERYSILENT /SUPPRESSMSGBOXES /NORESTART' -WindowStyle Hidden -Wait -PassThru
if($process.ExitCode -ne 0 -or (Test-Path -LiteralPath "$folder/mpc-hc64.exe")){throw 'Native uninstall failed'}
if(-not(Test-Path -LiteralPath "$folder/mpc-hc64.ini") -or -not(Test-Path -LiteralPath "$cache/setup-test.txt")){throw 'Uninstall removed settings without consent'}
'Fresh install, bundled codec loading, upgrade preservation and native uninstall passed; player was not launched.'
