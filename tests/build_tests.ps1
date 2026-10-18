# SPDX-License-Identifier: Apache-2.0
# Host unit tests for the pure-C survival kernel and the text layout layer
# (no ESP-IDF, no LVGL, no engine -- tests/host_compat stands in for the blit API).
# Usage: powershell -ExecutionPolicy Bypass -File projects/poet_life/tests/build_tests.ps1
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot           # projects/poet_life
$main = Join-Path $root 'main'
$gen = Join-Path $main 'generated'
$compat = Join-Path $PSScriptRoot 'host_compat'
$gcc = 'D:\esp-mo\mingw\w64devkit\bin\gcc.exe'
if (-not (Test-Path $gcc)) { $gcc = 'gcc' }

$suites = @(
    @{ name = 'poets_game'; inc = @($main, $gen);
       srcs = @((Join-Path $main 'poets_game.c'), (Join-Path $gen 'poets_data.c'),
                 (Join-Path $main 'pl_save.c')) },
    @{ name = 'pl_text'; inc = @($main, $compat);
       srcs = @((Join-Path $main 'pl_font.c'), (Join-Path $main 'pl_text.c'),
                 (Join-Path $main 'pl_cjk_glyph_data.c'),
                 (Join-Path $PSScriptRoot 'stub_gfx.c')) }
)

# PL_TEXT_DIAG=1 用 diag_text.c 顶替 test_pl_text.c，打印实际断行结果。
$fail = 0
foreach ($suite in $suites) {
    $test = if ($env:PL_TEXT_DIAG -and $suite.name -eq 'pl_text') {
        Join-Path $PSScriptRoot 'diag_text.c'
    } else {
        Join-Path $PSScriptRoot ('test_' + $suite.name + '.c')
    }
    $out = Join-Path $PSScriptRoot ('test_' + $suite.name + '.exe')
    $args2 = @('-std=c11', '-Wall', '-Wextra', '-Werror')
    foreach ($i in $suite.inc) { $args2 += @('-I', $i) }
    $args2 += $test
    foreach ($s in $suite.srcs) { $args2 += $s }
    $args2 += @('-o', $out)
    Write-Host "== compile test_$($suite.name) =="
    & $gcc @args2
    if ($LASTEXITCODE -ne 0) { Write-Host "COMPILE FAILED ($LASTEXITCODE)"; $fail = 1; continue }
    & $out
    if ($LASTEXITCODE -ne 0) { $fail = 1 }
}
Write-Host "suites exit: $fail"
exit $fail
