#!/usr/bin/env pwsh
#Requires -Version 5.1
# Package Metro Exodus Enhanced Edition Head Tracking into two release ZIPs:
#   - MetroExodusHeadTracking-v{version}-installer.zip (GitHub Release)
#   - MetroExodusHeadTracking-v{version}-nexus.zip (Nexus, extract-to-game-folder)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
$ProgressPreference = 'SilentlyContinue'

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$projectDir = Split-Path -Parent $scriptDir

Import-Module (Join-Path $projectDir "cameraunlock-core\powershell\ReleaseWorkflow.psm1") -Force

$pixiToml = Get-Content (Join-Path $projectDir "pixi.toml") -Raw
# Semver only, the same shape release.ps1 writes and release.yml validates the
# pushed tag against. It is also what keeps the value below - which names both
# ZIPs and is stamped into the shipped manifest - from being any arbitrary
# string that happened to sit on a version line.
if ($pixiToml -notmatch '(?m)^\s*version\s*=\s*"(\d+\.\d+\.\d+)"') {
    throw "Could not find a X.Y.Z version in pixi.toml"
}
$version = $matches[1]

Write-Host "=== Metro Exodus Enhanced Edition Head Tracking - Package Release ===" -ForegroundColor Magenta
Write-Host "Version: $version" -ForegroundColor Cyan

$releaseDir = Join-Path $projectDir "release"
if (-not (Test-Path $releaseDir)) { New-Item -ItemType Directory -Path $releaseDir -Force | Out-Null }

$asiPath = Join-Path $projectDir "bin/Release/MetroExodusHeadTracking.asi"
if (-not (Test-Path $asiPath)) { throw "MetroExodusHeadTracking.asi not found at: $asiPath" }

# The mod loads its INI by module-relative name (src/dllmain.cpp), so the
# shipped artifact must be named MetroExodusHeadTracking.ini or nothing reads it.
$iniPath = Join-Path $projectDir "MetroExodusHeadTracking.ini"
if (-not (Test-Path $iniPath)) { throw "MetroExodusHeadTracking.ini not found at: $iniPath" }

$vendorAsiDir = Join-Path $projectDir "vendor/ultimate-asi-loader"
$vendorAsiDll = Join-Path $vendorAsiDir "dinput8.dll"
if (-not (Test-Path $vendorAsiDll)) { throw "Bundled ASI loader missing: $vendorAsiDll (run 'pixi run update-deps')" }

$scriptsDir = Join-Path $projectDir "scripts"
foreach ($script in @("install.cmd", "uninstall.cmd")) {
    if (-not (Test-Path (Join-Path $scriptsDir $script))) {
        throw "Required script not found: $script"
    }
}

$modManifestPath = Join-Path $projectDir "launcher-manifest.json"
if (-not (Test-Path $modManifestPath)) {
    throw "launcher-manifest.json not found at: $modManifestPath"
}

# --- GitHub Release ZIP ---
Write-Host ""
Write-Host "--- GitHub Release ZIP ---" -ForegroundColor Yellow

$ghStagingDir = Join-Path $releaseDir "staging-github"
if (Test-Path $ghStagingDir) { Remove-Item -Recurse -Force $ghStagingDir }
New-Item -ItemType Directory -Path $ghStagingDir -Force | Out-Null

foreach ($script in @("install.cmd", "uninstall.cmd")) {
    Copy-Item (Join-Path $scriptsDir $script) -Destination $ghStagingDir -Force
    Write-Host "  $script" -ForegroundColor Green
}

$pluginsDir = Join-Path $ghStagingDir "plugins"
New-Item -ItemType Directory -Path $pluginsDir -Force | Out-Null
Copy-Item $asiPath -Destination $pluginsDir -Force
Write-Host "  plugins/MetroExodusHeadTracking.asi" -ForegroundColor Green
Copy-Item $iniPath -Destination $pluginsDir -Force
Write-Host "  plugins/MetroExodusHeadTracking.ini" -ForegroundColor Green

