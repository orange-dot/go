# EK-KOR v2 Build Script for Windows (PowerShell)
#
# Usage:
#   .\build.ps1                  # Build all
#   .\build.ps1 -Target c        # Build C only
#   .\build.ps1 -Target rust     # Build Rust only
#   .\build.ps1 -Target test     # Run all tests
#   .\build.ps1 -Target sim      # Run simulator
#   .\build.ps1 -Target clean    # Clean all

param(
    [ValidateSet('all', 'c', 'rust', 'test', 'test-c', 'test-rust', 'test-vectors', 'sim', 'clean', 'help')]
    [string]$Target = 'all',

    [ValidateSet('Debug', 'Release')]
    [string]$BuildType = 'Debug',

    [ValidateSet('posix', 'stm32g474', 'tricore')]
    [string]$Platform = 'posix'
)

$ErrorActionPreference = 'Stop'

$ProjectRoot = $PSScriptRoot
$BuildDir = Join-Path $ProjectRoot 'build'
$CDir = Join-Path $ProjectRoot 'c'
$RustDir = Join-Path $ProjectRoot 'rust'
$ToolsDir = Join-Path $ProjectRoot 'tools'
$SimDir = Join-Path $ProjectRoot 'sim'

function Write-Header($text) {
    Write-Host ""
    Write-Host "=== $text ===" -ForegroundColor Cyan
}

function Build-C {
    Write-Header "Building C"

    $CBuildDir = Join-Path $BuildDir 'c'

    if (-not (Test-Path $CBuildDir)) {
        New-Item -ItemType Directory -Path $CBuildDir -Force | Out-Null
    }

    Push-Location $CBuildDir
    try {
        # Configure with CMake
        & cmake $CDir `
            -DCMAKE_BUILD_TYPE=$BuildType `
            -DEKK_PLATFORM=$Platform `
            -DEKK_BUILD_TESTS=ON

        if ($LASTEXITCODE -ne 0) { throw "CMake configuration failed" }

        # Build
        & cmake --build . --config $BuildType

        if ($LASTEXITCODE -ne 0) { throw "C build failed" }

        Write-Host "C build successful" -ForegroundColor Green
    }
    finally {
        Pop-Location
    }
}

function Build-Rust {
    Write-Header "Building Rust"

    Push-Location $RustDir
    try {
        if ($BuildType -eq 'Release') {
            & cargo build --release
        } else {
            & cargo build
        }

        if ($LASTEXITCODE -ne 0) { throw "Rust build failed" }

        Write-Host "Rust build successful" -ForegroundColor Green
    }
    finally {
        Pop-Location
    }
}

function Test-C {
    Write-Header "Running C tests"

    $CBuildDir = Join-Path $BuildDir 'c'

    if (-not (Test-Path $CBuildDir)) {
        Write-Host "C not built yet. Building..." -ForegroundColor Yellow
        Build-C
    }

    Push-Location $CBuildDir
    try {
        & ctest --output-on-failure --build-config $BuildType

        if ($LASTEXITCODE -ne 0) { throw "C tests failed" }

        Write-Host "C tests passed" -ForegroundColor Green
    }
    finally {
        Pop-Location
    }
}

function Test-Rust {
    Write-Header "Running Rust tests"

    Push-Location $RustDir
    try {
        & cargo test

        if ($LASTEXITCODE -ne 0) { throw "Rust tests failed" }

        Write-Host "Rust tests passed" -ForegroundColor Green
    }
    finally {
        Pop-Location
    }
}

function Test-Vectors {
    Write-Header "Running cross-language test vectors"

    $RunTests = Join-Path $ToolsDir 'run_tests.py'
    & python $RunTests

    if ($LASTEXITCODE -ne 0) { throw "Test vectors failed" }

    Write-Host "Test vectors passed" -ForegroundColor Green
}

function Run-Simulator {
    Write-Header "Running multi-module simulator"

    $Simulator = Join-Path $SimDir 'multi_module.py'
    & python $Simulator --modules 49 --ticks 1000
}

function Clean-All {
    Write-Header "Cleaning build artifacts"

    if (Test-Path $BuildDir) {
        Remove-Item -Recurse -Force $BuildDir
        Write-Host "Removed $BuildDir" -ForegroundColor Yellow
    }

    Push-Location $RustDir
    try {
        & cargo clean
        Write-Host "Cleaned Rust artifacts" -ForegroundColor Yellow
    }
    finally {
        Pop-Location
    }

    Write-Host "Clean complete" -ForegroundColor Green
}

function Show-Help {
    Write-Host @"

EK-KOR v2 Build Script (PowerShell)
===================================

Usage:
    .\build.ps1 [options]

Options:
    -Target <target>     Build target (default: all)
    -BuildType <type>    Debug or Release (default: Debug)
    -Platform <platform> posix, stm32g474, or tricore (default: posix)

Targets:
    all           Build both C and Rust
    c             Build C library
    rust          Build Rust library
    test          Run all tests
    test-c        Run C unit tests
    test-rust     Run Rust unit tests
    test-vectors  Run cross-language test vectors
    sim           Run Python simulator
    clean         Clean all build artifacts
    help          Show this help message

Examples:
    .\build.ps1                           # Build all in Debug mode
    .\build.ps1 -Target c -BuildType Release
    .\build.ps1 -Target test
    .\build.ps1 -Target sim

"@
}

# Main execution
switch ($Target) {
    'all' {
        Build-C
        Build-Rust
    }
    'c' {
        Build-C
    }
    'rust' {
        Build-Rust
    }
    'test' {
        Test-C
        Test-Rust
        Test-Vectors
    }
    'test-c' {
        Test-C
    }
    'test-rust' {
        Test-Rust
    }
    'test-vectors' {
        Test-Vectors
    }
    'sim' {
        Run-Simulator
    }
    'clean' {
        Clean-All
    }
    'help' {
        Show-Help
    }
}
