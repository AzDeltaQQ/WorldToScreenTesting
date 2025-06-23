cmake_minimum_required(VERSION 3.20)
project(WorldToScreenTesting)

# Force 32-bit compilation
set(CMAKE_GENERATOR_PLATFORM Win32)
set(CMAKE_SIZEOF_VOID_P 4)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Set output directory
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Define MinHook sources manually
set(MINHOOK_DIR ${CMAKE_CURRENT_SOURCE_DIR}/dependencies/MinHook)
set(MINHOOK_SOURCES
    ${MINHOOK_DIR}/src/buffer.c
    ${MINHOOK_DIR}/src/hook.c
    ${MINHOOK_DIR}/src/trampoline.c
    ${MINHOOK_DIR}/src/hde/hde32.c
    ${MINHOOK_DIR}/src/hde/hde64.c
)

# Add nlohmann/json (header-only library)
set(JSON_DIR ${CMAKE_CURRENT_SOURCE_DIR}/dependencies/json-develop)

# Source files
set(SOURCES
    dllmain.cpp
    core/hook.cpp
    core/worldtoscreen.cpp
    core/player.cpp
    ${MINHOOK_SOURCES}
)

set(HEADERS
    core/hook.h
    core/types.h
    core/worldtoscreen.h
)

# Create the DLL
add_library(${PROJECT_NAME} SHARED ${SOURCES} ${HEADERS})

# Include directories
target_include_directories(${PROJECT_NAME} 
    PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}/core
    ${CMAKE_CURRENT_SOURCE_DIR}/dependencies
    ${MINHOOK_DIR}/include
    ${JSON_DIR}/include
)

# Link libraries
target_link_libraries(${PROJECT_NAME} 
    PRIVATE 
    d3d9
    user32
    kernel32
)

# Compiler definitions
target_compile_definitions(${PROJECT_NAME} PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    WIN32
    _WIN32
)

# Force 32-bit compilation flags
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "This project must be compiled as 32-bit!")
endif()

# Set 32-bit specific compiler flags
if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /arch:IA32)
    set_target_properties(${PROJECT_NAME} PROPERTIES
        LINK_FLAGS "/MACHINE:X86 /SAFESEH:NO"
    )
endif()

# Set DLL properties
set_target_properties(${PROJECT_NAME} PROPERTIES
    OUTPUT_NAME "WorldToScreenTesting"
    SUFFIX ".dll"
) 