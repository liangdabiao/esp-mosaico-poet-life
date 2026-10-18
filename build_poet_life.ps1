# build_poet_life.ps1 -- firmware build for projects/poet_life.
#
# Carries the two verified host fixes from build_tamaweb.ps1:
#   1) Git Bash leaks MSYSTEM into child processes; ESP-IDF's export aborts with
#      "MSys/Mingw is not supported", so scrub those vars before building.
#   2) ninja's .ninja_deps records /D:/-style paths for GSP-generated files, so an
#      incremental build dies in FindFirstFileExA. Delete it before each build.
# Keep this file ASCII-only: PowerShell 5.x decodes a BOM-less .ps1 with the
# system ANSI codepage (GBK here) and non-ASCII bytes can break parsing.
param(
    [switch]$Clean
)

$ErrorActionPreference = "Stop"

# Same list as .agents/tools/mosaico-flash.ps1 (P2): MSYS itself must go too, or
# idf_tools.py export runs half-MSYS-aware and never reports IDF_PYTHON_ENV_PATH.
foreach ($v in @('MSYSTEM', 'MSYS', 'MSYSCON', 'MINGW_PREFIX', 'MINGW_CHOST', 'CHERE_INVOKING')) {
    if (Test-Path "Env:\$v") { Remove-Item "Env:\$v" }
}

$workspace = "D:\esp-mo\esp-mosaico-vibe-main"
$project = "projects/poet_life"
$build = "$workspace\projects\poet_life\build"
$script = "$workspace\submodule\esp-mosaico-utils\mosaico-tools\skills\idf-low-noise-build\scripts\idf_low_noise_build.py"

if ($Clean -and (Test-Path $build)) {
    Write-Host "[build] clean: removing $build"
    Remove-Item $build -Recurse -Force
}

if (Test-Path "$build\.ninja_deps") {
    Write-Host "[build] removing stale .ninja_deps (ninja /D:/ path bug)"
    Remove-Item "$build\.ninja_deps" -Force
}

Write-Host "[build] building $project ..."
Push-Location $workspace
try {
    $env:IDF_PATH = "D:\esp-mo\esp-idf"
    python $script --project $project build
    if ($LASTEXITCODE -ne 0) { throw "build failed (exit $LASTEXITCODE)" }
} finally {
    Pop-Location
}
Write-Host "[build] OK"
