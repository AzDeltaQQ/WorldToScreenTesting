cmake_minimum_required(VERSION 3.20)
cmake_policy(SET CMP0167 OLD)
cmake_policy(SET CMP0144 NEW)
project(WorldToScreenTesting)

# Force 32-bit compilation
set(CMAKE_GENERATOR_PLATFORM Win32)
set(CMAKE_SIZEOF_VOID_P 4)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# DirectX SDK paths
set(DIRECTX_SDK_DIR "C:/Program Files (x86)/Microsoft DirectX SDK (June 2010)")
set(DIRECTX_INCLUDE_DIR "${DIRECTX_SDK_DIR}/Include")
set(DIRECTX_LIB_DIR "${DIRECTX_SDK_DIR}/Lib/x86")

# -----------------------------------------------------------------------------
# vcpkg integration (header & library discovery)
# -----------------------------------------------------------------------------
# If the user has run `vcpkg integrate install` inside their vcpkg instance OR
# is configuring CMake with
#   -DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%/scripts/buildsystems/vcpkg.cmake
# the packages will be found automatically and no extra work is needed.
#
# If the toolchain file is not used we fall back to adding the triplet's
# `share` folder to CMAKE_PREFIX_PATH based on the VCPKG_ROOT env-variable so
# that find_package() works in CONFIG mode.
# -----------------------------------------------------------------------------

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE AND DEFINED ENV{VCPKG_ROOT})
    set(VCPKG_TRIPLET "x86-windows" CACHE STRING "vcpkg triplet to use")
    list(APPEND CMAKE_PREFIX_PATH "$ENV{VCPKG_ROOT}/installed/${VCPKG_TRIPLET}/share")
endif()

# --------------------------------------------------------------------------------
# Locate third-party libraries via FindBoost / FindZLIB
# (works whether vcpkg tool-chain is used or not)
# --------------------------------------------------------------------------------
# When the vcpkg tool-chain is *not* used we help CMake by pointing it at the
# triplet folder so that the stock FindBoost/FindZLIB modules can see the
# headers and libs.

if(NOT TARGET Boost::filesystem)
    if(DEFINED ENV{VCPKG_ROOT})
        set(BOOST_ROOT "$ENV{VCPKG_ROOT}/installed/${VCPKG_TRIPLET}")
        set(BOOST_INCLUDEDIR "${BOOST_ROOT}/include")
        set(BOOST_LIBRARYDIR "${BOOST_ROOT}/lib")
        list(APPEND CMAKE_PREFIX_PATH "${BOOST_ROOT}")
    endif()

    set(Boost_USE_STATIC_LIBS ON)
    set(Boost_USE_MULTITHREADED ON)
    set(Boost_USE_STATIC_RUNTIME ON)

    find_package(Boost 1.71 REQUIRED COMPONENTS filesystem system)
endif()

# Set CMAKE_MSVC_RUNTIME_LIBRARY
include(Compiler/MSVC)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# ZLIB (needed by g3dlite)
find_package(ZLIB REQUIRED)

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

# Add TrinityCore Collision Sources
set(TC_COLLISION_DIR ${CMAKE_CURRENT_SOURCE_DIR}/TrinityCore-3.3.5/src/common/Collision)
set(TC_COLLISION_SOURCES
    ${TC_COLLISION_DIR}/BoundingIntervalHierarchy.cpp
    ${TC_COLLISION_DIR}/DynamicTree.cpp
    ${TC_COLLISION_DIR}/Management/MMapFactory.cpp
    ${TC_COLLISION_DIR}/Management/MMapManager.cpp
    ${TC_COLLISION_DIR}/Management/VMapFactory.cpp
    ${TC_COLLISION_DIR}/Management/VMapManager2.cpp
    ${TC_COLLISION_DIR}/Maps/MapTree.cpp
    ${TC_COLLISION_DIR}/Maps/TileAssembler.cpp
    ${TC_COLLISION_DIR}/Models/GameObjectModel.cpp
    ${TC_COLLISION_DIR}/Models/ModelInstance.cpp
    ${TC_COLLISION_DIR}/Models/WorldModel.cpp
)

# Stub TrinityCore functions instead of linking full TrinityCore common libs

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

# Add fmt (formatting library)
set(FMT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/TrinityCore-3.3.5/dep/fmt)
set(FMT_SOURCES
    ${FMT_DIR}/src/format.cc
    ${FMT_DIR}/src/os.cc
    # ${FMT_DIR}/src/fmt.cc  # Module interface not needed; causes errors under MSVC without /modules flag
)

