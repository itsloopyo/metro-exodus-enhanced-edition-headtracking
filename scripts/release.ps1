#!/usr/bin/env pwsh
#Requires -Version 5.1
<#
.SYNOPSIS
    Release entry point. Usage: pixi run release <major|minor|patch|nightly|X.Y.Z>
.DESCRIPTION
    Fully unattended. The command-line invocation is the authorization - there
    are no prompts. Deterministic preconditions (on main, clean tree, tag
    absent, valid semver) fail-fast with a non-zero exit instead of asking.
    Never destructive: no force push, no amend, no tag overwrite.

    pixi.toml is the canonical version source, because that is what
    release.yml validates the pushed tag against (version-source: pixi) and
    what package-release.ps1 names the ZIPs after. CMakeLists.txt,
    launcher-manifest.json and install.cmd's MOD_VERSION are kept in step.
#>
param(
    [Parameter(Position = 0)]
    [string]$Version = ''
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$usage = 'Usage: pixi run release <major|minor|patch|nightly|X.Y.Z>'

$ProjectRoot = Resolve-Path (Join-Path $PSScriptRoot '..')
$PixiPath = Join-Path $ProjectRoot 'pixi.toml'
$CMakePath = Join-Path $ProjectRoot 'CMakeLists.txt'
$ManifestPath = Join-Path $ProjectRoot 'launcher-manifest.json'
$InstallCmdPath = Join-Path $ProjectRoot 'scripts\install.cmd'
$ChangelogPath = Join-Path $ProjectRoot 'CHANGELOG.md'

Import-Module (Join-Path $ProjectRoot 'cameraunlock-core\powershell\ReleaseWorkflow.psm1') -Force

function Get-PixiVersion {
    $content = Get-Content -LiteralPath $PixiPath -Raw
    if ($content -notmatch '(?m)^\s*version\s*=\s*"(\d+\.\d+\.\d+)"') {
        throw "Could not read version from $PixiPath"
    }
    return $Matches[1]
}

$currentVersion = Get-PixiVersion

if ([string]::IsNullOrWhiteSpace($Version)) {
    Write-Host "Current version: $currentVersion"
    Write-Host $usage
    exit 0
}

if ($Version -eq 'nightly') {
    & (Join-Path $PSScriptRoot 'release-nightly.ps1')
    exit $LASTEXITCODE
}

try {
    $newVersion = Resolve-ReleaseVersion -Argument $Version -CurrentVersion $currentVersion
} catch {
    Write-Host $usage
    Write-Host $_.Exception.Message
    exit 2
}

$tag = "v$newVersion"

$branch = (git -C $ProjectRoot rev-parse --abbrev-ref HEAD).Trim()
if ($branch -ne 'main') {
    Write-Host "Refusing to release: on branch '$branch', not 'main'."
    exit 1
}
if (-not (Test-CleanGitStatus)) {
    Write-Host 'Refusing to release: working tree is dirty. Commit or stash first.'
    git -C $ProjectRoot status --short
    exit 1
}
if (Test-GitTagExists -Tag $tag) {
    Write-Host "Refusing to release: tag '$tag' already exists."
    exit 1
}

Write-Host "Releasing $currentVersion -> $newVersion (tag $tag)" -ForegroundColor Cyan

# THIRD-PARTY-NOTICES.md names the cameraunlock-core commit compiled into the
# release ZIPs, and bumping the submodule does not touch it. A wrong hash reads
# exactly like a right one, so re-sync it before the tag rather than shipping
# attribution nobody wrote.
& git -C $ProjectRoot diff --quiet -- THIRD-PARTY-NOTICES.md
if ($LASTEXITCODE -ne 0) { throw 'THIRD-PARTY-NOTICES.md has uncommitted edits. Commit or discard them, then re-run.' }
& (Join-Path $ProjectRoot 'cameraunlock-core\scripts\sync-core-notices.ps1') -Repo $ProjectRoot
if ($LASTEXITCODE -ne 0) { throw "sync-core-notices.ps1 exited $LASTEXITCODE - fix THIRD-PARTY-NOTICES.md before releasing." }
& git -C $ProjectRoot diff --quiet -- THIRD-PARTY-NOTICES.md
if ($LASTEXITCODE -ne 0) {
    & git -C $ProjectRoot commit -q -m 'chore: record the cameraunlock-core commit this build compiles' -- THIRD-PARTY-NOTICES.md
    if ($LASTEXITCODE -ne 0) { throw 'Could not commit the re-synced THIRD-PARTY-NOTICES.md.' }
    Write-Host 'THIRD-PARTY-NOTICES.md re-synced to the pinned cameraunlock-core commit.' -ForegroundColor Yellow
}

# ReleaseWorkflow.psm1's git helpers (changelog, version commit, tag and push)
# all shell out to bare `git`, so they act on the current directory rather than
# on $ProjectRoot. Everything below therefore runs from the repo root: invoked
# through `pixi run release` that is already true, but a release must not depend
# on where it was launched from.
Push-Location $ProjectRoot
try {

# The changelog is generated first because it is the step that can refuse
# (nothing user-facing since the last tag), so a release with nothing to say
# stops before anything is built.
#
# Everything below writes into five files and then builds. A failure after the
# writes used to leave all five modified, no commit and no tag, so the next
# `pixi run release` refused on a dirty tree and the operator had to work out by
# hand which edits were the aborted run's. The originals are held here - the
# changelog included, because New-ChangelogFromCommits writes it too - and put
# back on any failure.
$versionFiles = @($PixiPath, $CMakePath, $ManifestPath, $InstallCmdPath, $ChangelogPath)
$originals = @{}
foreach ($f in $versionFiles) { $originals[$f] = [System.IO.File]::ReadAllBytes($f) }

try {

New-ChangelogFromCommits -ChangelogPath $ChangelogPath -Version $newVersion `
    -ArtifactPaths @('src/', 'cameraunlock-core/', 'scripts/install.cmd', 'scripts/uninstall.cmd') | Out-Null

(Get-Content -LiteralPath $PixiPath) `
    -replace '(?m)^version\s*=\s*"\d+\.\d+\.\d+"', "version = `"$newVersion`"" |
    Set-Content -LiteralPath $PixiPath

(Get-Content -LiteralPath $CMakePath) `
    -replace '(project\(MetroExodusHeadTracking VERSION )\d+\.\d+\.\d+', "`${1}$newVersion" |
    Set-Content -LiteralPath $CMakePath

# A targeted substitution, not a ConvertFrom-Json / ConvertTo-Json round trip.
# PowerShell 5.1's serializer reflows the whole file - two spaces after every
# colon, arrays split across lines - so a one-line version bump arrived as a
# whole-file diff and the committed manifest stopped being the shape a reviewer
# wrote. package-release.ps1 restamps the same field the same way.
$manifestText = [System.IO.File]::ReadAllText($ManifestPath)
$manifestText = $manifestText -replace '("version"\s*:\s*")\d+\.\d+\.\d+(")', "`${1}$newVersion`${2}"
# UTF-8 without a BOM: PowerShell 5.1's -Encoding UTF8 prefixes EF BB BF, which
# the launcher's serde_json parser rejects.
[System.IO.File]::WriteAllText($ManifestPath, $manifestText,
                               (New-Object System.Text.UTF8Encoding $false))

(Get-Content -LiteralPath $InstallCmdPath) `
    -replace 'set "MOD_VERSION=.+"', "set `"MOD_VERSION=$newVersion`"" |
    Set-Content -LiteralPath $InstallCmdPath

& pixi run build
if ($LASTEXITCODE -ne 0) { throw "Build failed (exit $LASTEXITCODE)." }
& pixi run package
if ($LASTEXITCODE -ne 0) { throw "Package failed (exit $LASTEXITCODE)." }

} catch {
    foreach ($f in $versionFiles) { [System.IO.File]::WriteAllBytes($f, $originals[$f]) }
    throw
}

$null = Invoke-VersionCommit -Version $newVersion -Files @(
    $PixiPath, $CMakePath, $ManifestPath, $InstallCmdPath, $ChangelogPath
)

New-ReleaseTag -Version $newVersion -Message "Release $tag" -Branch 'main'

} finally {
    Pop-Location
}

Write-Host "Released $tag." -ForegroundColor Green
exit 0
