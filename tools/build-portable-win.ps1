#Requires -Version 5.1
<#
.SYNOPSIS
    Build an x64 Release PrusaSlicer and create a portable ZIP.
.DESCRIPTION
    Uses Visual Studio 2022, CMake and the repository's own dependency source.
    Builds release dependencies once (DEP_DEBUG=OFF); later runs reuse them.
    Resources are copied as real files, never packaged as the build's junction.
.EXAMPLE
    .\tools\build-portable-win.ps1
.EXAMPLE
    .\tools\build-portable-win.ps1 -PackageOnly
.EXAMPLE
    .\tools\build-portable-win.ps1 -RunSeamTests -Jobs 4
#>
[CmdletBinding()]
param(
    [switch] $PackageOnly,
    [switch] $RebuildDependencies,
    [switch] $RunSeamTests,
    [switch] $SkipZip,
    [ValidateRange(1, 16)] [int] $Jobs = 4,
    [string] $DependenciesDestination = ''
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$depsBuild = Join-Path $repoRoot 'deps\build'
$appBuild = Join-Path $repoRoot 'build'
if (-not $DependenciesDestination) {
    $DependenciesDestination = [IO.Path]::GetFullPath((Join-Path $repoRoot '..\PrusaSlicer-deps'))
}
$DependenciesDestination = [IO.Path]::GetFullPath($DependenciesDestination)
$prefix = Join-Path $DependenciesDestination 'usr\local'
$depsReady = Join-Path $depsBuild '.release-deps-ready'
$bin = Join-Path $appBuild 'src\Release'
$distRoot = Join-Path $repoRoot 'dist'
$package = Join-Path $distRoot 'PrusaSlicer-2.9.4-mod'
$zip = Join-Path $distRoot 'PrusaSlicer-2.9.4-mod-win64.zip'

function Invoke-Tool {
    param([string] $Tool, [string[]] $Arguments)
    Write-Host ">> $Tool $($Arguments -join ' ')" -ForegroundColor Cyan
    & $Tool @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "Command failed with exit code $LASTEXITCODE : $Tool"
    }
}

function Require-Tool {
    param([string] $Tool)
    if (-not (Get-Command $Tool -ErrorAction SilentlyContinue)) {
        throw "Missing $Tool. Install the required tool and open a new PowerShell session."
    }
}

Require-Tool 'git'
Require-Tool 'cmake'

$branch = (& git -C $repoRoot branch --show-current).Trim()
if ($LASTEXITCODE -ne 0 -or $branch -ne 'release-2.9.4-mod') {
    throw "Checkout the release-2.9.4-mod branch before building (currently: '$branch')."
}

if (-not $PackageOnly) {
    $vswhere = Join-Path "${env:ProgramFiles(x86)}" 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path -LiteralPath $vswhere)) {
        throw 'Visual Studio vswhere.exe not found. Install Visual Studio 2022 with Desktop development with C++.'
    }
    $vs = & $vswhere '-version' '[17.0,18.0)' '-latest' '-products' '*' '-requires' 'Microsoft.VisualStudio.Component.VC.Tools.x86.x64' '-property' 'installationPath'
    if (-not $vs) {
        throw 'Visual Studio 2022 with MSVC v143 C++ tools was not found. Use Visual Studio Installer.'
    }

    $cmakeVersion = (& cmake --version | Select-Object -First 1)
    if ($cmakeVersion -match 'version\s+(\d+)\.' -and [int]$Matches[1] -ge 4) {
        Write-Warning 'CMake 4.x may reject older third-party dependencies. Prefer CMake 3.31.x if errors mention cmake_minimum_required.'
    }

    if ($RebuildDependencies -or -not (Test-Path -LiteralPath $depsReady) -or -not (Test-Path -LiteralPath $prefix)) {
        Write-Host 'Building RELEASE dependencies (Debug variants disabled).' -ForegroundColor Green
        Invoke-Tool 'cmake' @(
            '-S', (Join-Path $repoRoot 'deps'),
            '-B', $depsBuild,
            '-G', 'Visual Studio 17 2022',
            '-A', 'x64',
            '-DDEP_DEBUG=OFF',
            "-DDESTDIR=$DependenciesDestination"
        )
        # ExternalProject dependencies already parallelize internally. Keeping the
        # top-level dependency graph serial avoids excessive competing link jobs.
        Invoke-Tool 'cmake' @('--build', $depsBuild, '--config', 'Release', '--target', 'ALL_BUILD', '--parallel', '1')
        if (-not (Test-Path -LiteralPath $prefix)) {
            throw "Dependency install location is missing: $prefix"
        }
        'Release dependencies built successfully.' | Set-Content -LiteralPath $depsReady -Encoding ASCII
    }
    else {
        Write-Host 'Reusing existing release dependencies.' -ForegroundColor Green
    }

    Invoke-Tool 'cmake' @(
        '-S', $repoRoot,
        '-B', $appBuild,
        '-G', 'Visual Studio 17 2022',
        '-A', 'x64',
        "-DCMAKE_PREFIX_PATH=$prefix",
        '-DSLIC3R_BUILD_TESTS=ON'
    )

    Write-Host "Building modified application (max top-level jobs: $Jobs)." -ForegroundColor Green
    Invoke-Tool 'cmake' @(
        '--build', $appBuild, '--config', 'Release',
        '--target', 'PrusaSlicer_app_gui', 'PrusaSlicer_app_console',
        'PrusaSlicer_app_gcodeviewer', 'PrusaSlicerDllsCopy',
        '--parallel', "$Jobs"
    )
}

