# ARM Cortex-M4 Toolchain for STM32G474
#
# Usage:
#   cmake -DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi.cmake \
#         -DEKK_PLATFORM=stm32g474 ..
#
# Requirements:
#   - arm-none-eabi-gcc (GNU Arm Embedded Toolchain)
#   - Windows: Download from developer.arm.com or use MSYS2
#   - Linux: apt install gcc-arm-none-eabi
#
# Copyright (c) 2026 Elektrokombinacija
# SPDX-License-Identifier: MIT

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

# Toolchain prefix
set(TOOLCHAIN_PREFIX arm-none-eabi-)

# Find toolchain programs
find_program(CMAKE_C_COMPILER ${TOOLCHAIN_PREFIX}gcc)
find_program(CMAKE_CXX_COMPILER ${TOOLCHAIN_PREFIX}g++)
find_program(CMAKE_ASM_COMPILER ${TOOLCHAIN_PREFIX}gcc)
find_program(CMAKE_AR ${TOOLCHAIN_PREFIX}ar)
find_program(CMAKE_OBJCOPY ${TOOLCHAIN_PREFIX}objcopy)
find_program(CMAKE_OBJDUMP ${TOOLCHAIN_PREFIX}objdump)
find_program(CMAKE_SIZE ${TOOLCHAIN_PREFIX}size)

# Don't try to run test executables on host
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# Cortex-M4 with FPU flags
set(CPU_FLAGS "-mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard")

# Common C/ASM flags
set(COMMON_FLAGS "${CPU_FLAGS} -ffunction-sections -fdata-sections -fno-common")

# Debug flags
set(DEBUG_FLAGS "-g3 -gdwarf-2 -O0")

# Release flags
set(RELEASE_FLAGS "-Os -DNDEBUG")

# Warning flags
set(WARNING_FLAGS "-Wall -Wextra -Wpedantic -Wno-unused-parameter")

# C standard
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Combine flags
set(CMAKE_C_FLAGS "${COMMON_FLAGS} ${WARNING_FLAGS}" CACHE STRING "C flags" FORCE)
set(CMAKE_C_FLAGS_DEBUG "${DEBUG_FLAGS}" CACHE STRING "C debug flags" FORCE)
set(CMAKE_C_FLAGS_RELEASE "${RELEASE_FLAGS}" CACHE STRING "C release flags" FORCE)
set(CMAKE_C_FLAGS_MINSIZEREL "-Os -DNDEBUG" CACHE STRING "C minsize flags" FORCE)
set(CMAKE_C_FLAGS_RELWITHDEBINFO "-Os -g -DNDEBUG" CACHE STRING "C relwithdebinfo flags" FORCE)

set(CMAKE_ASM_FLAGS "${COMMON_FLAGS}" CACHE STRING "ASM flags" FORCE)

# Linker flags
set(LINKER_FLAGS "-Wl,--gc-sections -Wl,--print-memory-usage")
set(CMAKE_EXE_LINKER_FLAGS "${CPU_FLAGS} ${LINKER_FLAGS} --specs=nano.specs --specs=nosys.specs" CACHE STRING "Linker flags" FORCE)

# Search paths
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# Macro to create firmware targets
macro(add_firmware TARGET)
    # Get linker script path
    set(LINKER_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/stm32g474.ld")

    # Add linker script to linker flags
    target_link_options(${TARGET} PRIVATE
        -T${LINKER_SCRIPT}
        -Wl,-Map=${TARGET}.map
    )

    # Add dependency on linker script
    set_target_properties(${TARGET} PROPERTIES
        LINK_DEPENDS ${LINKER_SCRIPT}
        SUFFIX ".elf"
    )

    # Generate binary file
    add_custom_command(TARGET ${TARGET} POST_BUILD
        COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${TARGET}> ${TARGET}.bin
        COMMENT "Creating ${TARGET}.bin"
    )

    # Generate hex file
    add_custom_command(TARGET ${TARGET} POST_BUILD
        COMMAND ${CMAKE_OBJCOPY} -O ihex $<TARGET_FILE:${TARGET}> ${TARGET}.hex
        COMMENT "Creating ${TARGET}.hex"
    )

    # Print size
    add_custom_command(TARGET ${TARGET} POST_BUILD
        COMMAND ${CMAKE_SIZE} $<TARGET_FILE:${TARGET}>
        COMMENT "Size of ${TARGET}:"
    )
endmacro()
