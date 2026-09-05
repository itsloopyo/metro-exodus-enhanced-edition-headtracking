[CmdletBinding()]
param(
    [switch]$AllowDirty
)

$ErrorActionPreference = 'Stop'

$ProjectRoot = Resolve-Path (Join-Path $PSScriptRoot '..')

Import-Module (Join-Path $ProjectRoot 'cameraunlock-core\powershell\NightlyRelease.psm1') -Force

# The same anchored semver pattern release.ps1 and package-release.ps1 use.
# `[^"]+` accepted anything, and a non-semver value then reached
# Publish-NightlyBuild, which derives the expected ZIP filename from it - so the
# failure arrived as "installer ZIP not found" rather than as the version being
# wrong. Select-Object -First 1 because Select-String returns one match object
# per matching line, and `.Matches[0]` only behaves as written while there is
# exactly one `version =` line in the file.
$pixiFile = Join-Path $ProjectRoot 'pixi.toml'
$versionMatch = Select-String -Path $pixiFile -Pattern '^\s*version\s*=\s*"(\d+\.\d+\.\d+)"' |
    Select-Object -First 1
if (-not $versionMatch) {
    throw "Could not find a X.Y.Z version line in $pixiFile"
}
$version = $versionMatch.Matches[0].Groups[1].Value

Publish-NightlyBuild `
    -ModId 'metro-exodus-enhanced-edition' `
    -ModName 'MetroExodusHeadTracking' `
    -Version $version `
    -ProjectRoot $ProjectRoot `
    -BuildCommand 'pixi run build' `
    -AllowDirty:$AllowDirty
