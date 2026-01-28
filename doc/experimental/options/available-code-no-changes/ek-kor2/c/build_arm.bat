@echo off
REM Build EK-KOR v2 for STM32G474 (ARM Cortex-M4)
REM
REM Prerequisites:
REM   - arm-none-eabi-gcc in PATH
REM   - CMake 3.16+
REM
REM Install toolchain:
REM   - MSYS2: pacman -S mingw-w64-x86_64-arm-none-eabi-gcc
REM   - Or: https://developer.arm.com/downloads/-/gnu-rm

setlocal

REM Check for ARM GCC
where arm-none-eabi-gcc >nul 2>&1
if %ERRORLEVEL% neq 0 (
    echo ERROR: arm-none-eabi-gcc not found in PATH
    echo.
    echo Install with MSYS2:
    echo   pacman -S mingw-w64-x86_64-arm-none-eabi-gcc
    echo.
    echo Or download from:
    echo   https://developer.arm.com/downloads/-/gnu-rm
    exit /b 1
)

echo ARM GCC found:
arm-none-eabi-gcc --version | head -1

REM Create build directory
if not exist build_arm mkdir build_arm
cd build_arm

REM Configure with CMake
echo.
echo Configuring CMake...
cmake -G "MinGW Makefiles" ^
      -DCMAKE_TOOLCHAIN_FILE=../cmake/arm-none-eabi.cmake ^
      -DEKK_PLATFORM=stm32g474 ^
      -DCMAKE_BUILD_TYPE=Debug ^
      ..

if %ERRORLEVEL% neq 0 (
    echo CMake configuration failed!
    exit /b 1
)

REM Build
echo.
echo Building...
cmake --build . -- -j%NUMBER_OF_PROCESSORS%

if %ERRORLEVEL% neq 0 (
    echo Build failed!
    exit /b 1
)

echo.
echo ========================================
echo Build successful!
echo ========================================
echo.
echo Output files:
echo   build_arm\ekk_test.elf  - ELF for Renode/debugger
echo   build_arm\ekk_test.bin  - Binary for flashing
echo   build_arm\ekk_test.hex  - Intel HEX for flashing
echo   renode\ekk_test.elf     - Copied for Renode
echo.
echo To run in Renode:
echo   cd ..\renode
echo   renode ekk_single.resc
echo   (in monitor) start
echo.

endlocal