$ghVendorDir = Join-Path $ghStagingDir "vendor/ultimate-asi-loader"
New-Item -ItemType Directory -Path $ghVendorDir -Force | Out-Null
foreach ($vendorFile in @("dinput8.dll", "LICENSE", "README.md")) {
    $src = Join-Path $vendorAsiDir $vendorFile
    if (Test-Path $src) {
        Copy-Item $src -Destination $ghVendorDir -Force
        Write-Host "  vendor/ultimate-asi-loader/$vendorFile" -ForegroundColor Green
    }
}

Copy-LicenceNotices -StagingDir $ghStagingDir -ProjectRoot $projectDir -Additional @("README.md", "CHANGELOG.md")

# No -RefreshCore: packaging consumes the cameraunlock-core commit the build
# just used. Moving the pointer is `pixi run sync`, a deliberate act with a
# commit attached.
Copy-SharedBundle -StagingDir $ghStagingDir

# Canonical launcher manifest. The launcher ingests this to detect, version,
# and (in a future manifest delivery mode) deploy the package declaratively.
# Stamp the version from the build so the shipped manifest can never disagree
# with the built .asi.
$stagedManifest = Join-Path $ghStagingDir "launcher-manifest.json"
$manifestText = Get-Content $modManifestPath -Raw
$manifestText = $manifestText -replace '("version":\s*")\d+\.\d+\.\d+(")', "`${1}$version`$2"

# The launcher writes the default INI from the manifest's own base64 rather than
# from the copy in plugins/, so the two have to agree. Restamped from the repo's
# MetroExodusHeadTracking.ini for the same reason the version is: an edit to the
# INI would otherwise ship a launcher install carrying the previous release's
# defaults, and nothing in the ZIP would look wrong.
$iniB64 = [Convert]::ToBase64String([System.IO.File]::ReadAllBytes($iniPath))
$manifestText = $manifestText -replace '("content_b64":\s*")[A-Za-z0-9+/=]*(")', "`${1}$iniB64`$2"

# The stamp is a substitution, so it does nothing at all when the manifest's own
# version is absent, empty or not X.Y.Z - and a manifest that quietly kept the
# wrong version is the one thing this step exists to prevent, because the
# launcher reads mod_info.version to decide whether an install is up to date.
# Read it back rather than trusting the replace.
$stampedManifestObject = $manifestText | ConvertFrom-Json
$stampedVersion = $stampedManifestObject.mod_info.version
if ($stampedVersion -ne $version) {
    throw "launcher-manifest.json still declares mod_info.version '$stampedVersion' after stamping $version. Set it to a X.Y.Z version and re-run."
}
$seededIni = @($stampedManifestObject.loader.seed | Where-Object { $_.target -eq "MetroExodusHeadTracking.ini" })
if ($seededIni.Count -ne 1 -or $seededIni[0].content_b64 -ne $iniB64) {
    throw "launcher-manifest.json's loader.seed does not carry the current MetroExodusHeadTracking.ini after stamping. It needs exactly one seed entry targeting MetroExodusHeadTracking.ini with a content_b64 value."
}

[System.IO.File]::WriteAllText($stagedManifest, $manifestText, (New-Object System.Text.UTF8Encoding $false))
Write-Host "  launcher-manifest.json (version $version)" -ForegroundColor Green

$ghZipName = "MetroExodusHeadTracking-v$version-installer.zip"
$ghZipPath = Join-Path $releaseDir $ghZipName
if (Test-Path $ghZipPath) { Remove-Item $ghZipPath -Force }

Write-Host ""
Write-Host "Creating GitHub ZIP..." -ForegroundColor Cyan
Push-Location $ghStagingDir
try { Compress-Archive -Path ".\*" -DestinationPath $ghZipPath -Force }
finally { Pop-Location }
Remove-Item -Recurse -Force $ghStagingDir

