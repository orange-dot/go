# Toolchain file for Raspberry Pi 3 (AArch64) bare-metal
# Uses aarch64-linux-gnu-gcc from Ubuntu packages

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

# Compiler
set(CMAKE_C_COMPILER aarch64-linux-gnu-gcc)
set(CMAKE_ASM_COMPILER aarch64-linux-gnu-gcc)
set(CMAKE_OBJCOPY aarch64-linux-gnu-objcopy)
set(CMAKE_SIZE aarch64-linux-gnu-size)

# Compiler flags for bare-metal AArch64
set(CMAKE_C_FLAGS_INIT "-ffreestanding -nostdlib -nostartfiles -mcpu=cortex-a53 -Wall -Wextra")
set(CMAKE_ASM_FLAGS_INIT "-ffreestanding -nostdlib -nostartfiles -mcpu=cortex-a53")
set(CMAKE_EXE_LINKER_FLAGS_INIT "-nostdlib -nostartfiles -lgcc")

# Don't try to compile test programs (we're cross-compiling for bare-metal)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# Search paths
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
