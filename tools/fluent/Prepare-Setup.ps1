[CmdletBinding()]
param()
$ErrorActionPreference='Stop'
$config=Get-Content -LiteralPath 'tools/fluent/project.json' -Raw | ConvertFrom-Json
$tag=[string]$config.upstream_tag
if($tag -notmatch '^v?\d+\.\d+\.\d+(\.\d+)?(-[A-Za-z0-9.-]+)?$'){throw 'Invalid upstream tag'}
$headers=@{'User-Agent'='MPC-HC-Fluent-setup'}
if($env:GH_TOKEN){$headers.Authorization="Bearer $env:GH_TOKEN"}
$release=Invoke-RestMethod "https://api.github.com/repos/clsid2/mpc-hc/releases/tags/$tag" -Headers $headers
$asset=@($release.assets | Where-Object {$_.name -match '^MPC-HC\..+\.x64\.zip$'})
if($asset.Count -ne 1 -or $asset[0].digest -notmatch '^sha256:[a-f0-9]{64}$'){throw 'Expected official x64 package with SHA256'}
$asset=$asset[0]
$temporary=Join-Path $env:RUNNER_TEMP ('fluent-setup-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $temporary | Out-Null
function Get-CheckedFile($Url,$Path,$Hash) {
  Invoke-WebRequest $Url -OutFile $Path
  if((Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant() -ne $Hash){throw "Dependency checksum mismatch: $Url"}
}
Get-CheckedFile $asset.browser_download_url "$temporary/upstream.zip" $asset.digest.Substring(7)
Expand-Archive -LiteralPath "$temporary/upstream.zip" -DestinationPath "$temporary/upstream"
$player=@(Get-ChildItem "$temporary/upstream" -Filter mpc-hc64.exe -File -Recurse)
if($player.Count -ne 1){throw 'Ambiguous upstream package layout'}
$source=$player[0].Directory.FullName
$destination='bin/mpc-hc_x64'
New-Item -ItemType Directory -Path $destination -Force | Out-Null
# Stock binaries are syntax-check inputs only. The full build overwrites them,
# and packaging rejects an EXE whose four-part version differs from our revision.
Copy-Item -LiteralPath "$source/mpc-hc64.exe","$source/mpciconlib.dll" -Destination $destination
Copy-Item -LiteralPath "$source/Lang" -Destination $destination -Recurse
Copy-Item -LiteralPath "$source/LAVFilters64" -Destination $destination -Recurse
Copy-Item -LiteralPath "$source/MediaInfo.dll" -Destination 'distrib/x64/MediaInfo.dll'
New-Item -ItemType Directory -Path 'distrib/mpcvr' -Force | Out-Null
Copy-Item -LiteralPath "$source/MPCVR/MpcVideoRenderer64.ax" -Destination 'distrib/mpcvr/'
# Build the original full solution, using matching released codecs instead of compiling FFmpeg again.
$solution=[IO.File]::ReadAllText((Join-Path (Get-Location) 'mpc-hc.sln'))
$codecBuild='(?m)^\s*\{ED257874-E12E-4143-AF0A-0676DA3BB18C\}\.Release\|x64\.Build\.0 = Release\|x64\r?$'
if([regex]::Matches($solution,$codecBuild).Count -ne 1){throw 'Review required: upstream codec project configuration changed'}
[IO.File]::WriteAllText((Join-Path (Get-Location) 'mpc-hc.sln'),[regex]::Replace($solution,$codecBuild,''))
foreach($name in 'LAVAudio.ax','LAVSplitter.ax','LAVVideo.ax','LAVFilters.Dependencies.manifest') {
  if(-not (Test-Path -LiteralPath "$destination/LAVFilters64/$name" -PathType Leaf)){throw "Missing codec: $name"}
}
$innoUrl='https://github.com/jrsoftware/issrc/releases/download/is-6_7_3/innosetup-6.7.3.exe'
$innoHash='9c73c3bae7ed48d44112a0f48e66742c00090bdb5bef71d9d3c056c66e97b732'
Get-CheckedFile $innoUrl "$temporary/inno.exe" $innoHash
$inno=Join-Path $temporary 'compiler'
$process=Start-Process -FilePath "$temporary/inno.exe" -ArgumentList @('/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART','/SP-',"/DIR=`"$inno`"") -WindowStyle Hidden -PassThru -Wait
if($process.ExitCode -ne 0 -or -not (Test-Path -LiteralPath "$inno/ISCC.exe")){throw 'Inno compiler preparation failed'}
"iscc=$inno/ISCC.exe" >> $env:GITHUB_OUTPUT
$runtimeUrl='https://aka.ms/windowsappsdk/1.8/1.8.260921001/windowsappruntimeinstall-x64.exe'
$runtimeHash='7c092e85fc1e3e7cb2bd7d549aaab03caf924131f15d9b20b1855457a98205aa'
New-Item -ItemType Directory -Path 'distrib/FluentRuntime' -Force | Out-Null
Get-CheckedFile $runtimeUrl 'distrib/FluentRuntime/WindowsAppRuntimeInstall-x64.exe' $runtimeHash
$signature=Get-AuthenticodeSignature -LiteralPath 'distrib/FluentRuntime/WindowsAppRuntimeInstall-x64.exe'
if($signature.Status -ne 'Valid' -or $signature.SignerCertificate.Subject -notmatch '^CN=Microsoft Corporation,'){throw 'Invalid Microsoft runtime signature'}
Copy-Item -LiteralPath 'src/mpc-hc/wasdk-probe/foundation/runtimes/win-x64/native/Microsoft.WindowsAppRuntime.Bootstrap.dll' -Destination 'distrib/FluentRuntime/'
@{upstream_package=$asset.browser_download_url; upstream_sha256=$asset.digest.Substring(7);
  upstream_source="https://github.com/clsid2/mpc-hc/tree/$($config.upstream_commit)";
  windows_app_runtime=$runtimeUrl; windows_app_runtime_sha256=$runtimeHash;
  windows_app_runtime_source='https://github.com/microsoft/WindowsAppSDK/tree/v1.8.12';
  inno_setup=$innoUrl; inno_sha256=$innoHash} | ConvertTo-Json | Set-Content -LiteralPath 'tools/fluent/dependencies.json' -Encoding utf8
