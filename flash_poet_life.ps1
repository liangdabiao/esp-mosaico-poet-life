# flash_poet_life.ps1 -- install projects/poet_life over ESP-Iris Vibe Mode.
#
# Host-specific fixes carried over from build_poet_life.ps1 / flash_tamaweb.ps1:
#   1) Git Bash leaks MSYSTEM into child processes; ESP-IDF's export aborts with
#      "MSys/Mingw is not supported", so scrub those vars first.
#   2) system-update re-runs the bundle build. build/.ninja_deps may hold the
#      GSP-generated "/D:/" entries that make ninja die in FindFirstFileExA;
#      -MoveDepsAside moves the index away first (that forces a FULL rebuild,
#      25+ min), so try without it and only pay that price when ninja complains.
#   3) -SkipBuild reuses build/poet_life-system-update.irisfw as it stands: use it
#      after a write that lost its Gateway lease mid-flight, never after a rebuild.
# Keep this file ASCII-only: PowerShell 5.x decodes a BOM-less .ps1 with the
# system ANSI codepage (GBK here) and non-ASCII bytes can break parsing.
param(
    [int]$TimeoutSec = 1800,
    [switch]$SkipBuild,
    [switch]$MoveDepsAside
)

$ErrorActionPreference = "Stop"

foreach ($v in @('MSYSTEM', 'MSYS', 'MSYSCON', 'MINGW_PREFIX', 'MINGW_CHOST', 'CHERE_INVOKING')) {
    if (Test-Path "Env:\$v") { Remove-Item "Env:\$v" }
}

$workspace = "D:\esp-mo\esp-mosaico-vibe-main"
$project = "projects/poet_life"
$deps = "$workspace\projects\poet_life\build\.ninja_deps"

if ($MoveDepsAside -and (Test-Path $deps)) {
    Write-Host "[flash] moving .ninja_deps aside (ninja /D:/ path bug; full rebuild follows)"
    Move-Item $deps "$deps.stale.$(Get-Date -Format yyyyMMddHHmmss)" -Force
}

$extra = @()
if ($SkipBuild) { $extra = @('--skip-build') }

Write-Host "[flash] iris system-update --project $project $extra (timeout $TimeoutSec s)"
Push-Location $workspace
try {
    $env:IDF_PATH = "D:\esp-mo\esp-idf"
    python mosaico.py iris system-update --project $project --timeout $TimeoutSec @extra
    if ($LASTEXITCODE -ne 0) { throw "system-update failed (exit $LASTEXITCODE)" }
} finally {
    Pop-Location
}
Write-Host "[flash] OK"
