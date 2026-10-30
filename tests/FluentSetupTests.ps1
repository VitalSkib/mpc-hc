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
@('[Settings]','ControlState=11','SetupPreservationSentinel=keep') | Set-Content -LiteralPath "$folder/mpc-hc64.ini" -Encoding unicode
$cache=Join-Path $env:LOCALAPPDATA 'MPC-HC/EpgCache'
New-Item -ItemType Directory -Path $cache -Force | Out-Null
Set-Content -LiteralPath "$cache/setup-test.txt" -Value 'keep'
Install-Player ''
if((Get-ItemProperty "$profile/Settings").ControlState -ne 15){throw 'Upgrade reset existing registry settings'}
$ini=Get-Content -LiteralPath "$folder/mpc-hc64.ini" -Raw
if($ini -notmatch 'ControlState=11' -or $ini -notmatch 'SetupPreservationSentinel=keep'){throw 'Upgrade reset existing INI'}
if((Get-Content -LiteralPath "$cache/setup-test.txt").Trim() -ne 'keep'){throw 'Upgrade changed EPG cache'}
$uninstall=Get-ItemProperty 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\{2ACBF1FA-F5C3-4B19-A774-B22A31F231B9}_is1'
if($uninstall.DisplayName -notlike '*Fluent*' -or $uninstall.DisplayVersion -ne $Version){throw 'Native uninstall registration missing'}
$process=Start-Process -FilePath "$folder/unins000.exe" -ArgumentList '/VERYSILENT /SUPPRESSMSGBOXES /NORESTART' -WindowStyle Hidden -Wait -PassThru
if($process.ExitCode -ne 0 -or (Test-Path -LiteralPath "$folder/mpc-hc64.exe")){throw 'Native uninstall failed'}
if(-not(Test-Path -LiteralPath "$folder/mpc-hc64.ini") -or -not(Test-Path -LiteralPath "$cache/setup-test.txt")){throw 'Uninstall removed settings without consent'}
'Fresh install, bundled codec loading, upgrade preservation and native uninstall passed; player was not launched.'