# Add TrinityCore g3dlite (math/geom library)
set(G3DLITE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/TrinityCore-3.3.5/dep/g3dlite)
set(G3DLITE_SOURCES
    ${G3DLITE_DIR}/source/AABox.cpp
    ${G3DLITE_DIR}/source/Any.cpp
    ${G3DLITE_DIR}/source/AnyTableReader.cpp
    ${G3DLITE_DIR}/source/BinaryFormat.cpp
    ${G3DLITE_DIR}/source/BinaryInput.cpp
    ${G3DLITE_DIR}/source/BinaryOutput.cpp
    ${G3DLITE_DIR}/source/Box.cpp
    ${G3DLITE_DIR}/source/Capsule.cpp
    ${G3DLITE_DIR}/source/CollisionDetection.cpp
    ${G3DLITE_DIR}/source/CoordinateFrame.cpp
    ${G3DLITE_DIR}/source/Crypto.cpp
    ${G3DLITE_DIR}/source/Cylinder.cpp
    ${G3DLITE_DIR}/source/debugAssert.cpp
    ${G3DLITE_DIR}/source/FileSystem.cpp
    ${G3DLITE_DIR}/source/fileutils.cpp
    ${G3DLITE_DIR}/source/format.cpp
    ${G3DLITE_DIR}/source/g3dfnmatch.cpp
    ${G3DLITE_DIR}/source/g3dmath.cpp
    ${G3DLITE_DIR}/source/GThread.cpp
    ${G3DLITE_DIR}/source/Line.cpp
    ${G3DLITE_DIR}/source/LineSegment.cpp
    ${G3DLITE_DIR}/source/Log.cpp
    ${G3DLITE_DIR}/source/Matrix3.cpp
    ${G3DLITE_DIR}/source/Matrix4.cpp
    ${G3DLITE_DIR}/source/MemoryManager.cpp
    ${G3DLITE_DIR}/source/PhysicsFrame.cpp
    ${G3DLITE_DIR}/source/Plane.cpp
    ${G3DLITE_DIR}/source/prompt.cpp
    ${G3DLITE_DIR}/source/Quat.cpp
    ${G3DLITE_DIR}/source/Random.cpp
    ${G3DLITE_DIR}/source/Ray.cpp
    ${G3DLITE_DIR}/source/RegistryUtil.cpp
    ${G3DLITE_DIR}/source/Sphere.cpp
    ${G3DLITE_DIR}/source/stringutils.cpp
    ${G3DLITE_DIR}/source/System.cpp
    ${G3DLITE_DIR}/source/TextInput.cpp
    ${G3DLITE_DIR}/source/TextOutput.cpp
    ${G3DLITE_DIR}/source/Triangle.cpp
    ${G3DLITE_DIR}/source/uint128.cpp
    ${G3DLITE_DIR}/source/UprightFrame.cpp
    ${G3DLITE_DIR}/source/Vector2.cpp
    ${G3DLITE_DIR}/source/Vector3.cpp
    ${G3DLITE_DIR}/source/Vector4.cpp
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
    core/navigation/MapHeightManager.cpp
    core/gui/tabs/NavigationTab.cpp
    core/logs/Logger.cpp
    core/TrinityStubs.cpp
    core/types/types.cpp
    ${MINHOOK_SOURCES}
    ${IMGUI_SOURCES}
    ${RECAST_SOURCES}
    ${TC_COLLISION_SOURCES}
    ${FMT_SOURCES}
    ${G3DLITE_SOURCES}
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
    core/navigation/MapHeightManager.h
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
    ${CMAKE_CURRENT_SOURCE_DIR}/TrinityCore-3.3.5/src
    ${CMAKE_CURRENT_SOURCE_DIR}/TrinityCore-3.3.5/src/common
    ${CMAKE_CURRENT_SOURCE_DIR}/TrinityCore-3.3.5/src/common/Collision
    ${CMAKE_CURRENT_SOURCE_DIR}/TrinityCore-3.3.5/src/common/Collision/Management
    ${CMAKE_CURRENT_SOURCE_DIR}/TrinityCore-3.3.5/src/common/Collision/Maps
    ${CMAKE_CURRENT_SOURCE_DIR}/TrinityCore-3.3.5/src/common/Collision/Models
    ${CMAKE_CURRENT_SOURCE_DIR}/TrinityCore-3.3.5/src/common/Utilities
    ${CMAKE_CURRENT_SOURCE_DIR}/TrinityCore-3.3.5/dep/g3dlite/include
    ${FMT_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/TrinityCore-3.3.5/src/common/Configuration
    ${CMAKE_CURRENT_SOURCE_DIR}/TrinityCore-3.3.5/src/common/Logging
    ${CMAKE_CURRENT_SOURCE_DIR}/TrinityCore-3.3.5/src/common/Time
    ${CMAKE_CURRENT_SOURCE_DIR}/TrinityCore-3.3.5/src/common/Asio
    ${CMAKE_CURRENT_SOURCE_DIR}/TrinityCore-3.3.5/src/common/Debugging
    ${CMAKE_CURRENT_SOURCE_DIR}/TrinityCore-3.3.5/src/common/Metric
    ${CMAKE_CURRENT_SOURCE_DIR}/TrinityCore-3.3.5/src/common/Threading
    ${Boost_INCLUDE_DIRS}
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
    ZLIB::ZLIB
    Boost::filesystem
    Boost::system
)

# Compiler definitions
target_compile_definitions(${PROJECT_NAME} PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    _WIN32_WINNT=0x0A00
    NOMINMAX
    WIN32
    _WIN32
    DT_POLYREF64
    _SILENCE_STDEXT_ARR_ITERS_DEPRECATION_WARNING
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