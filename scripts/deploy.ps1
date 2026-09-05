#!/usr/bin/env pwsh
#Requires -Version 5.1
<#
.SYNOPSIS
    Deploy the freshly built Release ASI + MetroExodusHeadTracking.ini into the
    local game install. Usage: pixi run install [<game path>]
.DESCRIPTION
    Convenience for iteration; bypasses install.cmd's loader-check flow, so the
    ASI loader proxy has to be in place already (run install.cmd once).

    Fully unattended. Game detection follows the same order install.cmd uses -
    an explicit positional path wins, otherwise Find-GamePath walks env var,
    registry, Steam and games.json. Nothing found is exit 1, never a prompt.
#>
param(
    [Parameter(Position = 0)]
    [string]$GamePath = ''
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$projectDir = Resolve-Path (Join-Path $PSScriptRoot '..')

if ($GamePath) {
    if (-not (Test-Path (Join-Path $GamePath 'MetroExodus.exe'))) {
        Write-Host "Not a Metro Exodus Enhanced Edition install (no MetroExodus.exe): $GamePath"
        exit 1
    }
} else {
    Import-Module (Join-Path $projectDir 'cameraunlock-core/powershell/GamePathDetection.psm1') -Force
    $GamePath = Find-GamePath -GameId 'metro-exodus-enhanced-edition'
    if (-not $GamePath) {
        Write-Host 'Could not detect the Metro Exodus Enhanced Edition install. Set METRO_EXODUS_ENHANCED_EDITION_PATH, or pass the game folder: pixi run install "C:\Path\To\Metro Exodus Enhanced Edition"'
        exit 1
    }
}

# install.cmd cannot be run from the repo - it looks for a plugins/ folder beside
# itself and belongs to the extracted release ZIP - so the proxy is a manual copy
# and it is routinely the thing that is missing. Without it the game starts, no
# HeadTracking.log appears, and nothing said why.
# Either name, because MetroExodus.exe imports both (checked against its import
# table) and a developer may already have a dinput8.dll loader from another mod.
# install.cmd ships winmm.dll; the README says leaving it as dinput8.dll works.
$loaderNames = @('winmm.dll', 'dinput8.dll')
if (-not ($loaderNames | Where-Object { Test-Path (Join-Path $GamePath $_) })) {
    throw "No ASI loader in $GamePath. Copy vendor\ultimate-asi-loader\dinput8.dll there, as winmm.dll or under its own name, then re-run. Without it the .asi is never loaded and no HeadTracking.log is written."
}

$asi = Join-Path $projectDir 'bin/Release/MetroExodusHeadTracking.asi'
$ini = Join-Path $projectDir 'MetroExodusHeadTracking.ini'
foreach ($f in @($asi, $ini)) {
    if (-not (Test-Path $f)) { throw "Build artifact missing: $f. Run 'pixi run build' first." }
}

Write-Host "Deploying to: $GamePath" -ForegroundColor Cyan
Copy-Item $asi -Destination $GamePath -Force
Write-Host '  MetroExodusHeadTracking.asi' -ForegroundColor Green

# Seeded, never overwritten: an update must not reset whatever the user tuned.
# install.cmd draws the same line with MOD_SEED_FILES.
$iniTarget = Join-Path $GamePath 'MetroExodusHeadTracking.ini'
if (Test-Path $iniTarget) {
    Write-Host '  MetroExodusHeadTracking.ini (kept, already present)' -ForegroundColor DarkGray
} else {
    Copy-Item $ini -Destination $iniTarget -Force
    Write-Host '  MetroExodusHeadTracking.ini' -ForegroundColor Green
}
