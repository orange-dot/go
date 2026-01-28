@echo off
REM Run Elle consistency tests for ROJ consensus (Windows)
REM
REM Usage:
REM   run-elle-tests.bat              Run full suite
REM   run-elle-tests.bat happy        Run single scenario
REM   run-elle-tests.bat --download   Download elle-cli only

setlocal enabledelayedexpansion

REM Configuration
set ELLE_CLI_VERSION=0.1.7
set ELLE_CLI_URL=https://github.com/ligurio/elle-cli/releases/download/%ELLE_CLI_VERSION%/elle-cli-%ELLE_CLI_VERSION%-standalone.jar
set ELLE_CLI_DIR=%USERPROFILE%\.elle-cli
set ELLE_CLI_JAR=%ELLE_CLI_DIR%\elle-cli.jar

REM Get script directory
set SCRIPT_DIR=%~dp0
set PROJECT_DIR=%SCRIPT_DIR%..

REM Parse arguments
if "%1"=="--help" goto :print_usage
if "%1"=="-h" goto :print_usage
if "%1"=="--download" goto :download_only

REM Check prerequisites
call :check_prerequisites
if errorlevel 1 exit /b 1

REM Build harness
call :build_harness
if errorlevel 1 exit /b 1

REM Run tests
if "%1"=="" (
    call :run_suite
) else (
    call :run_scenario %1
)

exit /b %errorlevel%

:print_usage
echo ROJ Elle Consistency Tests (Windows)
echo.
echo Usage:
echo     %~nx0                      Run full test suite
echo     %~nx0 ^<scenario^>           Run single scenario
echo     %~nx0 --download           Download elle-cli only
echo     %~nx0 --help               Show this help
echo.
echo Scenarios:
echo     happy         No faults - baseline test
echo     partition     Network partition mid-test
echo     leader-crash  Leader crashes mid-test
echo     message-loss  10%% message loss throughout
echo     contention    Single key, high contention
exit /b 0

:download_only
call :download_elle_cli
exit /b %errorlevel%

:check_prerequisites
REM Check Rust
where cargo >nul 2>&1
if errorlevel 1 (
    echo [ERROR] Cargo not found. Please install Rust: https://rustup.rs
    exit /b 1
)

REM Check Java
where java >nul 2>&1
if errorlevel 1 (
    echo [ERROR] Java not found. Please install Java 11+: https://adoptium.net
    exit /b 1
)

REM Download elle-cli if needed
call :download_elle_cli
exit /b 0

:download_elle_cli
if exist "%ELLE_CLI_JAR%" (
    echo [INFO] elle-cli already present at %ELLE_CLI_JAR%
    exit /b 0
)

echo [INFO] Downloading elle-cli %ELLE_CLI_VERSION%...
if not exist "%ELLE_CLI_DIR%" mkdir "%ELLE_CLI_DIR%"

powershell -Command "Invoke-WebRequest -Uri '%ELLE_CLI_URL%' -OutFile '%ELLE_CLI_JAR%'"

if exist "%ELLE_CLI_JAR%" (
    echo [INFO] Downloaded elle-cli to %ELLE_CLI_JAR%
    exit /b 0
) else (
    echo [ERROR] Failed to download elle-cli
    exit /b 1
)

:build_harness
echo [INFO] Building roj-elle-harness...
pushd "%PROJECT_DIR%"
cargo build --release -p roj-elle-harness
set BUILD_RESULT=%errorlevel%
popd
exit /b %BUILD_RESULT%

:run_scenario
set SCENARIO=%1
if "%SCENARIO%"=="" set SCENARIO=happy

echo [INFO] Running scenario: %SCENARIO%

if not exist "%PROJECT_DIR%\results" mkdir "%PROJECT_DIR%\results"

"%PROJECT_DIR%\target\release\roj-elle.exe" run --scenario %SCENARIO% --operations 100 --output-dir "%PROJECT_DIR%\results"

set HISTORY_FILE=%PROJECT_DIR%\results\%SCENARIO%-history.json
if exist "%HISTORY_FILE%" (
    echo [INFO] Checking %SCENARIO% with Elle...
    "%PROJECT_DIR%\target\release\roj-elle.exe" check --history "%HISTORY_FILE%" --elle-jar "%ELLE_CLI_JAR%"
)
exit /b 0

:run_suite
echo [INFO] Running full test suite...

if not exist "%PROJECT_DIR%\results" mkdir "%PROJECT_DIR%\results"

"%PROJECT_DIR%\target\release\roj-elle.exe" suite --output-dir "%PROJECT_DIR%\results" --junit-xml "%PROJECT_DIR%\results\junit.xml" --elle-jar "%ELLE_CLI_JAR%"

echo [INFO] Results written to %PROJECT_DIR%\results
exit /b 0
