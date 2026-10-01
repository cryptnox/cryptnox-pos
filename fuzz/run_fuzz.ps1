<#
.SYNOPSIS
  Build (if needed) and run the cryptnox-pos libFuzzer harnesses on Windows.

.DESCRIPTION
  Compiles the harnesses with clang (LLVM for Windows) inside a Visual Studio
  developer environment (needed for the MSVC/SDK headers), puts the ASan
  runtime DLL on PATH, then runs the selected target(s). Crashes land in
  fuzz\artifacts\crash-<hash> -- replay one with:
      .\fuzz_<target>.exe artifacts\crash-<hash>

.PARAMETER Target
  eth_rlp | parse_address | eth_rpc_json | all   (default: all)

.PARAMETER Seconds
  Wall-clock budget per target (default: 300). Use 3600 for an hour, etc.

.PARAMETER Jobs
  Parallel worker processes. 0 (default) = single process with -fork=1
  (robust: keeps going past a crash). >0 = -jobs/-workers parallel fuzzing.

.PARAMETER Build
  Force a rebuild even if the .exe already exists.

.EXAMPLE
  .\run_fuzz.ps1                              # all three, 5 min each
  .\run_fuzz.ps1 -Target eth_rpc_json -Seconds 3600 -Jobs 8
  .\run_fuzz.ps1 -Target parse_address -Seconds 60
#>
param(
    [ValidateSet('eth_rlp','parse_address','eth_rpc_json','all')]
    [string]$Target  = 'all',
    [int]   $Seconds = 300,
    [int]   $Jobs    = 0,
    [switch]$Build
)

# 'Continue', not 'Stop': libFuzzer writes all its normal output (INFO, stats)
# to stderr, which under 'Stop' PowerShell turns into a fatal NativeCommandError.
# Real failures are caught by explicit `throw` (always terminating) + Test-Path.
$ErrorActionPreference = 'Continue'
Set-Location $PSScriptRoot

# Toolchain locations
$clang = "C:\Program Files\LLVM\bin\clang++.exe"
if (-not (Test-Path $clang)) {
    throw "clang not found at $clang - install LLVM for Windows (winget install LLVM.LLVM)."
}

$sdk   = Join-Path $PSScriptRoot "..\cryptnox-sdk-esp32\cryptnox-sdk-cpp"
$cjson = if ($env:IDF_PATH) { Join-Path $env:IDF_PATH "components\json\cJSON" }
         else { "C:\Espressif\frameworks\esp-idf-v5.5\components\json\cJSON" }

# Map a target name to its harness sources / extra build args.
$specs = @{
    eth_rlp        = @{ srcs = @('fuzz_eth_rlp.cpp');        inc = @("-I$sdk") }
    parse_address  = @{ srcs = @('fuzz_parse_address.cpp');  inc = @() }
    eth_rpc_json   = @{ srcs = @('fuzz_eth_rpc_json.cpp', (Join-Path $cjson 'cJSON.c'));
                        inc = @("-I$sdk", "-I$cjson") }
}
$targets = if ($Target -eq 'all') { 'eth_rlp','parse_address','eth_rpc_json' } else { @($Target) }

New-Item -ItemType Directory -Force artifacts | Out-Null

# Build phase (only if an exe is missing or -Build)
$needBuild = $Build -or ($targets | Where-Object { -not (Test-Path "fuzz_$_.exe") })
if ($needBuild) {
    Write-Host "== entering VS developer environment (for MSVC/SDK headers) ==" -ForegroundColor Cyan
    # Pick an install that actually has the MSVC C++ toolset (not bare Build
    # Tools without it). vswhere -requires filters to that component.
    $vsw = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    $vs  = if (Test-Path $vsw) {
               & $vsw -latest -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
                      -property installationPath
           }
    if (-not $vs -or -not (Test-Path $vs)) {
        $vs = "C:\Program Files\Microsoft Visual Studio\2022\Community"
    }
    Write-Host "   using VS: $vs"
    Import-Module (Join-Path $vs "Common7\Tools\Microsoft.VisualStudio.DevShell.dll")
    Enter-VsDevShell -VsInstallPath $vs -DevCmdArguments "-arch=x64" -SkipAutomaticLocation | Out-Null

    foreach ($t in $targets) {
        if (-not $Build -and (Test-Path "fuzz_$t.exe")) { continue }
        Write-Host "== building fuzz_$t ==" -ForegroundColor Cyan
        $cargs = @('-fsanitize=fuzzer,address','-g','-O1','-std=c++14') +
                 $specs[$t].inc + $specs[$t].srcs + @('-o', "fuzz_$t.exe")
        & $clang @cargs
        if (-not (Test-Path "fuzz_$t.exe")) { throw "build failed: fuzz_$t" }
    }
}

# Run phase (ASan runtime DLL must be reachable)
$asanDir = Split-Path (Get-ChildItem "C:\Program Files\LLVM\lib\clang\*\lib\windows\clang_rt.asan_dynamic-x86_64.dll" |
                       Select-Object -First 1).FullName
$env:PATH = "C:\Program Files\LLVM\bin;$asanDir;$env:PATH"

foreach ($t in $targets) {
    Write-Host "`n#### fuzz_$t  (${Seconds}s) ####" -ForegroundColor Green
    $runArgs = @("corpus\$t", "-max_total_time=$Seconds",
                 "-print_final_stats=1", "-artifact_prefix=artifacts\")
    if ($Jobs -gt 0) { $runArgs += @("-jobs=$Jobs", "-workers=$Jobs") }
    else             { $runArgs += "-fork=1" }
    & ".\fuzz_$t.exe" @runArgs
}

Write-Host "`n== crashes (if any) ==" -ForegroundColor Yellow
Get-ChildItem artifacts -ErrorAction SilentlyContinue | Select-Object Name,Length
