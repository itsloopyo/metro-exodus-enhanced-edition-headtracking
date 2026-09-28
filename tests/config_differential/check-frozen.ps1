# Fails when a file frozen.tsv records has changed: the frozen legacy reader, or a core source
# it compiles. Each one is what an old MetroExodusHeadTracking.ini is read through, so a change
# moves what a player's old file converts to.
$ErrorActionPreference = 'Stop'

# Not Get-FileHash: Windows PowerShell 5.1 autoloads it from a script module,
# and a powershell.exe started from pwsh (GitHub Actions' shell: pwsh)
# inherits pwsh's PSModulePath, resolves the Core-only
# Microsoft.PowerShell.Utility first and reports the cmdlet as not recognized.
function Get-Sha256Hex {
    param([Parameter(Mandatory)][string]$LiteralPath)

    $sha    = [System.Security.Cryptography.SHA256]::Create()
    $stream = [System.IO.File]::OpenRead((Convert-Path -LiteralPath $LiteralPath))
    try {
        return [BitConverter]::ToString($sha.ComputeHash($stream)).Replace('-', '').ToLowerInvariant()
    } finally {
        $stream.Dispose()
        $sha.Dispose()
    }
}

$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$record = Join-Path $PSScriptRoot 'frozen.tsv'
$changed = @()
foreach ($line in [System.IO.File]::ReadAllLines($record)) {
    if ($line -eq '' -or $line.StartsWith('#')) { continue }
    $path, $sha = $line.Split("`t")[0, 1]
    if ($path.Contains(':')) { continue }
    $actual = Get-Sha256Hex (Join-Path $repo $path)
    if ($actual -ne $sha) { $changed += "$path is $actual, frozen.tsv records $sha" }
}
if ($changed.Count -gt 0) {
    throw "Frozen config reader sources changed:`n  $($changed -join "`n  ")"
}
Write-Host 'frozen config reader: unchanged'
