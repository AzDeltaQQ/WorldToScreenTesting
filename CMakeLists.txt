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

# Add Recast Navigation
set(RECAST_DIR ${CMAKE_CURRENT_SOURCE_DIR}/dependencies/recastnavigation)
set(RECAST_SOURCES
    ${RECAST_DIR}/Detour/Source/DetourAlloc.cpp
    ${RECAST_DIR}/Detour/Source/DetourCommon.cpp
    ${RECAST_DIR}/Detour/Source/DetourNavMesh.cpp
    ${RECAST_DIR}/Detour/Source/DetourNavMeshBuilder.cpp
    ${RECAST_DIR}/Detour/Source/DetourNavMeshQuery.cpp
    ${RECAST_DIR}/Detour/Source/DetourNode.cpp
    ${RECAST_DIR}/Recast/Source/Recast.cpp
    ${RECAST_DIR}/Recast/Source/RecastAlloc.cpp
    ${RECAST_DIR}/Recast/Source/RecastArea.cpp
    ${RECAST_DIR}/Recast/Source/RecastAssert.cpp
    ${RECAST_DIR}/Recast/Source/RecastContour.cpp
    ${RECAST_DIR}/Recast/Source/RecastFilter.cpp
    ${RECAST_DIR}/Recast/Source/RecastLayers.cpp
    ${RECAST_DIR}/Recast/Source/RecastMesh.cpp
    ${RECAST_DIR}/Recast/Source/RecastMeshDetail.cpp
    ${RECAST_DIR}/Recast/Source/RecastRasterization.cpp
    ${RECAST_DIR}/Recast/Source/RecastRegion.cpp
)

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
    core/gui/tabs/CombatLogTab.cpp
    core/gui/tabs/MovementTab.cpp
    core/drawing/drawing.cpp
    core/drawing/components/WorldToScreenCore.cpp
    core/drawing/components/RenderEngine.cpp
    core/drawing/components/LineManager.cpp
    core/drawing/components/MarkerManager.cpp
    core/drawing/components/PlayerTracker.cpp
    core/drawing/components/ObjectOverlay.cpp
    core/drawing/components/LineOfSightManager.cpp
    core/drawing/components/TextureManager.cpp
    core/combat/CombatLogManager.cpp
    core/combat/CombatLogAnalyzer.cpp
    core/movement/MovementController.cpp
    core/navigation/NavigationManager.cpp
    core/navigation/VMapManager.cpp
    core/gui/tabs/NavigationTab.cpp
    core/logs/Logger.cpp
    core/types/types.cpp
    ${MINHOOK_SOURCES}
    ${IMGUI_SOURCES}
    ${RECAST_SOURCES}
)

# TrinityCore VMap integration removed - using simplified VMap implementation

set(HEADERS
    core/hook.h
    core/drawing/drawing.h
    core/drawing/components/WorldToScreenCore.h
    core/drawing/components/RenderEngine.h
    core/drawing/components/LineManager.h
    core/drawing/components/MarkerManager.h
    core/drawing/components/PlayerTracker.h
    core/drawing/components/LineOfSightManager.h
    core/drawing/components/TextureManager.h
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
    core/gui/tabs/CombatLogTab.h
    core/gui/tabs/MovementTab.h
    core/gui/tabs/NavigationTab.h
    core/navigation/NavigationManager.h
    core/navigation/NavigationTypes.h
    core/navigation/VMapManager.h
    core/combat/CombatLogEntry.h
    core/combat/CombatLogManager.h
    core/combat/CombatLogAnalyzer.h
    core/movement/MovementController.h
)

# Create the DLL
add_library(${PROJECT_NAME} SHARED ${SOURCES} ${HEADERS})

# Copy BLP folder to build directory
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_CURRENT_SOURCE_DIR}/core/blps
    ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/$<CONFIG>/blps
    COMMENT "Copying BLP files to build directory"
)

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
    ${CMAKE_CURRENT_SOURCE_DIR}/core/combat
    ${CMAKE_CURRENT_SOURCE_DIR}/core/movement
    ${CMAKE_CURRENT_SOURCE_DIR}/core/navigation
    ${CMAKE_CURRENT_SOURCE_DIR}/dependencies
    ${CMAKE_CURRENT_SOURCE_DIR}/dependencies/recastnavigation/Detour/Include
    ${CMAKE_CURRENT_SOURCE_DIR}/dependencies/recastnavigation/DetourCrowd/Include
    ${CMAKE_CURRENT_SOURCE_DIR}/dependencies/recastnavigation/Recast/Include
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
    DT_POLYREF64
)

# Force 32-bit compilation flags
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "This project must be compiled as 32-bit!")
endif()

# Set 32-bit specific compiler flags
if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /arch:IA32 /FS)
    set_target_properties(${PROJECT_NAME} PROPERTIES
        LINK_FLAGS "/MACHINE:X86 /SAFESEH:NO"
    )
endif()

# Set DLL properties
set_target_properties(${PROJECT_NAME} PROPERTIES
    OUTPUT_NAME "WorldToScreenTesting"
    SUFFIX ".dll"
) 