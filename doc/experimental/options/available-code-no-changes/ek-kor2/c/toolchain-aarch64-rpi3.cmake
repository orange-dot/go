# CMake Toolchain File for Raspberry Pi 3B+ (AArch64 bare-metal)
#
# Usage:
#   cmake -DCMAKE_TOOLCHAIN_FILE=toolchain-aarch64-rpi3.cmake -DEKK_PLATFORM=rpi3 ..
#
# Prerequisites:
#   - aarch64-linux-gnu-gcc (or aarch64-none-elf-gcc)
#   - On Ubuntu/Debian: apt install gcc-aarch64-linux-gnu
#   - On Windows: Use ARM GNU Toolchain or WSL

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

# Cross-compiler prefix (adjust if using different toolchain)
set(CROSS_COMPILE "aarch64-linux-gnu-" CACHE STRING "Cross-compiler prefix")

# Compilers
set(CMAKE_C_COMPILER   ${CROSS_COMPILE}gcc)
set(CMAKE_ASM_COMPILER ${CROSS_COMPILE}gcc)
set(CMAKE_OBJCOPY      ${CROSS_COMPILE}objcopy)
set(CMAKE_OBJDUMP      ${CROSS_COMPILE}objdump)
set(CMAKE_SIZE         ${CROSS_COMPILE}size)
set(CMAKE_AR           ${CROSS_COMPILE}ar)
set(CMAKE_RANLIB       ${CROSS_COMPILE}ranlib)

# Don't try to compile test programs (bare-metal won't run on host)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# CPU flags for Cortex-A53
set(CPU_FLAGS "-mcpu=cortex-a53 -march=armv8-a")

# Compiler flags
set(CMAKE_C_FLAGS_INIT
    "${CPU_FLAGS} -ffreestanding -fno-builtin -nostdlib -Wall -Wextra"
)

set(CMAKE_ASM_FLAGS_INIT
    "${CPU_FLAGS} -ffreestanding"
)

# Debug/Release flags
set(CMAKE_C_FLAGS_DEBUG "-O0 -g3 -DDEBUG")
set(CMAKE_C_FLAGS_RELEASE "-O2 -DNDEBUG")
set(CMAKE_C_FLAGS_MINSIZEREL "-Os -DNDEBUG")
set(CMAKE_C_FLAGS_RELWITHDEBINFO "-O2 -g -DNDEBUG")

# Linker flags
set(CMAKE_EXE_LINKER_FLAGS_INIT
    "-nostdlib -nostartfiles -Wl,--gc-sections"
)

# Search paths (don't search host paths)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
