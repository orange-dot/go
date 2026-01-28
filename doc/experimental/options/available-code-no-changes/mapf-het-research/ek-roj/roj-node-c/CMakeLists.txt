cmake_minimum_required(VERSION 3.10)
project(roj-node-c VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Platform-specific settings
if(WIN32)
    add_definitions(-D_WIN32_WINNT=0x0601)
    set(PLATFORM_LIBS ws2_32 iphlpapi)
else()
    set(PLATFORM_LIBS pthread)
endif()

# Include directories
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/deps)

# Source files
set(SOURCES
    src/main.c
    src/discovery.c
    src/transport.c
    src/consensus.c
    deps/cJSON.c
)

# Executable
add_executable(roj-node-c ${SOURCES})

# Link libraries
target_link_libraries(roj-node-c ${PLATFORM_LIBS})

# Compiler warnings
if(MSVC)
    target_compile_options(roj-node-c PRIVATE /W3)
else()
    target_compile_options(roj-node-c PRIVATE -Wall -Wextra -pedantic)
endif()