if ($RunSeamTests) {
    if ($PackageOnly) {
        Write-Host 'Running previously compiled seam tests (PackageOnly was specified).'
    }
    else {
        Invoke-Tool 'cmake' @('--build', $appBuild, '--config', 'Release', '--target', 'fff_print_tests', '--parallel', "$Jobs")
    }
    $testExe = Join-Path $appBuild 'tests\fff_print\Release\fff_print_tests.exe'
    if (-not (Test-Path -LiteralPath $testExe)) {
        throw "Seam test executable not found: $testExe"
    }
    Invoke-Tool $testExe @('[Seams]')
}

$required = @('prusa-slicer.exe', 'prusa-slicer-console.exe', 'prusa-gcodeviewer.exe', 'PrusaSlicer.dll', 'WebView2Loader.dll', 'libgmp-10.dll', 'libmpfr-4.dll')
foreach ($name in $required) {
    if (-not (Test-Path -LiteralPath (Join-Path $bin $name))) {
        throw "Missing build output '$name' in '$bin'. Check the preceding build log and build target."
    }
}
$resources = Join-Path $repoRoot 'resources'
if (-not (Test-Path -LiteralPath $resources)) {
    throw "Repository resources directory does not exist: $resources"
}

New-Item -ItemType Directory -Path $distRoot -Force | Out-Null
if (Test-Path -LiteralPath $package) {
    Remove-Item -LiteralPath $package -Recurse -Force
}
New-Item -ItemType Directory -Path $package -Force | Out-Null

# Copy all resulting runtime DLLs, plus the three launchers.
Get-ChildItem -LiteralPath $bin -Filter '*.dll' -File |
    Copy-Item -Destination $package -Force
foreach ($name in @('prusa-slicer.exe', 'prusa-slicer-console.exe', 'prusa-gcodeviewer.exe')) {
    Copy-Item -LiteralPath (Join-Path $bin $name) -Destination $package -Force
}
# The build directory contains a junction to source resources. Copy the source
# directory instead, so the resulting ZIP is independently relocatable.
Copy-Item -LiteralPath $resources -Destination $package -Recurse -Force
Copy-Item -LiteralPath (Join-Path $repoRoot 'LICENSE') -Destination $package -Force

# Opt-in isolated profile storage: use the launcher to keep configuration
# alongside this build. Running prusa-slicer.exe directly uses the normal
# Windows PrusaSlicer configuration location instead.
New-Item -ItemType Directory -Path (Join-Path $package 'config') -Force | Out-Null
@'
@echo off
setlocal
"%~dp0prusa-slicer.exe" --datadir "%~dp0config" %*
endlocal
'@ | Set-Content -LiteralPath (Join-Path $package 'Start-PrusaSlicer-Portable.cmd') -Encoding ASCII

$commit = (& git -C $repoRoot rev-parse --short=12 HEAD).Trim()
@(
    'PrusaSlicer 2.9.4 Mod for Windows x64'
    "Branch: $branch"
    "Commit: $commit"
    'Configuration: Release'
    'Use Start-PrusaSlicer-Portable.cmd for profiles isolated to this folder.'
    'For testing, inspect generated G-code before using with a printer.'
) | Set-Content -LiteralPath (Join-Path $package 'BUILD-INFO.txt') -Encoding UTF8

if (-not $SkipZip) {
    if (Test-Path -LiteralPath $zip) { Remove-Item -LiteralPath $zip -Force }
    Compress-Archive -LiteralPath $package -DestinationPath $zip -CompressionLevel Optimal
}
Write-Host "Portable application: $package" -ForegroundColor Green
if (-not $SkipZip) { Write-Host "Portable ZIP:         $zip" -ForegroundColor Green }
Write-Host 'NOTE: The build must be validated on Windows and its G-code checked before unattended printing.'
