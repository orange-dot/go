@echo off
REM EK-KOR v2 Build Script for Windows (CMD)
REM
REM Usage:
REM   build         - Build all
REM   build c       - Build C only
REM   build rust    - Build Rust only
REM   build test    - Run all tests
REM   build sim     - Run simulator
REM   build clean   - Clean all

setlocal enabledelayedexpansion

set TARGET=%1
if "%TARGET%"=="" set TARGET=all

if "%TARGET%"=="help" goto :help
if "%TARGET%"=="all" goto :all
if "%TARGET%"=="c" goto :build_c
if "%TARGET%"=="rust" goto :build_rust
if "%TARGET%"=="test" goto :test
if "%TARGET%"=="test-c" goto :test_c
if "%TARGET%"=="test-rust" goto :test_rust
if "%TARGET%"=="sim" goto :sim
if "%TARGET%"=="clean" goto :clean

echo Unknown target: %TARGET%
goto :help

:all
call :build_c
if errorlevel 1 exit /b 1
call :build_rust
if errorlevel 1 exit /b 1
goto :eof

:build_c
echo === Building C ===
if not exist build\c mkdir build\c
pushd build\c
cmake ..\..\c -DCMAKE_BUILD_TYPE=Debug -DEKK_PLATFORM=posix -DEKK_BUILD_TESTS=ON
if errorlevel 1 (
    popd
    exit /b 1
)
cmake --build . --config Debug
if errorlevel 1 (
    popd
    exit /b 1
)
popd
echo C build successful
goto :eof

:build_rust
echo === Building Rust ===
pushd rust
cargo build
if errorlevel 1 (
    popd
    exit /b 1
)
popd
echo Rust build successful
goto :eof

:test
call :test_c
if errorlevel 1 exit /b 1
call :test_rust
if errorlevel 1 exit /b 1
echo === Running test vectors ===
python tools\run_tests.py
goto :eof

:test_c
echo === Running C tests ===
pushd build\c
ctest --output-on-failure
popd
goto :eof

:test_rust
echo === Running Rust tests ===
pushd rust
cargo test
popd
goto :eof

:sim
echo === Running simulator ===
python sim\multi_module.py --modules 49 --ticks 1000
goto :eof

:clean
echo === Cleaning ===
if exist build rmdir /s /q build
pushd rust
cargo clean
popd
echo Clean complete
goto :eof

:help
echo.
echo EK-KOR v2 Build Script (CMD)
echo ============================
echo.
echo Usage: build [target]
echo.
echo Targets:
echo   all       Build both C and Rust (default)
echo   c         Build C library
echo   rust      Build Rust library
echo   test      Run all tests
echo   test-c    Run C tests only
echo   test-rust Run Rust tests only
echo   sim       Run Python simulator
echo   clean     Clean all build artifacts
echo   help      Show this help
echo.
echo For more options, use PowerShell: .\build.ps1 -Help
echo.
goto :eof
