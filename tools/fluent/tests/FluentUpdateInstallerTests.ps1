$ErrorActionPreference='Stop'
$root=Join-Path ([IO.Path]::GetTempPath()) ('FluentInstallerTest-'+[guid]::NewGuid().ToString('N'))
$package=Join-Path $root 'package';$target=Join-Path $root 'portable'
New-Item -ItemType Directory -Path "$package/payload","$target/BrowserRouting","$target/EpgCache" -Force|Out-Null
try {
    Copy-Item tools/fluent/Update-Fluent.ps1 "$package/Update-Fluent.ps1"
    [IO.File]::WriteAllText("$target/mpc-hc64.exe",'old exe fixture')
    [IO.File]::WriteAllText("$package/payload/mpc-hc64.exe",'new exe fixture')
    [IO.File]::WriteAllText("$target/mpc-hc64.ini",'settings fixture')
    [IO.File]::WriteAllText("$target/EpgCache/stream-identities.json",'identity fixture')
    @{PlayerSHA256='old';Preserve='routing fixture'}|ConvertTo-Json|Set-Content "$target/BrowserRouting/test-routing.json"
    $manifest=@{product='MPC-HC-Fluent';version='2.8.4.10001';channel='stable';exe_sha256=(Get-FileHash "$package/payload/mpc-hc64.exe").Hash}
    $manifest|ConvertTo-Json|Set-Content "$package/manifest.json"
    & "$package/Update-Fluent.ps1" -TargetDirectory $target
    if((Get-Content "$target/mpc-hc64.exe" -Raw) -ne 'new exe fixture'){throw 'EXE not updated'}
    if((Get-Content "$target/mpc-hc64.ini" -Raw) -ne 'settings fixture'){throw 'INI changed'}
    if((Get-Content "$target/EpgCache/stream-identities.json" -Raw) -ne 'identity fixture'){throw 'Cache changed'}
    $route=Get-Content "$target/BrowserRouting/test-routing.json" -Raw|ConvertFrom-Json
    if($route.Preserve -ne 'routing fixture' -or $route.PlayerSHA256 -ne $manifest.exe_sha256){throw 'Route integrity not updated'}
    $manifest.exe_sha256='bad';$manifest|ConvertTo-Json|Set-Content "$package/manifest.json"
    $blocked=$false;try { & "$package/Update-Fluent.ps1" -TargetDirectory $target }catch{$blocked=$true}
    if(!$blocked -or (Get-Content "$target/mpc-hc64.exe" -Raw) -ne 'new exe fixture'){throw 'Checksum failure was not safe'}
    $manifest.exe_sha256=(Get-FileHash "$package/payload/mpc-hc64.exe").Hash;$manifest|ConvertTo-Json|Set-Content "$package/manifest.json"
    [IO.File]::WriteAllText("$package/payload/mpc-hc64.exe",'rollback payload fixture')
    $manifest.exe_sha256=(Get-FileHash "$package/payload/mpc-hc64.exe").Hash;$manifest|ConvertTo-Json|Set-Content "$package/manifest.json"
    (Get-Item "$target/BrowserRouting/test-routing.json").IsReadOnly=$true
    $blocked=$false
    try { & "$package/Update-Fluent.ps1" -TargetDirectory $target }catch{$blocked=$true}
    finally { (Get-Item "$target/BrowserRouting/test-routing.json").IsReadOnly=$false }
    if(!$blocked -or (Get-Content "$target/mpc-hc64.exe" -Raw) -ne 'new exe fixture'){throw 'Route failure did not roll back EXE'}
    [IO.File]::WriteAllText("$package/payload/mpc-hc64.exe",'new exe fixture')
    $manifest.exe_sha256=(Get-FileHash "$package/payload/mpc-hc64.exe").Hash;$manifest|ConvertTo-Json|Set-Content "$package/manifest.json"
    for($i=0;$i -lt 4;$i++){ & "$package/Update-Fluent.ps1" -TargetDirectory $target }
    if(@(Get-ChildItem "$target/.fluent-backups" -Directory).Count -ne 3){throw 'Backup retention failed'}
    Write-Output 'Installer update, preservation, routing, checksum rejection and backup retention passed'
} finally {
    $absolute=[IO.Path]::GetFullPath($root);$temp=[IO.Path]::GetFullPath([IO.Path]::GetTempPath())
    if(!$absolute.StartsWith($temp,[StringComparison]::OrdinalIgnoreCase) -or !(Split-Path $absolute -Leaf).StartsWith('FluentInstallerTest-')){throw 'Unsafe test cleanup path'}
    Remove-Item -LiteralPath $absolute -Recurse -Force
}
