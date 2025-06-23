cmake_minimum_required(VERSION 3.16)
project(WorldToScreenTest)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Set output directory
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Define source files
set(SOURCES
    dllmain.cpp
    core/hook.cpp
    core/worldtoscreen.cpp
)

set(HEADERS
    core/hook.h
    core/types.h
    core/worldtoscreen.h
)

# Add ImGui sources
set(IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/dependencies/ImGui)
set(IMGUI_SOURCES
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_win32.cpp
    ${IMGUI_DIR}/backends/imgui_impl_dx9.cpp
)

# Add MinHook sources
set(MINHOOK_DIR ${CMAKE_CURRENT_SOURCE_DIR}/dependencies/MinHook)
set(MINHOOK_SOURCES
    ${MINHOOK_DIR}/src/buffer.c
    ${MINHOOK_DIR}/src/hook.c
    ${MINHOOK_DIR}/src/trampoline.c
    ${MINHOOK_DIR}/src/hde/hde32.c
    ${MINHOOK_DIR}/src/hde/hde64.c
)

# Create the DLL
add_library(${PROJECT_NAME} SHARED 
    ${SOURCES} 
    ${HEADERS}
    ${IMGUI_SOURCES}
    ${MINHOOK_SOURCES}
)

# Include directories
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
    ${MINHOOK_DIR}/include
)

# Link libraries
target_link_libraries(${PROJECT_NAME} 
    d3d9
    user32
    kernel32
)

# Compiler definitions
target_compile_definitions(${PROJECT_NAME} PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _CRT_SECURE_NO_WARNINGS
)

# Set target properties
set_target_properties(${PROJECT_NAME} PROPERTIES
    SUFFIX ".dll"
    PREFIX ""
) 