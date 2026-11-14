# Build the native Windows launcher and Client from the repository checkout.
# The Launcher owns Client imports and installation assignment; do not write into
# its managed package registry or instance directories from this build script.
[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Debug'
)

$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$preset = 'windows-msvc-x64'
$buildPreset = "windows-msvc-x64-$($Configuration.ToLowerInvariant())"
$buildRoot = Join-Path $root 'build/cmake/windows-msvc-x64/bin'
$launcherPath = Join-Path (Join-Path $buildRoot $Configuration) 'MaaGE Launcher.exe'

function Invoke-CMake {
    param([string[]]$Arguments)
    & cmake @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "CMake failed (exit $LASTEXITCODE): cmake $($Arguments -join ' ')"
    }
}

Push-Location $root
try {
    $cmake = Get-Command cmake -ErrorAction SilentlyContinue
    if (-not $cmake) {
        throw 'CMake 3.21+ was not found on PATH. Install CMake or Visual Studio CMake tools and restart VS Code.'
    }

    $cache = Join-Path $root 'cmake/windows-msvc-x64/CMakeCache.txt'
    if (-not (Test-Path -LiteralPath $cache -PathType Leaf)) {
        Write-Host "Configuring MaaGE ($preset)..."
        Invoke-CMake -Arguments @('--preset', $preset)
    }

    Write-Host "Building MaaGE Launcher and Client ($Configuration)..."
    Invoke-CMake -Arguments @('--build', '--preset', $buildPreset, '--target', 'maage_launcher', 'maage_client_manifest')

    if (-not (Test-Path -LiteralPath $launcherPath -PathType Leaf)) {
        throw "Launcher was not found at the expected location: $launcherPath"
    }

    $clientPackages = @(Get-ChildItem -LiteralPath (Join-Path $buildRoot $Configuration) -Directory -ErrorAction Stop |
        Where-Object { $_.Name -like 'MaaGE-Client-*-Windows-*' } |
        Where-Object {
            (Test-Path -LiteralPath (Join-Path $_.FullName 'maage-client.exe') -PathType Leaf) -and
            (Test-Path -LiteralPath (Join-Path $_.FullName 'maage-client.json') -PathType Leaf)
        })
    if ($clientPackages.Count -eq 0) {
        throw "No complete Client package found under $buildRoot/$Configuration."
    }
    # Old versioned outputs can coexist in a development checkout. The
    # just-built manifest is normally the most recently written one.
    $selectedPackage = $clientPackages |
        Sort-Object { (Get-Item -LiteralPath (Join-Path $_.FullName 'maage-client.json')).LastWriteTimeUtc } -Descending |
        Select-Object -First 1
    $clientExe = Join-Path $selectedPackage.FullName 'maage-client.exe'
    Write-Host "Client package ready: $clientExe"
    Write-Host 'Launching MaaGE Launcher...'
    Start-Process -FilePath $launcherPath -WorkingDirectory (Split-Path $launcherPath -Parent)

    Write-Host ''
    Write-Host 'To use this new build in an instance:'
    Write-Host '  Launcher > Installations > Import Local Client (or Replace Client)'
    Write-Host "  Select: $clientExe"
    Write-Host '  Assign the imported build to your installation, then select Play.'
    Write-Host 'The Launcher performs account, package and resource checks before Play.'
}
finally {
    Pop-Location
}