$ghZipSize = (Get-Item $ghZipPath).Length / 1KB
Write-Host ("  $ghZipPath ({0:N1} KB)" -f $ghZipSize) -ForegroundColor Green

# delivery_mode is "manifest", so the launcher deploys from launcher-manifest.json
# alone: a files[] row naming a path the ZIP does not carry installs a mod that
# cannot run, and a payload file no row names is left out. Both of those report a
# successful install. Core owns the check; running it here means a broken package
# fails the build instead of a download.
# Node is not in pixi.toml's (empty) dependency list and is not one of the three
# tools this workspace documents as coming from the machine, so say so plainly
# rather than letting $ErrorActionPreference throw CommandNotFoundException after
# a full Release build.
if (-not (Get-Command node -ErrorAction SilentlyContinue)) {
    throw "node is required to validate the release manifest, and it is not on PATH. Install Node.js (the CI runner image ships it) and re-run 'pixi run package'."
}
& node (Join-Path $projectDir "cameraunlock-core/scripts/validate-manifest.mjs") $ghZipPath
if ($LASTEXITCODE -ne 0) { throw "validate-manifest.mjs rejected $ghZipName (exit $LASTEXITCODE)." }

# --- Nexus Mods ZIP ---
Write-Host ""
Write-Host "--- Nexus Mods ZIP ---" -ForegroundColor Yellow

$nexusStagingDir = Join-Path $releaseDir "staging-nexus"
if (Test-Path $nexusStagingDir) { Remove-Item -Recurse -Force $nexusStagingDir }
New-Item -ItemType Directory -Path $nexusStagingDir -Force | Out-Null

Copy-Item $asiPath -Destination $nexusStagingDir -Force
Write-Host "  MetroExodusHeadTracking.asi" -ForegroundColor Green
Copy-Item $iniPath -Destination $nexusStagingDir -Force
Write-Host "  MetroExodusHeadTracking.ini" -ForegroundColor Green

# The loader ships here even though a Nexus ZIP normally carries payload only.
# A BepInEx mod can leave the loader out because the user installed BepInEx as
# its own Nexus download; there is no equivalent for an ASI mod, and an archive
# of MetroExodusHeadTracking.asi alone extracts into the game folder and does
# nothing at all, with no error to explain why. Do not remove it.
$nexusWinmm = Join-Path $nexusStagingDir "winmm.dll"
Copy-Item $vendorAsiDll -Destination $nexusWinmm -Force
Write-Host "  winmm.dll (Ultimate ASI Loader, MIT)" -ForegroundColor Green

$nexusZipName = "MetroExodusHeadTracking-v$version-nexus.zip"
$nexusZipPath = Join-Path $releaseDir $nexusZipName
if (Test-Path $nexusZipPath) { Remove-Item $nexusZipPath -Force }

# The Nexus ZIP is a binary distribution too: the licences of everything
# compiled into or bundled with the payload require their notices to travel
# with it, so LICENSE, THIRD-PARTY-NOTICES.md and the cameraunlock-core licence
# ship at its root. No README or CHANGELOG - this archive extracts straight
# into the game folder, and documentation is not payload.
Copy-LicenceNotices -StagingDir $nexusStagingDir -ProjectRoot $projectDir

Push-Location $nexusStagingDir
try { Compress-Archive -Path ".\*" -DestinationPath $nexusZipPath -Force }
finally { Pop-Location }
Remove-Item -Recurse -Force $nexusStagingDir

$nexusZipSize = (Get-Item $nexusZipPath).Length / 1KB
Write-Host ("  $nexusZipPath ({0:N1} KB)" -f $nexusZipSize) -ForegroundColor Green

Write-Host ""
Write-Host "=== Package Complete ===" -ForegroundColor Magenta
Write-Output $ghZipPath
Write-Output $nexusZipPath
