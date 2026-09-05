#!/usr/bin/env pwsh
#Requires -Version 5.1
# Bump vendored Ultimate ASI Loader (dinput8.dll) to the latest upstream within
# the pinned range. Manual; commit the result. CI does not run this.

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$ProgressPreference    = 'SilentlyContinue'

$scriptDir  = Split-Path -Parent $MyInvocation.MyCommand.Path
$projectDir = Split-Path -Parent $scriptDir

$module = Join-Path $projectDir 'cameraunlock-core/powershell/ModLoaderSetup.psm1'
if (-not (Test-Path $module)) {
    throw "ModLoaderSetup.psm1 not found. Run 'pixi run sync'."
}
Import-Module $module -Force

$vendorAsiDir = Join-Path $projectDir 'vendor/ultimate-asi-loader'
$vendorAsiDll = Join-Path $vendorAsiDir 'dinput8.dll'
if (-not (Test-Path $vendorAsiDir)) {
    New-Item -ItemType Directory -Path $vendorAsiDir -Force | Out-Null
}

$tempZip = Join-Path $env:TEMP ("asi-update-" + [IO.Path]::GetRandomFileName() + ".zip")
try {
    Write-Host "Refreshing vendor/ultimate-asi-loader from upstream..." -ForegroundColor Cyan
    $meta = Invoke-FetchLatestLoader `
        -OutputPath $tempZip `
        -Owner 'ThirteenAG' -Repo 'Ultimate-ASI-Loader' `
        -VersionPrefix 'v9.' `
        -AssetPattern '^Ultimate-ASI-Loader_x64\.zip$'

    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $zip = [System.IO.Compression.ZipFile]::OpenRead($tempZip)
    try {
        $dllEntry = $zip.Entries | Where-Object { $_.Name -eq 'dinput8.dll' } | Select-Object -First 1
        if (-not $dllEntry) { throw "Upstream zip $($meta.AssetName) does not contain dinput8.dll." }
        # Extracted beside the real file and moved over it only once the stream
        # has closed cleanly. Creating the vendored DLL directly truncates it to
        # zero bytes first, so a failed copy left a 0-byte loader in the tree -
        # and package-release.ps1's only check on it is Test-Path, which a
        # 0-byte file passes, so the next release would ship an empty winmm.dll.
        $dllTemp = "$vendorAsiDll.tmp"
        $out = [System.IO.File]::Create($dllTemp)
        try { $in = $dllEntry.Open(); try { $in.CopyTo($out) } finally { $in.Dispose() } } finally { $out.Dispose() }
        Move-Item -LiteralPath $dllTemp -Destination $vendorAsiDll -Force

        $licenseEntry = $zip.Entries | Where-Object { $_.Name -match '^(license|LICENSE)(\..+)?$' -and $_.FullName -notmatch '/.+/' } | Select-Object -First 1
        if ($licenseEntry) {
            $out = [System.IO.File]::Create((Join-Path $vendorAsiDir 'LICENSE'))
            try { $in = $licenseEntry.Open(); try { $in.CopyTo($out) } finally { $in.Dispose() } } finally { $out.Dispose() }
        }
    } finally { $zip.Dispose() }

    if (-not (Test-Path (Join-Path $vendorAsiDir 'LICENSE'))) {
        $licenseUrl = "https://raw.githubusercontent.com/ThirteenAG/Ultimate-ASI-Loader/$($meta.Tag)/license"
        Invoke-WebRequest -Uri $licenseUrl -OutFile (Join-Path $vendorAsiDir 'LICENSE') -UseBasicParsing -TimeoutSec 30 -Headers @{ "User-Agent" = "CameraUnlock-HeadTracking" }
    }

    $dllSha = (Get-FileHash -Path $vendorAsiDll -Algorithm SHA256).Hash.ToLower()
    $readme = @(
        '# Ultimate ASI Loader (vendored)',
        '',
        'Bundled copy of Ultimate ASI Loader, the install-time source of truth.',
        'Refresh manually with `pixi run update-deps`, then commit.',
        '',
        '## Snapshot',
        '',
        '- Upstream: https://github.com/ThirteenAG/Ultimate-ASI-Loader',
        "- Tag: ``$($meta.Tag)``",
        "- Commit: ``$($meta.CommitSha)``",
        "- Asset: ``$($meta.AssetName)``",
        "- dinput8.dll SHA-256: ``$dllSha``",
        "- Fetched at: $($meta.FetchedAt)",
        '',
        '`dinput8.dll` is extracted from the upstream asset untouched. install.cmd copies it',
        'to the Metro Exodus Enhanced Edition exe dir as `winmm.dll`.'
    ) -join "`n"
    # UTF-8 without a BOM. PowerShell 5.1's -Encoding UTF8 writes EF BB BF, which
    # is what put a BOM at the top of the vendored README.
    [System.IO.File]::WriteAllText((Join-Path $vendorAsiDir 'README.md'), $readme + "`n",
                                   (New-Object System.Text.UTF8Encoding $false))

    # install.cmd records the loader version into the state file the launcher
    # reads, so it has to move with the vendored DLL. Left behind, the state file
    # names a version that is not the one on disk and nothing compares the two.
    $installCmd = Join-Path $projectDir 'scripts/install.cmd'
    $loaderVersion = $meta.Tag -replace '^v', ''
    (Get-Content -LiteralPath $installCmd) `
        -replace 'set "ASI_LOADER_VERSION=.*"', "set `"ASI_LOADER_VERSION=$loaderVersion`"" |
        Set-Content -LiteralPath $installCmd

    Write-Host "  tag=$($meta.Tag) sha256=$($dllSha.Substring(0,12))..." -ForegroundColor DarkGray
} finally {
    Remove-Item $tempZip -Force -ErrorAction SilentlyContinue
}

Write-Host ""
Write-Host "vendor/ultimate-asi-loader refreshed. Review and commit." -ForegroundColor Green
