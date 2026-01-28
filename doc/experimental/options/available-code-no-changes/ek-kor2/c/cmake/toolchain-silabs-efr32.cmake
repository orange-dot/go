# Toolchain file for Silicon Labs EFR32MG24 (Cortex-M33) bare-metal
#
# Uses ARM GCC cross compiler from:
#   - arm-none-eabi-gcc (standard ARM toolchain)
#   - Or xPack/ARM GNU toolchain
#
# For Silicon Labs Gecko SDK integration (optional):
#   Set GECKO_SDK_PATH environment variable
#
# Usage:
#   cmake -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-silabs-efr32.cmake \
#         -DEKK_PLATFORM=efr32mg24 ..
#
# Copyright (c) 2026 Elektrokombinacija
# SPDX-License-Identifier: MIT

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

# Find ARM GCC compiler
# Try common names: arm-none-eabi-gcc, arm-unknown-eabi-gcc
find_program(ARM_CC arm-none-eabi-gcc)
if(NOT ARM_CC)
    find_program(ARM_CC arm-unknown-eabi-gcc)
endif()
if(NOT ARM_CC)
    message(FATAL_ERROR "ARM GCC cross compiler not found. Install arm-none-eabi-gcc.")
endif()

# Set compilers
set(CMAKE_C_COMPILER arm-none-eabi-gcc)
set(CMAKE_CXX_COMPILER arm-none-eabi-g++)
set(CMAKE_ASM_COMPILER arm-none-eabi-gcc)
set(CMAKE_OBJCOPY arm-none-eabi-objcopy)
set(CMAKE_OBJDUMP arm-none-eabi-objdump)
set(CMAKE_SIZE arm-none-eabi-size)
set(CMAKE_AR arm-none-eabi-ar)
set(CMAKE_RANLIB arm-none-eabi-ranlib)

# Cortex-M33 architecture flags
# - mcpu=cortex-m33: Target CPU
# - mthumb: Thumb instruction set
# - mfloat-abi=hard: Hardware floating point
# - mfpu=fpv5-sp-d16: Single-precision FPU (M33)
# - march=armv8-m.main+dsp: ARMv8-M with DSP extension
set(CPU_FLAGS "-mcpu=cortex-m33 -mthumb -mfloat-abi=hard -mfpu=fpv5-sp-d16")

# Compiler flags for bare-metal
set(CMAKE_C_FLAGS_INIT "${CPU_FLAGS} -ffreestanding -ffunction-sections -fdata-sections -Wall -Wextra")
set(CMAKE_CXX_FLAGS_INIT "${CMAKE_C_FLAGS_INIT} -fno-exceptions -fno-rtti")
set(CMAKE_ASM_FLAGS_INIT "${CPU_FLAGS} -x assembler-with-cpp")

# Linker flags
set(CMAKE_EXE_LINKER_FLAGS_INIT "${CPU_FLAGS} -Wl,--gc-sections -Wl,--print-memory-usage --specs=nosys.specs --specs=nano.specs")

# Don't try to compile test programs (we're cross-compiling for bare-metal)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# Search paths
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# Define for conditional compilation
add_compile_definitions(EFR32MG24)
add_compile_definitions(EKK_PLATFORM_EFR32MG24)

# Optional: Silicon Labs Gecko SDK integration
# If GECKO_SDK_PATH is set, add include paths for device headers
if(DEFINED ENV{GECKO_SDK_PATH})
    set(GECKO_SDK_PATH $ENV{GECKO_SDK_PATH})
    message(STATUS "Using Gecko SDK: ${GECKO_SDK_PATH}")

    # Device headers
    include_directories(
        ${GECKO_SDK_PATH}/platform/Device/SiliconLabs/EFR32MG24/Include
        ${GECKO_SDK_PATH}/platform/CMSIS/Core/Include
        ${GECKO_SDK_PATH}/platform/common/inc
    )
else()
    message(STATUS "Gecko SDK not found. Using minimal register definitions.")
    message(STATUS "Set GECKO_SDK_PATH environment variable for full SDK support.")
endif()

# Debug message
message(STATUS "EFR32MG24 Toolchain: ${CMAKE_C_COMPILER}")
message(STATUS "CPU Flags: ${CPU_FLAGS}")
