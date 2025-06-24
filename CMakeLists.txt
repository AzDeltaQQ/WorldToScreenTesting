cmake_minimum_required(VERSION 3.20)
project(WorldToScreenTesting)

# Force 32-bit compilation
set(CMAKE_GENERATOR_PLATFORM Win32)
set(CMAKE_SIZEOF_VOID_P 4)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# DirectX SDK paths
set(DIRECTX_SDK_DIR "C:/Program Files (x86)/Microsoft DirectX SDK (June 2010)")
set(DIRECTX_INCLUDE_DIR "${DIRECTX_SDK_DIR}/Include")
set(DIRECTX_LIB_DIR "${DIRECTX_SDK_DIR}/Lib/x86")

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

# Add ImGui
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

# Source files
set(SOURCES
    dllmain.cpp
    core/hook.cpp
    core/memory/memory.cpp
    core/objects/ObjectManager.cpp
    core/objects/WowObject.cpp
    core/objects/WowPlayer.cpp
    core/objects/WowUnit.cpp
    core/objects/WowGameObject.cpp
    core/gui/GUI.cpp
    core/gui/tabs/DrawingTab.cpp
    core/gui/tabs/ObjectsTab.cpp
    core/drawing/drawing.cpp
    core/drawing/components/WorldToScreenCore.cpp
    core/drawing/components/RenderEngine.cpp
    core/drawing/components/LineManager.cpp
    core/drawing/components/MarkerManager.cpp
    core/drawing/components/PlayerTracker.cpp
    core/drawing/components/ObjectOverlay.cpp
    core/logs/Logger.cpp
    core/types/types.cpp
    ${MINHOOK_SOURCES}
    ${IMGUI_SOURCES}
)

set(HEADERS
    core/hook.h
    core/types.h
    core/drawing/drawing.h
    core/drawing/components/WorldToScreenCore.h
    core/drawing/components/RenderEngine.h
    core/drawing/components/LineManager.h
    core/drawing/components/MarkerManager.h
    core/drawing/components/PlayerTracker.h
    core/logs/Logger.h
    core/memory/memory.h
    core/types/types.h
    core/objects/WowObject.h
    core/objects/WowUnit.h
    core/objects/WowPlayer.h
    core/objects/WowGameObject.h
    core/objects/ObjectManager.h
    core/gui/GUI.h
    core/gui/tabs/ObjectsTab.h
    core/gui/tabs/DrawingTab.h
)

# Create the DLL
add_library(${PROJECT_NAME} SHARED ${SOURCES} ${HEADERS})

# Include directories
target_include_directories(${PROJECT_NAME} 
    PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}/core
    ${CMAKE_CURRENT_SOURCE_DIR}/core/drawing
    ${CMAKE_CURRENT_SOURCE_DIR}/core/drawing/components
    ${CMAKE_CURRENT_SOURCE_DIR}/core/logs
    ${CMAKE_CURRENT_SOURCE_DIR}/core/memory
    ${CMAKE_CURRENT_SOURCE_DIR}/core/types
    ${CMAKE_CURRENT_SOURCE_DIR}/core/objects
    ${CMAKE_CURRENT_SOURCE_DIR}/core/gui
    ${CMAKE_CURRENT_SOURCE_DIR}/dependencies
    ${MINHOOK_DIR}/include
    ${JSON_DIR}/include
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
    ${DIRECTX_INCLUDE_DIR}
)

# Link directories
target_link_directories(${PROJECT_NAME} 
    PRIVATE 
    ${DIRECTX_LIB_DIR}
)

# Link libraries
target_link_libraries(${PROJECT_NAME} 
    PRIVATE 
    d3d9
    d3dx9
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