param([string]$TargetDirectory = (Join-Path ([Environment]::GetFolderPath('Desktop')) 'MPC-HC-x64-toolbar-no-fill'))
$ErrorActionPreference = 'Stop'
$target = [IO.Path]::GetFullPath($TargetDirectory)
$exe = Join-Path $target 'mpc-hc64.exe'
$ini = Join-Path $target 'mpc-hc64.ini'
$manifest = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'manifest.json') -Raw | ConvertFrom-Json
$payload = Join-Path $PSScriptRoot 'payload\mpc-hc64.exe'
if ($manifest.product -ne 'MPC-HC-Fluent' -or $manifest.channel -notin @('stable','beta')) { throw 'Invalid update manifest' }
if (!(Test-Path -LiteralPath $exe) -or !(Test-Path -LiteralPath $ini)) { throw 'Select an existing portable player folder with EXE and INI' }
if ((Get-FileHash -LiteralPath $payload).Hash -ne $manifest.exe_sha256) { throw 'Update checksum mismatch' }
if (@(Get-CimInstance Win32_Process -Filter "Name = 'mpc-hc64.exe'" | Where-Object {$_.ExecutablePath -eq $exe}).Count) { throw 'Close the player before updating' }
$iniHash = (Get-FileHash -LiteralPath $ini).Hash
$route = Join-Path $target 'BrowserRouting\test-routing.json'
$config = $null
if (Test-Path -LiteralPath $route) { $config = Get-Content -LiteralPath $route -Raw | ConvertFrom-Json }
$backupRoot = Join-Path $target '.fluent-backups'
if ((Test-Path -LiteralPath $backupRoot) -and ((Get-Item -LiteralPath $backupRoot).Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw 'Backup folder must not be a link' }
$lock = $null; $stage = Join-Path $target ('.fluent-update-'+[guid]::NewGuid().ToString('N')+'.exe')
$routeStage = $route+'.fluent-new'; $replaced = $false; $routeReplaced = $false
try {
    $lock = [IO.File]::Open((Join-Path $target '.fluent-update.lock'),[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
    $backup = Join-Path $backupRoot ((Get-Date).ToUniversalTime().ToString('yyyyMMdd-HHmmss')+'-'+[guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $backup -Force | Out-Null
    if ($config) { Copy-Item -LiteralPath $route -Destination (Join-Path $backup 'test-routing.json') }
    Copy-Item -LiteralPath $payload -Destination $stage
    [IO.File]::Replace($stage,$exe,(Join-Path $backup 'mpc-hc64.exe'))
    $replaced = $true
    if ($config) {
        $config.PlayerSHA256 = $manifest.exe_sha256
        [IO.File]::WriteAllText($routeStage,($config|ConvertTo-Json -Depth 32),[Text.UTF8Encoding]::new($false))
        [IO.File]::Replace($routeStage,$route,[NullString]::Value)
        $routeReplaced = $true
    }
    if ((Get-FileHash -LiteralPath $exe).Hash -ne $manifest.exe_sha256) { throw 'Installed EXE checksum mismatch' }
    if ((Get-FileHash -LiteralPath $ini).Hash -ne $iniHash) { throw 'Settings changed during update' }
    @{product='MPC-HC-Fluent';version=$manifest.version;previous_sha256=(Get-FileHash (Join-Path $backup 'mpc-hc64.exe')).Hash} |
        ConvertTo-Json | Set-Content -LiteralPath (Join-Path $backup 'MPC-HC-Fluent-backup.json') -Encoding utf8
    # Remove only our marked backup directories, never arbitrary neighbouring files.
    $owned = @(Get-ChildItem -LiteralPath $backupRoot -Directory | Where-Object {
        !($_.Attributes -band [IO.FileAttributes]::ReparsePoint) -and
        (Test-Path -LiteralPath (Join-Path $_.FullName 'MPC-HC-Fluent-backup.json'))
    } | Sort-Object Name -Descending)
    foreach ($old in ($owned | Select-Object -Skip 3)) {
        $resolved = [IO.Path]::GetFullPath($old.FullName)
        if (!$resolved.StartsWith($backupRoot+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)) { continue }
        if (@(Get-ChildItem -LiteralPath $resolved -Recurse -Force | Where-Object {$_.Attributes -band [IO.FileAttributes]::ReparsePoint}).Count) { continue }
        Remove-Item -LiteralPath $resolved -Recurse -Force -ErrorAction SilentlyContinue
    }
    Write-Output ('Updated MPC-HC Fluent to '+$manifest.version+'. Settings and EPG preserved. Player not started.')
} catch {
    $failure = $_
    if ($replaced -and (Test-Path -LiteralPath (Join-Path $backup 'mpc-hc64.exe'))) {
        Copy-Item -LiteralPath (Join-Path $backup 'mpc-hc64.exe') -Destination $stage -Force
        [IO.File]::Replace($stage,$exe,[NullString]::Value)
    }
    if ($routeReplaced) { Copy-Item -LiteralPath (Join-Path $backup 'test-routing.json') -Destination $route -Force }
    if ($backup -and (Test-Path -LiteralPath $backup)) {
        foreach ($name in @('mpc-hc64.exe','test-routing.json','MPC-HC-Fluent-backup.json')) {
            $ownedFile=Join-Path $backup $name
            if(Test-Path -LiteralPath $ownedFile){Remove-Item -LiteralPath $ownedFile -Force}
        }
        [IO.Directory]::Delete($backup,$false)
    }
    throw $failure
} finally {
    foreach ($file in @($stage,$routeStage)) { if (Test-Path -LiteralPath $file) { Remove-Item -LiteralPath $file -Force } }
    if ($lock) { $lock.Dispose(); Remove-Item -LiteralPath (Join-Path $target '.fluent-update.lock') -Force }
}

