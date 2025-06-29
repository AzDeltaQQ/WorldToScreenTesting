#include "NavigationManager.h"
#include "../logs/Logger.h"
#include "../objects/ObjectManager.h"
#include <filesystem>
#include <sstream>
#include <iostream>
#include <fstream>
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <Windows.h>
#include <unordered_map>
#include <set>
#include <cfloat>

#define VMAP_TILE_SIZE (533.33333f)

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Detour includes
#include "../../TrinityCore-3.3.5/dep/recastnavigation/Detour/Include/DetourNavMesh.h"
#include "../../TrinityCore-3.3.5/dep/recastnavigation/Detour/Include/DetourNavMeshQuery.h"
#include "../../TrinityCore-3.3.5/dep/recastnavigation/Detour/Include/DetourCommon.h"
#include "../../TrinityCore-3.3.5/dep/recastnavigation/Detour/Include/DetourNavMeshBuilder.h"

#define MAX_PATH_WAYPOINTS 2048

// Navigation area and flag constants from TrinityCore MapDefines.h
enum NavArea
{
    NAV_AREA_EMPTY          = 0,
    NAV_AREA_GROUND         = 11,
    NAV_AREA_GROUND_STEEP   = 10,
    NAV_AREA_WATER          = 9,
    NAV_AREA_MAGMA_SLIME    = 8,
    NAV_AREA_MAX_VALUE      = NAV_AREA_GROUND,
    NAV_AREA_MIN_VALUE      = NAV_AREA_MAGMA_SLIME
};

enum NavTerrainFlag
{
    NAV_EMPTY        = 0x00,
    NAV_GROUND       = 1 << (NAV_AREA_MAX_VALUE - NAV_AREA_GROUND),       // 1
    NAV_GROUND_STEEP = 1 << (NAV_AREA_MAX_VALUE - NAV_AREA_GROUND_STEEP), // 2
    NAV_WATER        = 1 << (NAV_AREA_MAX_VALUE - NAV_AREA_WATER),        // 4
    NAV_MAGMA_SLIME  = 1 << (NAV_AREA_MAX_VALUE - NAV_AREA_MAGMA_SLIME)   // 8
};

// Required for Detour assertions
extern "C" void dtAssertFail(const char* msg, const char* file, int line) {
    std::ostringstream oss;
    oss << "Detour assertion failed: " << msg << " at " << file << ":" << line;
    LOG_ERROR(oss.str());
}

// Provide the accessor with C++ linkage signature expected by Detour static lib
using dtAssertFailFunc = void(*)(const char*, const char*, int);

dtAssertFailFunc dtAssertFailGetCustom() {
    return &dtAssertFail;
}

//------------------------------------------------------------------------------
// MMap file header & constants (taken from TrinityCore/MaNGOS reference)
//------------------------------------------------------------------------------
#define MMAP_MAGIC 0x4d4d4150   // 'MMAP'
#define MMAP_VERSION 15

struct MmapTileHeader
{
    uint32_t mmapMagic;     // MMAP_MAGIC
    uint32_t dtVersion;     // Detour version, compare with DT_NAVMESH_VERSION
    uint32_t mmapVersion;   // Expected MMAP_VERSION
    uint32_t size;          // Size of tile data in bytes
    unsigned char usesLiquids; // Whether liquid polygons were included (1 byte)
    unsigned char padding[3];  // Padding to match TrinityCore (total struct size 20 bytes)

    MmapTileHeader()
        : mmapMagic(MMAP_MAGIC), dtVersion(DT_NAVMESH_VERSION), mmapVersion(MMAP_VERSION), size(0), usesLiquids(0), padding{0,0,0} {}
};

// Helper function to find maps directory
std::string FindMapsDirectory(HMODULE hModule) {
    // Get the current module directory
    char dllPath[MAX_PATH];
    
    if (GetModuleFileNameA(hModule, dllPath, MAX_PATH)) {
        std::string dllDir = std::string(dllPath);
        size_t lastSlash = dllDir.find_last_of("\\/");
        if (lastSlash != std::string::npos) {
            dllDir = dllDir.substr(0, lastSlash);
        }
        
        // Try different relative paths
        std::vector<std::string> possiblePaths = {
            dllDir + "/../../../core/maps/mmaps/",
            dllDir + "/../../core/maps/mmaps/",
            dllDir + "/../core/maps/mmaps/",
            dllDir + "/core/maps/mmaps/",
            dllDir + "/maps/mmaps/",
            dllDir + "/mmaps/"
        };
        
        for (const auto& path : possiblePaths) {
            if (std::filesystem::exists(path)) {
                std::string canonicalPath = std::filesystem::canonical(path).string();
                std::replace(canonicalPath.begin(), canonicalPath.end(), '\\', '/');
                LOG_INFO("Found maps directory: " + canonicalPath);
                
                // List available map files
                try {
                    std::vector<std::string> mapFiles;
                    for (const auto& entry : std::filesystem::directory_iterator(path)) {
                        if (entry.is_regular_file()) {
                            std::string filename = entry.path().filename().string();
                            if (filename.size() >= 5 && filename.substr(filename.size() - 5) == ".mmap") {
                                mapFiles.push_back(filename);
                            }
                        }
                    }
                    
                    if (!mapFiles.empty()) {
                        std::sort(mapFiles.begin(), mapFiles.end());
                        std::ostringstream oss;
                        oss << "Available map files: ";
                        for (size_t i = 0; i < mapFiles.size(); ++i) {
                            if (i > 0) oss << ", ";
                            oss << mapFiles[i];
                        }
                        LOG_INFO(oss.str());
                    }
                } catch (const std::exception& e) {
                    LOG_WARNING("Could not list map files: " + std::string(e.what()));
                }
                
                return canonicalPath;
            }
        }
    }
    
    LOG_ERROR("Could not find maps directory");
    return "";
}

namespace Navigation {

// Forward declarations for static utility functions
static void AnalyzeNavMeshTiles(const dtNavMesh* navMesh);

HMODULE NavigationManager::s_hModule = NULL;

void NavigationManager::SetModuleHandle(HMODULE hModule) {
    s_hModule = hModule;
}

// MapData destructor implementation
MapData::~MapData() {
    if (navMesh) {
        dtFreeNavMesh(navMesh);
        navMesh = nullptr;
    }
}

// MapData move constructor
MapData::MapData(MapData&& other) noexcept 
    : navMesh(other.navMesh), mapId(other.mapId), loadedTiles(std::move(other.loadedTiles)) {
    other.navMesh = nullptr;
    other.mapId = -1;
}

// MapData move assignment
MapData& MapData::operator=(MapData&& other) noexcept {
    if (this != &other) {
        if (navMesh) {
            dtFreeNavMesh(navMesh);
        }
        navMesh = other.navMesh;
        mapId = other.mapId;
        loadedTiles = std::move(other.loadedTiles);
        
        other.navMesh = nullptr;
        other.mapId = -1;
    }
    return *this;
}

NavigationManager& NavigationManager::Instance() {
    static NavigationManager instance;
    return instance;
}

NavigationManager::NavigationManager() 
    : m_navMesh(nullptr), m_navMeshQuery(nullptr) {
    // VMapManager will be created later in Initialize(), once we know the correct maps directory.
}

NavigationManager::~NavigationManager() {
    Shutdown();
}

bool NavigationManager::Initialize() {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (m_initialized) {
        return true;
    }

    LOG_INFO("Initializing NavigationManager...");

    // Find maps directory
    m_mapsDirectory = ::FindMapsDirectory(s_hModule);
    if (m_mapsDirectory.empty()) {
        LOG_ERROR("Failed to find maps directory");
        return false;
    }

    // Initialize Detour components
    m_navMeshQuery = dtAllocNavMeshQuery();
    if (!m_navMeshQuery) {
        LOG_ERROR("Failed to allocate NavMeshQuery");
        return false;
    }

    m_filter = new dtQueryFilter();
    if (!m_filter) {
        LOG_ERROR("Failed to allocate QueryFilter");
        dtFreeNavMeshQuery(m_navMeshQuery);
        return false;
    }

    // By default restrict queries to ordinary ground polygons. We will fall back to steep if needed.
    unsigned short includeFlags = NAV_GROUND;
    unsigned short excludeFlags = NAV_GROUND_STEEP | NAV_WATER | NAV_MAGMA_SLIME;

    m_filter->setIncludeFlags(includeFlags);
    m_filter->setExcludeFlags(excludeFlags);

    // AUTOMATIC AREA COSTS based on terrain analysis
    float steepCost = 25.0f;  // Heavy-ish penalty for steep terrain
    float waterCost = 5.0f;   // Light penalty for water when allowed
    float magmaCost = 100.0f; // Heavy penalty for lava when allowed

    m_filter->setAreaCost(NAV_AREA_GROUND,        1.0f);      // Normal ground - preferred
    m_filter->setAreaCost(NAV_AREA_GROUND_STEEP,  steepCost); // Steep terrain - automatically avoid hills
    m_filter->setAreaCost(NAV_AREA_WATER,         waterCost); // Water - slight penalty
    m_filter->setAreaCost(NAV_AREA_MAGMA_SLIME,   magmaCost); // Lava - heavy penalty

    LOG_INFO("Automatic terrain-aware filter configured - Steep terrain cost: 25x, Water cost: 5x");

    // Initialize VMap collision detection for nav-mesh enhancement
    m_vmapManager = std::make_unique<VMapManager>();
    if (!m_vmapManager->Initialize(m_mapsDirectory)) {
        LOG_WARNING("VMap initialization failed - collision detection will be disabled");
        // Don't fail the entire initialization, just disable VMap features
    } else {
        LOG_INFO("VMap collision detection initialized successfully");
    }

    m_initialized = true;
    LOG_INFO("NavigationManager initialized successfully with automatic obstacle avoidance");
    return true;
}

void NavigationManager::Shutdown() {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (!m_initialized) {
        return;
    }

    LOG_INFO("Shutting down NavigationManager...");

    // Clean up loaded maps (MapData destructor handles nav mesh cleanup)
    m_loadedMaps.clear();

    // Clean up VMap system
    if (m_vmapManager) {
        m_vmapManager->Shutdown();
        m_vmapManager.reset();
    }

    // Clean up Detour components
    if (m_navMeshQuery) {
        dtFreeNavMeshQuery(m_navMeshQuery);
        m_navMeshQuery = nullptr;
    }

    if (m_filter) {
        delete m_filter;
        m_filter = nullptr;
    }

    m_initialized = false;
    LOG_INFO("NavigationManager shut down");

    // Prevent dangling pointer to freed navmesh
    m_navMesh = nullptr;
}

bool NavigationManager::IsMapLoaded(uint32_t mapId) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_loadedMaps.find(static_cast<int>(mapId)) != m_loadedMaps.end();
}

void NavigationManager::UnloadMap(uint32_t mapId) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    auto it = m_loadedMaps.find(static_cast<int>(mapId));
    if (it != m_loadedMaps.end()) {
        LOG_INFO("Unloading map " + std::to_string(mapId));
        m_loadedMaps.erase(it);
        // If this was the active navmesh, clear the pointer to avoid dangling reference
        m_navMesh = nullptr;
    }
}

void NavigationManager::UnloadAllMaps() {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (!m_loadedMaps.empty()) {
        LOG_INFO("Unloading all " + std::to_string(m_loadedMaps.size()) + " loaded maps");
        m_loadedMaps.clear();
        // Prevent dangling pointer to freed navmesh
        m_navMesh = nullptr;
    }
}

uint32_t NavigationManager::GetCurrentMapId() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_loadedMaps.empty()) {
        return static_cast<uint32_t>(m_loadedMaps.begin()->first);
    }
    return 0;
}

std::vector<MapTile> NavigationManager::GetLoadedTiles() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<MapTile> tiles;
    
    for (const auto& mapPair : m_loadedMaps) {
        for (const auto& tilePair : mapPair.second.loadedTiles) {
            MapTile tile;
            tile.mapId = static_cast<uint32_t>(mapPair.first);
            tile.tileX = (tilePair.first >> 16) & 0xFFFF;
            tile.tileY = tilePair.first & 0xFFFF;
            tile.loaded = true;
            tiles.push_back(tile);
        }
    }
    
    return tiles;
}

bool NavigationManager::LoadMapNavMesh(int mapId) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    // Check if already loaded
    if (m_loadedMaps.find(mapId) != m_loadedMaps.end()) {
        LOG_INFO("Navigation mesh for map " + std::to_string(mapId) + " already loaded");
        return true;
    }
    
    // Auto-unload any previously loaded maps to ensure only one map is loaded at a time
    if (!m_loadedMaps.empty()) {
        LOG_INFO("Auto-unloading " + std::to_string(m_loadedMaps.size()) + " previously loaded maps before loading map " + std::to_string(mapId));
        m_loadedMaps.clear();
    }

    // Build the mmap file path using std::filesystem for robustness
    std::filesystem::path mmapPath(m_mapsDirectory);
    std::ostringstream filename;
    filename << std::setfill('0') << std::setw(3) << mapId << ".mmap";
    mmapPath /= filename.str();

    // Check if file exists
    std::ifstream file(mmapPath, std::ios::binary);
    if (!file.is_open()) {
        LOG_ERROR("[NavigationTab] Failed to open MMap file: " + mmapPath.string());
        return false;
    }

    // TrinityCore .mmap files contain ONLY a dtNavMeshParams struct.
    // Read dtNavMeshParams directly from the beginning of the file.
    dtNavMeshParams params;
    file.read(reinterpret_cast<char*>(&params), sizeof(dtNavMeshParams));
    if (file.gcount() != sizeof(dtNavMeshParams)) {
        LOG_ERROR("[NavigationTab] Failed to read dtNavMeshParams from: " + mmapPath.string());
        return false;
    }

    LOG_INFO("NavMeshParams for map " + std::to_string(mapId) + ":");
    LOG_INFO("  orig: (" + std::to_string(params.orig[0]) + ", " + std::to_string(params.orig[1]) + ", " + std::to_string(params.orig[2]) + ")");
    LOG_INFO("  tileWidth: " + std::to_string(params.tileWidth));
    LOG_INFO("  tileHeight: " + std::to_string(params.tileHeight));
    LOG_INFO("  maxTiles: " + std::to_string(params.maxTiles));
    LOG_INFO("  maxPolys: " + std::to_string(params.maxPolys));

    file.close();

    MapData mapData;

    // Create and initialize nav mesh
    dtNavMesh* navMesh = dtAllocNavMesh();
    if (!navMesh) {
        LOG_ERROR("Failed to allocate NavMesh");
        return false;
    }

    if (dtStatusFailed(navMesh->init(&params))) {
        LOG_ERROR("Failed to initialize NavMesh with params");
        dtFreeNavMesh(navMesh);
        return false;
    }

    // Store navmesh origin for coordinate conversion
    m_navOrigin = Vector3(params.orig[0], params.orig[1], params.orig[2]);

    // Load nav mesh tiles automatically
    int tilesLoaded = 0;
    int tilesWithPolygons = 0;
    int totalPolygons = 0;
    int totalVertices = 0;

    // Read all tiles from the directory
    std::filesystem::path tilesDir(m_mapsDirectory);
    std::ostringstream tilePattern;
    tilePattern << std::setfill('0') << std::setw(3) << mapId;
    std::string mapPrefix = tilePattern.str();

    try {
        for (const auto& entry : std::filesystem::directory_iterator(tilesDir)) {
            if (!entry.is_regular_file()) continue;
            
            std::string filename = entry.path().filename().string();
            if (filename.length() < 12 || filename.substr(filename.length() - 7) != ".mmtile") continue;
            if (filename.substr(0, 3) != mapPrefix) continue;

            // Extract tile coordinates from filename (format: 000XXYY.mmtile)
            int tileY = std::stoi(filename.substr(3, 2));
            int tileX = std::stoi(filename.substr(5, 2));

            std::ifstream tileFile(entry.path(), std::ios::binary);
            if (!tileFile.is_open()) continue;

            // Read tile header
            MmapTileHeader header;
            tileFile.read(reinterpret_cast<char*>(&header), sizeof(MmapTileHeader));
            if (tileFile.gcount() != sizeof(MmapTileHeader)) {
                tileFile.close();
                continue;
            }

            // Validate header
            if (header.mmapMagic != MMAP_MAGIC || header.dtVersion != DT_NAVMESH_VERSION || header.mmapVersion != MMAP_VERSION) {
                LOG_WARNING("Invalid tile header in file: " + filename);
                tileFile.close();
                continue;
            }

            // Read tile data
            std::vector<unsigned char> tileData(header.size);
            tileFile.read(reinterpret_cast<char*>(tileData.data()), header.size);
            if (static_cast<uint32_t>(tileFile.gcount()) != header.size) {
                LOG_WARNING("Failed to read complete tile data from: " + filename);
                tileFile.close();
                continue;
            }
            tileFile.close();

            // Add tile to nav mesh
            dtTileRef tileRef = 0;
            unsigned char* data = new unsigned char[header.size];
            std::memcpy(data, tileData.data(), header.size);
            
            dtStatus status = navMesh->addTile(data, header.size, DT_TILE_FREE_DATA, 0, &tileRef);
            if (dtStatusFailed(status) || !tileRef) {
                LOG_WARNING("Failed to add tile to navmesh: " + filename);
                delete[] data;
                continue;
            }

            // AUTOMATICALLY LOAD CORRESPONDING VMAP TILES
            if (m_vmapManager && m_vmapManager->IsLoaded()) {
                m_vmapManager->LoadMapTile(static_cast<uint32_t>(mapId), static_cast<uint32_t>(tileX), static_cast<uint32_t>(tileY));
            }

            // Track loaded tile and gather statistics
            uint32_t tileId = (static_cast<uint32_t>(tileX) << 16) | static_cast<uint32_t>(tileY);
            
            // Count polygons and vertices in this tile
            const dtMeshTile* tile = navMesh->getTileByRef(tileRef);
            if (tile && tile->header) {
                if (tile->header->polyCount > 0) {
                    tilesWithPolygons++;
                    totalPolygons += tile->header->polyCount;
                    totalVertices += tile->header->vertCount;
                }
            }

            tilesLoaded++;

            // Track tile in current map data
            mapData.loadedTiles[tileId] = tileRef;
        }
    } catch (const std::filesystem::filesystem_error& e) {
        LOG_ERROR("Filesystem error while loading tiles: " + std::string(e.what()));
    }

    if (tilesLoaded == 0) {
        LOG_ERROR("No tiles loaded for map " + std::to_string(mapId));
        dtFreeNavMesh(navMesh);
        return false;
    }

    LOG_INFO("Loaded " + std::to_string(tilesLoaded) + " tiles for map " + std::to_string(mapId));
    LOG_INFO("  Total loaded tiles with polygons: " + std::to_string(tilesWithPolygons));
    LOG_INFO("  Total polygons: " + std::to_string(totalPolygons));
    LOG_INFO("  Total vertices: " + std::to_string(totalVertices));

    // Initialize query
    if (dtStatusFailed(m_navMeshQuery->init(navMesh, 2048))) {
        LOG_ERROR("Failed to initialize NavMeshQuery");
        dtFreeNavMesh(navMesh);
        return false;
    }

    // >>> Set the active navmesh pointer so other methods can access it <<<
    m_navMesh = navMesh;

    // Store the loaded map data
    mapData.navMesh = navMesh;
    mapData.mapId = mapId;
    m_loadedMaps[mapId] = std::move(mapData);

    AnalyzeNavMeshTiles(navMesh);

    LOG_INFO("Successfully loaded navigation mesh for map " + std::to_string(mapId));
    return true;
}

bool NavigationManager::LoadMapTile(int mapId, int tileX, int tileY) {
    // TODO: Implement tile loading
    return false;
}

PathResult NavigationManager::FindPath(const Vector3& start, const Vector3& end,
                                     NavigationPath& path, const PathfindingOptions& options) {
    if (!m_initialized || !m_navMeshQuery || m_loadedMaps.empty()) {
        LOG_ERROR("NavigationManager not properly initialized or no maps loaded");
        path.result = PathResult::FAILED_NO_NAVMESH;
        return PathResult::FAILED_NO_NAVMESH;
    }

    // AUTOMATIC FILTER CONFIGURATION based on terrain preferences
    dtQueryFilter* filter = CreateCustomFilter(options);
    if (!filter) {
        LOG_ERROR("Failed to create query filter");
        path.result = PathResult::FAILED_INVALID_INPUT;
        return PathResult::FAILED_INVALID_INPUT;
    }

    // Convert coordinates to Recast space
    Vector3 startRecast = WowToRecast(start);
    Vector3 endRecast = WowToRecast(end);

    LOG_DEBUG("WowToRecast conversion:");
    LOG_DEBUG("  Start WoW: (" + std::to_string(start.x) + ", " + std::to_string(start.y) + ", " + std::to_string(start.z) + ")");
    LOG_DEBUG("  Start Recast: (" + std::to_string(startRecast.x) + ", " + std::to_string(startRecast.y) + ", " + std::to_string(startRecast.z) + ")");
    LOG_DEBUG("  End WoW: (" + std::to_string(end.x) + ", " + std::to_string(end.y) + ", " + std::to_string(end.z) + ")");
    LOG_DEBUG("  End Recast: (" + std::to_string(endRecast.x) + ", " + std::to_string(endRecast.y) + ", " + std::to_string(endRecast.z) + ")");

    // Find start and end polygons with automatic fallback
    float extents[3] = {4.0f, 8.0f, 4.0f}; // Standard search extents
    dtPolyRef startRef, endRef;
    float startNearest[3], endNearest[3];

    // Find start polygon (multi-pass with fallbacks)
    dtStatus startStatus = DT_FAILURE;
    // attempt 1: normal extents + default filter
    startStatus = m_navMeshQuery->findNearestPoly(&startRecast.x, extents, filter, &startRef, startNearest);

    // attempt 2: larger extents if first failed
    if (dtStatusFailed(startStatus) || startRef == 0) {
        float bigExtents[3] = { 32.0f, 32.0f, 32.0f };
        startStatus = m_navMeshQuery->findNearestPoly(&startRecast.x, bigExtents, filter, &startRef, startNearest);
    }

    // attempt 3: permissive filter if still failed
    if (dtStatusFailed(startStatus) || startRef == 0) {
        dtQueryFilter permissiveFilter;
        permissiveFilter.setIncludeFlags(NAV_GROUND | NAV_GROUND_STEEP | NAV_WATER);
        permissiveFilter.setExcludeFlags(NAV_MAGMA_SLIME);
        float bigExtents[3] = { 32.0f, 32.0f, 32.0f };
        startStatus = m_navMeshQuery->findNearestPoly(&startRecast.x, bigExtents, &permissiveFilter, &startRef, startNearest);
    }

    if (dtStatusFailed(startStatus) || startRef == 0) {
        std::stringstream ss; ss << std::fixed << std::setprecision(2);
        ss << "Failed to find start polygon after 3 attempts at WoW(" << start.x << ", " << start.y << ", " << start.z
           << ") Recast(" << startRecast.x << ", " << startRecast.y << ", " << startRecast.z << ")";
        LOG_ERROR(ss.str());
        delete filter;
        path.result = PathResult::FAILED_START_POLY;
        return PathResult::FAILED_START_POLY;
    }

    // Verify the nearest polygon is reasonably close to requested start position
    {
        float dx = startRecast.x - startNearest[0];
        float dz = startRecast.z - startNearest[2];
        float dist2 = dx*dx + dz*dz;
        const float MAX_ACCEPTABLE_DIST2 = 200.0f * 200.0f; // 200 yards squared
        if (dist2 > MAX_ACCEPTABLE_DIST2) {
            std::stringstream ss; ss << std::fixed << std::setprecision(2);
            ss << "Nearest start polygon too far away (" << std::sqrt(dist2) << " yd) at WoW(" << start.x << ", " << start.y << ", " << start.z
               << ") Recast(" << startNearest[0] << ", " << startNearest[1] << ", " << startNearest[2] << ")";
            LOG_ERROR(ss.str());
            delete filter;
            path.result = PathResult::FAILED_START_POLY;
            return PathResult::FAILED_START_POLY;
        }
    }

    // Find end polygon
    dtStatus endStatus = DT_FAILURE;
    endStatus = m_navMeshQuery->findNearestPoly(&endRecast.x, extents, filter, &endRef, endNearest);

    if (dtStatusFailed(endStatus) || endRef == 0) {
        float bigExtents[3] = { 32.0f, 32.0f, 32.0f };
        endStatus = m_navMeshQuery->findNearestPoly(&endRecast.x, bigExtents, filter, &endRef, endNearest);
    }

    if (dtStatusFailed(endStatus) || endRef == 0) {
        dtQueryFilter permissiveFilter;
        permissiveFilter.setIncludeFlags(NAV_GROUND | NAV_GROUND_STEEP | NAV_WATER);
        permissiveFilter.setExcludeFlags(NAV_MAGMA_SLIME);
        float bigExtents[3] = { 32.0f, 32.0f, 32.0f };
        endStatus = m_navMeshQuery->findNearestPoly(&endRecast.x, bigExtents, &permissiveFilter, &endRef, endNearest);
    }

    if (dtStatusFailed(endStatus) || endRef == 0) {
        std::stringstream ss; ss << std::fixed << std::setprecision(2);
        ss << "Failed to find end polygon after 3 attempts at WoW(" << end.x << ", " << end.y << ", " << end.z
           << ") Recast(" << endRecast.x << ", " << endRecast.y << ", " << endRecast.z << ")";
        LOG_ERROR(ss.str());
        delete filter;
        path.result = PathResult::FAILED_END_POLY;
        return PathResult::FAILED_END_POLY;
    }

    // Verify end polygon distance as well
    {
        float dx = endRecast.x - endNearest[0];
        float dz = endRecast.z - endNearest[2];
        float dist2 = dx*dx + dz*dz;
        const float MAX_ACCEPTABLE_DIST2 = 200.0f * 200.0f;
        if (dist2 > MAX_ACCEPTABLE_DIST2) {
            std::stringstream ss; ss << std::fixed << std::setprecision(2);
            ss << "Nearest end polygon too far away (" << std::sqrt(dist2) << " yd) at WoW(" << end.x << ", " << end.y << ", " << end.z
               << ") Recast(" << endNearest[0] << ", " << endNearest[1] << ", " << endNearest[2] << ")";
            LOG_ERROR(ss.str());
            delete filter;
            path.result = PathResult::FAILED_END_POLY;
            return PathResult::FAILED_END_POLY;
        }
    }

    // AUTOMATIC PATH GENERATION - let Detour handle all obstacle avoidance
    const int MAX_POLYS = 512;
    std::vector<dtPolyRef> polyPath(MAX_POLYS);
    int polyCount = 0;

    dtStatus pathStatus = m_navMeshQuery->findPath(startRef, endRef, startNearest, endNearest, filter, polyPath.data(), &polyCount, MAX_POLYS);
    if (dtStatusFailed(pathStatus) || polyCount == 0) {
        LOG_ERROR("Failed to find polygon path");
        delete filter;
        path.result = PathResult::FAILED_PATHFIND;
        return PathResult::FAILED_PATHFIND;
    }

    // AUTOMATIC WAYPOINT GENERATION with optimal segment length
    const int maxStraight = 512;
    std::vector<unsigned char> straightFlags(maxStraight);
    std::vector<dtPolyRef> straightPolys(maxStraight);
    std::vector<float> straightPath(maxStraight * 3);
    int straightCount = 0;

    // Generate straight path with ALL_CROSSINGS for maximum waypoints to avoid obstacles
    dtStatus straightStatus = m_navMeshQuery->findStraightPath(
        startNearest, endNearest, polyPath.data(), polyCount,
        straightPath.data(), straightFlags.data(), straightPolys.data(), &straightCount, maxStraight,
        DT_STRAIGHTPATH_ALL_CROSSINGS | DT_STRAIGHTPATH_AREA_CROSSINGS
    );

    if (dtStatusFailed(straightStatus) || straightCount == 0) {
        LOG_ERROR("Failed to generate straight path");
        delete filter;
        path.result = PathResult::FAILED_PATHFIND;
        return PathResult::FAILED_PATHFIND;
    }

    // AUTOMATIC SEGMENT LENGTH OPTIMIZATION with VMap collision detection
    std::vector<Waypoint> finalWaypoints;
    const float MAX_SEGMENT_LENGTH = 25.0f; // Increase to 25 yards to reduce subdivision
    
    // Variables for path extension
    bool hasExtension = false;
    std::vector<Vector3> storedExtensionWaypoints;
    
    // Enhanced pathfinding: Combine nav-mesh with VMap collision detection
    Vector3 lastPos = start;
    
    for (int i = 0; i < straightCount; ++i) {
        float* v = &straightPath[i * 3];
        Vector3 recastPos(v[0], v[1], v[2]);
        Vector3 wowPos = RecastToWow(recastPos);

        // Debug the first few waypoints
        if (i < 3) {
            LOG_DEBUG("Waypoint " + std::to_string(i) + " conversion:");
            LOG_DEBUG("  Recast: (" + std::to_string(recastPos.x) + ", " + std::to_string(recastPos.y) + ", " + std::to_string(recastPos.z) + ")");
            LOG_DEBUG("  WoW: (" + std::to_string(wowPos.x) + ", " + std::to_string(wowPos.y) + ", " + std::to_string(wowPos.z) + ")");
        }

        // Add waypoint with automatic segment subdivision
        if (finalWaypoints.empty()) {
            finalWaypoints.emplace_back(AdjustToSurface(wowPos, options.mapId));
        } else {
            Vector3 lastPos = finalWaypoints.back().position;
            float segmentLength = lastPos.Distance(wowPos);
            
            // AUTOMATIC SEGMENT SUBDIVISION
            if (segmentLength > MAX_SEGMENT_LENGTH) {
                int subdivisions = static_cast<int>(std::ceil(segmentLength / MAX_SEGMENT_LENGTH));
                for (int j = 1; j < subdivisions; ++j) {
                    float t = static_cast<float>(j) / subdivisions;
                    Vector3 interpPos = lastPos + (wowPos - lastPos) * t;
                    finalWaypoints.emplace_back(AdjustToSurface(interpPos, options.mapId));
                }
            }
            
            finalWaypoints.emplace_back(AdjustToSurface(wowPos, options.mapId));
        }
        
        lastPos = wowPos;
    }

    // NEW: Pre-load all VMap tiles required for the full path before doing validation.
    // This prevents inconsistent obstacle avoidance on long paths.
    if (m_vmapManager && m_vmapManager->IsLoaded()) {
        PreloadVMapTilesForPath(finalWaypoints, options.mapId);
    }

    // Check if we need path extension to reach the actual target
    if (finalWaypoints.size() > 0) {
        Vector3 pathEnd = finalWaypoints.back().position;
        float distanceToTarget = pathEnd.Distance(end);
        
        if (distanceToTarget > 5.0f) {
            bool canExtend = !m_vmapManager || m_vmapManager->IsInLineOfSight(pathEnd, end, options.mapId);
            if (canExtend) {
                finalWaypoints.emplace_back(AdjustToSurface(end, options.mapId));
                hasExtension = true;
            }
        }
    }

    // ------------------------------------------------------------------
    // SECOND-PASS SUBDIVISION – ensure no remaining segment is longer than the
    // horizontal threshold (ignore Z so we don't split purely vertical drops).
    // ------------------------------------------------------------------
    const float MAX_HORIZ_SEG = 15.0f; // yards in X-Y plane

    for (size_t i = 1; i < finalWaypoints.size(); ++i) {
        const Vector3& a = finalWaypoints[i-1].position;
        const Vector3& b = finalWaypoints[i].position;

        float horiz = std::sqrt((a.x-b.x)*(a.x-b.x) + (a.y-b.y)*(a.y-b.y));
        if (horiz <= MAX_HORIZ_SEG)
            continue;

        int cuts = static_cast<int>(std::ceil(horiz / MAX_HORIZ_SEG));
        float step = 1.0f / cuts;

        std::vector<Waypoint> inserts;
        inserts.reserve(cuts-1);
        for (int c = 1; c < cuts; ++c) {
            float t = step * c;
            Vector3 p = a + (b - a) * t;
            inserts.emplace_back(AdjustToSurface(p, options.mapId));
        }

        finalWaypoints.insert(finalWaypoints.begin() + i, inserts.begin(), inserts.end());
        i += inserts.size();
    }

    // ------------------------------------------------------------------
    // THIRD PASS – obstacle validation using navmesh ray-casts and VMap LoS.
    // Insert detour points when a segment is blocked.
    // ------------------------------------------------------------------
    {
        const int MAX_ITERS = 4;   // safety – full scan up to 4 times
        int iter = 0;
        while (iter++ < MAX_ITERS) {
            bool anyBlocked = false;
            for (size_t i = 1; i < finalWaypoints.size(); ++i) {
                const Vector3& a = finalWaypoints[i-1].position;
                const Vector3& b = finalWaypoints[i].position;

                bool blocked = SegmentHitsObstacle(a,b,options.mapId);
                if (!blocked && m_vmapManager) {
                    blocked = !m_vmapManager->IsInLineOfSight(a,b,options.mapId);
                }
                if (!blocked)
                    continue;

                Vector3 safe = AdjustToSurface(FindSafePosition(a,b,options.mapId), options.mapId);
                // avoid duplicates
                if (safe.Distance(a) < 0.1f || safe.Distance(b) < 0.1f) {
                    continue; // cannot find better, skip to next
                }
                finalWaypoints.insert(finalWaypoints.begin()+i, Waypoint(safe));
                anyBlocked = true;
                break; // restart scanning from beginning after insertion
            }
            if (!anyBlocked) break; // path clean
        }
    }

    delete filter;

    // NO MANUAL PATH VALIDATION - trust the nav-mesh and automatic obstacle avoidance
    path.waypoints = std::move(finalWaypoints);
    path.result = PathResult::SUCCESS;
    path.isComplete = true;
    path.movementType = options.movementType;

    // Calculate total path length
    path.totalLength = 0.0f;
    for (size_t i = 1; i < path.waypoints.size(); ++i) {
        path.totalLength += path.waypoints[i-1].position.Distance(path.waypoints[i].position);
    }

    // AUTOMATIC PATH ENHANCEMENT - no manual controls needed
    if (options.smoothPath) {
        HumanizePath(path, options);
    }

    // Remove duplicate waypoints automatically
    auto it = std::unique(path.waypoints.begin(), path.waypoints.end(), 
        [](const Waypoint& a, const Waypoint& b) {
            return a.position.Distance(b.position) < 0.1f; // Less than 0.1 yard difference
        });
    path.waypoints.erase(it, path.waypoints.end());

    {
        std::stringstream ss;
        ss << "Successfully found path with " << path.waypoints.size() << " waypoints. Total length: " << std::fixed << std::setprecision(2) << path.totalLength << " yards";
        LOG_INFO(ss.str());

        // Detailed debug of each waypoint (first 50 to avoid spam)
        for (size_t i = 0; i < path.waypoints.size() && i < 50; ++i) {
            const Vector3& wp = path.waypoints[i].position;
            std::stringstream sw;
            sw << "  WP[" << i << "]: (" << std::fixed << std::setprecision(2) << wp.x << ", " << wp.y << ", " << wp.z << ")";
            LOG_DEBUG(sw.str());
        }
    }
    return PathResult::SUCCESS;
}

bool NavigationManager::IsPositionValid(const Vector3& position, uint32_t mapId) {
    // TODO: Implement position validation
    return false;
}

Vector3 NavigationManager::GetClosestPoint(const Vector3& position, uint32_t mapId) {
    // TODO: Implement closest point calculation
    return position;
}

Vector3 NavigationManager::WowToRecast(const Vector3& wowPos) const
{
    // TrinityCore conversion: WoW(x,y,z) -> Recast(y,z,x) 
    // NO origin offset - Detour handles this internally!
    return Vector3(wowPos.y, wowPos.z, wowPos.x);
}

Vector3 NavigationManager::RecastToWow(const Vector3& recastPos) const
{
    // TrinityCore reverse conversion: Recast(y,z,x) -> WoW(z,x,y)
    // NO origin offset - Detour provides absolute coordinates!
    return Vector3(recastPos.z, recastPos.x, recastPos.y);
}

Vector3 NavigationManager::AdjustToSurface(const Vector3& wowPos, uint32_t mapId) const {
    uint32_t effectiveMapId = mapId;
    if (effectiveMapId == 0) {
        effectiveMapId = GetCurrentMapId();
    }

    if (!m_navMeshQuery)
        return wowPos;

    // Project the given WoW position onto the nav-mesh.
    Vector3 rcPos = WowToRecast(wowPos);
    float ext[3] = {4.0f, 8.0f, 4.0f};
    dtPolyRef ref = 0;
    float nearest[3];

    if (dtStatusFailed(m_navMeshQuery->findNearestPoly(&rcPos.x, ext, m_filter, &ref, nearest)) || ref == 0)
        return wowPos; // Could not project – return original.

    Vector3 worldNearest = RecastToWow(Vector3(nearest[0], nearest[1], nearest[2]));
    
    // Now, use VMap to get the final ground height to avoid going under terrain
    if (m_vmapManager) {
        float groundZ = m_vmapManager->GetGroundHeight(worldNearest, effectiveMapId);
        LOG_DEBUG("VMap ground height check:");
        LOG_DEBUG("  Input WoW pos: (" + std::to_string(worldNearest.x) + ", " + std::to_string(worldNearest.y) + ", " + std::to_string(worldNearest.z) + ")");
        LOG_DEBUG("  VMap ground Z: " + std::to_string(groundZ));
        
        if (groundZ > -FLT_MAX) { // Valid ground height found
            // Use VMap ground height if it's reasonable (within 10 units of nav mesh height)
            if (std::abs(groundZ - worldNearest.z) < 10.0f) {
                worldNearest.z = groundZ + 0.5f;
                LOG_DEBUG("  Using VMap ground height: " + std::to_string(worldNearest.z));
            } else {
                LOG_DEBUG("  VMap height too different from nav mesh, using nav mesh height");
            }
        } else {
            LOG_DEBUG("  VMap returned invalid height, using nav mesh height");
        }
    }

    return worldNearest;
}

Vector3 NavigationManager::EnsureAboveGround(const Vector3& pos, uint32_t mapId) const {
    uint32_t effectiveMapId = mapId;
    if (effectiveMapId == 0) {
        effectiveMapId = GetCurrentMapId();
    }
    if (!m_vmapManager || !m_vmapManager->IsLoaded()) {
        return pos;
    }

    float groundZ = m_vmapManager->GetGroundHeight(pos, effectiveMapId);
    
    if (groundZ > -FLT_MAX) {
        if (pos.z < groundZ + 0.25f) { // If point is below or very close to ground
            Vector3 newPos = pos;
            newPos.z = groundZ + 0.5f; // Place it slightly above ground
            std::stringstream ss;
            ss << "EnsureAboveGround: Original: (" << std::fixed << std::setprecision(2) << pos.x << ", " << pos.y << ", " << pos.z 
               << "), Ground: " << groundZ << ", New: (" << newPos.x << ", " << newPos.y << ", " << newPos.z << ")";
            LOG_DEBUG(ss.str());
            return newPos;
        }
    }
    else {
        std::stringstream ss;
        ss << "EnsureAboveGround: Could not find ground for (" << std::fixed << std::setprecision(2) << pos.x << ", " << pos.y << ", " << pos.z << ")";
        LOG_WARNING(ss.str());
    }
    
    // If ground not found or point is already well above ground, return original position
    return pos;
}

NavMeshStats NavigationManager::GetNavMeshStats(uint32_t mapId) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    NavMeshStats stats;
    
    if (mapId == 0 && !m_loadedMaps.empty()) {
        mapId = static_cast<uint32_t>(m_loadedMaps.begin()->first);
    }
    
    auto it = m_loadedMaps.find(static_cast<int>(mapId));
    if (it != m_loadedMaps.end()) {
        stats.currentMapId = mapId;
        stats.loadedTiles = static_cast<uint32_t>(it->second.loadedTiles.size());

        const dtNavMesh* nm = it->second.navMesh;
        if (nm) {
            uint32_t totalPolys = 0;
            uint32_t totalVerts = 0;
            uint32_t totalTiles = 0;
            size_t memory = 0;
            for (int i = 0; i < nm->getMaxTiles(); ++i) {
                const dtMeshTile* tile = nm->getTile(i);
                if (tile && tile->header && tile->data) {
                    ++totalTiles;
                    totalPolys += tile->header->polyCount;
                    totalVerts += tile->header->vertCount;
                    memory += static_cast<size_t>(tile->dataSize);
                }
            }
            stats.totalTiles = totalTiles;
            stats.totalPolygons = totalPolys;
            stats.totalVertices = totalVerts;
            stats.memoryUsage = memory;
        }
    }
    
    return stats;
}

std::string NavigationManager::GetMMapFilePath(uint32_t mapId) const {
    return (std::filesystem::path(m_mapsDirectory) / (std::to_string(mapId) + ".mmap")).string();
}

bool NavigationManager::LoadMMapFile(const std::string& filePath, uint32_t mapId) {
    // This is a helper method, implementation moved to LoadMapNavMesh
    return LoadMapNavMesh(static_cast<int>(mapId));
}

bool NavigationManager::FindMapsDirectory() {
    m_mapsDirectory = ::FindMapsDirectory(s_hModule);
    return !m_mapsDirectory.empty();
}

std::string NavigationManager::GetLastError() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_lastError;
}

void NavigationManager::HumanizePath(NavigationPath& path, const PathfindingOptions& options) {
    if (path.waypoints.size() < 3) {
        return; // Need at least 3 points to humanize
    }
    
    std::vector<Waypoint> humanizedWaypoints;
    humanizedWaypoints.reserve(path.waypoints.size() * 2); // May add extra points

    // Keep the start point
    humanizedWaypoints.push_back(path.waypoints[0]);

    for (size_t i = 1; i < path.waypoints.size() - 1; ++i) {
        const Vector3& prev = path.waypoints[i-1].position;
        const Vector3& current = path.waypoints[i].position;
        const Vector3& next = path.waypoints[i+1].position;

        // Apply corner cutting if enabled
        if (options.cornerCutting != 0.0f) {
            Vector3 smoothed = SmoothCorner(prev, current, next, options.cornerCutting);
            humanizedWaypoints.emplace_back(smoothed);
        } else {
            humanizedWaypoints.push_back(path.waypoints[i]);
        }
    }

    // Keep the end point
    humanizedWaypoints.push_back(path.waypoints.back());

    // Apply wall padding if specified
    if (options.wallPadding > 0.1f) {
        ApplyWallPadding(path, options.wallPadding);
    }

    // Apply elevation smoothing if enabled
    if (options.maxElevationChange > 0.0f) {
        ApplyElevationSmoothing(path, options);
    }

    path.waypoints = std::move(humanizedWaypoints);
}

Vector3 NavigationManager::SmoothCorner(const Vector3& prev, const Vector3& current, const Vector3& next, float cornerFactor) {
    if (cornerFactor == 0.0f) return current;

    // Search extents for navmesh projection (2 yards horizontal, 5 yards vertical)
    const float extents[3] = {2.0f, 5.0f, 2.0f};
    
    // Calculate vectors to previous and next waypoints
    Vector3 toPrev = (prev - current).Normalized();
    Vector3 toNext = (next - current).Normalized();
    
    // Calculate the angle between the vectors
    float dot = toPrev.Dot(toNext);
    dot = std::max(-1.0f, std::min(1.0f, dot)); // Clamp to avoid numerical issues
    
    if (cornerFactor > 0.0f) {
        // POSITIVE: Smooth corners (more human-like)
        // Only smooth sharp corners (less than 120 degrees)
        if (dot < -0.5f) { // Sharp corner (greater than 120 degrees turn)
            Vector3 smoothDirection = (toPrev + toNext).Normalized();
            
            // Use conservative smoothing distance
            float maxSmoothDistance = std::min(prev.Distance(current), next.Distance(current)) * 0.3f;
            float smoothDistance = maxSmoothDistance * cornerFactor;
            
            Vector3 smoothedPos = current + smoothDirection * smoothDistance;
            
            // Validate the smoothed position is still accessible
            Vector3 recastPos = WowToRecast(smoothedPos);
            dtPolyRef polyRef;
            float closestPoint[3];
            
            if (dtStatusSucceed(m_navMeshQuery->findNearestPoly(&recastPos.x, extents, m_filter, &polyRef, closestPoint))) {
                return smoothedPos;
            }
        }
    } else {
        // NEGATIVE: Make corners more angular/direct (robot-like)
        float angularFactor = -cornerFactor; // Convert to positive for calculations
        
        // For any corner that isn't perfectly straight
        if (dot < 0.95f) { // Any corner with more than ~18 degrees
            // Calculate the "inside" direction of the corner
            Vector3 cornerDirection = (toPrev + toNext).Normalized();
            
            // Move AWAY from the corner direction to make it more angular
            Vector3 awayFromCorner = cornerDirection * (-1.0f);
            
            // Calculate push distance based on the sharpness of the corner
            float cornerSharpness = (1.0f - dot) * 0.5f; // 0 to 1, where 1 is sharpest
            float maxPushDistance = std::min(prev.Distance(current), next.Distance(current)) * 0.2f;
            float pushDistance = maxPushDistance * angularFactor * cornerSharpness;
            
            Vector3 angularPos = current + awayFromCorner * pushDistance;
            
            // Validate the angular position is still accessible
            Vector3 recastPos = WowToRecast(angularPos);
            dtPolyRef polyRef;
            float closestPoint[3];
            
            if (dtStatusSucceed(m_navMeshQuery->findNearestPoly(&recastPos.x, extents, m_filter, &polyRef, closestPoint))) {
                return angularPos;
            }
        }
    }
    
    return current;
}

// VMap collision detection methods
bool NavigationManager::IsInLineOfSight(const Vector3& start, const Vector3& end, uint32_t mapId) {
    if (!m_vmapManager || !m_vmapManager->IsLoaded()) {
        return true; // Default to true if VMap not available
    }

    // Determine map ID if not provided
    if (mapId == 0) {
        mapId = GetCurrentMapId();
        if (mapId == 0) {
            LOG_WARNING("IsInLineOfSight: No map ID provided and no maps loaded");
            return true;
        }
    }

    // Load VMap tile if needed
    m_vmapManager->LoadTileIfNeeded(mapId, start);
    m_vmapManager->LoadTileIfNeeded(mapId, end);

    return m_vmapManager->IsInLineOfSight(start, end, mapId);
}

float NavigationManager::GetDistanceToWall(const Vector3& position, const Vector3& direction, float maxDistance, uint32_t mapId) {
    if (!m_vmapManager || !m_vmapManager->IsLoaded()) {
        return maxDistance; // Return max distance if VMap not available
    }

    // Determine map ID if not provided
    if (mapId == 0) {
        mapId = GetCurrentMapId();
        if (mapId == 0) {
            LOG_WARNING("GetDistanceToWall: No map ID provided and no maps loaded");
            return maxDistance;
        }
    }

    // Load VMap tile if needed
    m_vmapManager->LoadTileIfNeeded(mapId, position);

    return m_vmapManager->GetDistanceToWall(position, direction, maxDistance, mapId);
}

bool NavigationManager::HasLineOfSightToTarget(const Vector3& start, const Vector3& end, uint32_t mapId) {
    // This is just an alias for IsInLineOfSight for semantic clarity
    return IsInLineOfSight(start, end, mapId);
}

std::vector<VMapCollisionResult> NavigationManager::GetNearbyWalls(const Vector3& center, float radius, uint32_t mapId) {
    if (!m_vmapManager || !m_vmapManager->IsLoaded()) {
        return {}; // Return empty vector if VMap not available
    }

    // Determine map ID if not provided
    if (mapId == 0) {
        mapId = GetCurrentMapId();
        if (mapId == 0) {
            LOG_WARNING("GetNearbyWalls: No map ID provided and no maps loaded");
            return {};
        }
    }

    // Load VMap tile if needed
    m_vmapManager->LoadTileIfNeeded(mapId, center);

    return m_vmapManager->GetNearbyWalls(center, radius, mapId);
}

std::string NavigationManager::GetMapName(uint32_t mapId) {
    static const std::unordered_map<uint32_t, std::string> mapNames = {
        {0, "Eastern Kingdom"},
        {1, "Kalimdor"},
        {13, "Testing"},
        {25, "Scott Test"},
        {30, "Alterac Valley"},
        {33, "Shadowfang Keep"},
        {34, "Stormwind Stockade"},
        {35, "<unused>StormwindPrison"},
        {36, "Deadmines"},
        {37, "Azshara Crater"},
        {42, "Collin's Test"},
        {43, "Wailing Caverns"},
        {44, "<unused> Monastery"},
        {47, "Razorfen Kraul"},
        {48, "Blackfathom Deeps"},
        {70, "Uldaman"},
        {90, "Gnomeregan"},
        {109, "Sunken Temple"},
        {129, "Razorfen Downs"},
        {169, "Emerald Dream"},
        {189, "Scarlet Monastery"},
        {209, "Zul'Farrak"},
        {229, "Blackrock Spire"},
        {230, "Blackrock Depths"},
        {249, "Onyxia's Lair"},
        {269, "Opening of the Dark Portal"},
        {289, "Scholomance"},
        {309, "Zul'Gurub"},
        {329, "Stratholme"},
        {349, "Maraudon"},
        {369, "Deeprun Tram"},
        {389, "Ragefire Chasm"},
        {409, "Molten Core"},
        {429, "Dire Maul"},
        {449, "Alliance PVP Barracks"},
        {450, "Horde PVP Barracks"},
        {451, "Development Land"},
        {469, "Blackwing Lair"},
        {489, "Warsong Gulch"},
        {509, "Ruins of Ahn'Qiraj"},
        {529, "Arathi Basin"},
        {530, "Outland"},
        {531, "Ahn'Qiraj Temple"},
        {532, "Karazhan"},
        {533, "Naxxramas"},
        {534, "The Battle for Mount Hyjal"},
        {540, "Hellfire Citadel: The Shattered Halls"},
        {542, "Hellfire Citadel: The Blood Furnace"},
        {543, "Hellfire Citadel: Ramparts"},
        {544, "Magtheridon's Lair"},
        {545, "Coilfang: The Steamvault"},
        {546, "Coilfang: The Underbog"},
        {547, "Coilfang: The Slave Pens"},
        {548, "Coilfang: Serpentshrine Cavern"},
        {550, "Tempest Keep"},
        {552, "Tempest Keep: The Arcatraz"},
        {553, "Tempest Keep: The Botanica"},
        {554, "Tempest Keep: The Mechanar"},
        {555, "Auchindoun: Shadow Labyrinth"},
        {556, "Auchindoun: Sethekk Halls"},
        {557, "Auchindoun: Mana-Tombs"},
        {558, "Auchindoun: Auchenai Crypts"},
        {559, "Nagrand Arena"},
        {560, "The Escape From Durnholde"},
        {562, "Blade's Edge Arena"},
        {564, "Black Temple"},
        {565, "Gruul's Lair"},
        {566, "Eye of the Storm"},
        {568, "Zul'Aman"},
        {571, "Northrend"},
        {572, "Ruins of Lordaeron"},
        {573, "ExteriorTest"},
        {574, "Utgarde Keep"},
        {575, "Utgarde Pinnacle"},
        {576, "The Nexus"},
        {578, "The Oculus"},
        {580, "The Sunwell"},
        {582, "Transport: Rut'theran to Auberdine"},
        {584, "Transport: Menethil to Theramore"},
        {585, "Magister's Terrace"},
        {586, "Transport: Exodar to Auberdine"},
        {587, "Transport: Feathermoon Ferry"},
        {588, "Transport: Menethil to Auberdine"},
        {589, "Transport: Orgrimmar to Grom'Gol"},
        {590, "Transport: Grom'Gol to Undercity"},
        {591, "Transport: Undercity to Orgrimmar"},
        {592, "Transport: Borean Tundra Test"},
        {593, "Transport: Booty Bay to Ratchet"},
        {594, "Transport: Howling Fjord Sister Mercy (Quest)"},
        {595, "The Culling of Stratholme"},
        {596, "Transport: Naglfar"},
        {597, "Craig Test"},
        {598, "Sunwell Fix (Unused)"},
        {599, "Halls of Stone"},
        {600, "Drak'Tharon Keep"},
        {601, "Azjol-Nerub"},
        {602, "Halls of Lightning"},
        {603, "Ulduar"},
        {604, "Gundrak"},
        {605, "Development Land (non-weighted textures)"},
        {606, "QA and DVD"},
        {607, "Strand of the Ancients"},
        {608, "Violet Hold"},
        {609, "Ebon Hold"},
        {610, "Transport: Tirisfal to Vengeance Landing"},
        {612, "Transport: Menethil to Valgarde"},
        {613, "Transport: Orgrimmar to Warsong Hold"},
        {614, "Transport: Stormwind to Valiance Keep"},
        {615, "The Obsidian Sanctum"},
        {616, "The Eye of Eternity"},
        {617, "Dalaran Sewers"},
        {618, "The Ring of Valor"},
        {619, "Ahn'kahet: The Old Kingdom"},
        {620, "Transport: Moa'ki to Unu'pe"},
        {621, "Transport: Moa'ki to Kamagua"},
        {622, "Transport: Orgrim's Hammer"},
        {623, "Transport: The Skybreaker"},
        {624, "Vault of Archavon"},
        {628, "Isle of Conquest"},
        {631, "Icecrown Citadel"},
        {632, "The Forge of Souls"},
        {641, "Transport: Alliance Airship BG"},
        {642, "Transport: HordeAirshipBG"},
        {647, "Transport: Orgrimmar to Thunder Bluff"},
        {649, "Trial of the Crusader"},
        {650, "Trial of the Champion"},
        {658, "Pit of Saron"},
        {668, "Halls of Reflection"},
        {672, "Transport: The Skybreaker (Icecrown Citadel Raid)"},
        {673, "Transport: Orgrim's Hammer (Icecrown Citadel Raid)"},
        {712, "Transport: The Skybreaker (IC Dungeon)"},
        {713, "Transport: Orgrim's Hammer (IC Dungeon)"},
        {718, "Trasnport: The Mighty Wind (Icecrown Citadel Raid)"},
        {723, "Stormwind"},
        {724, "The Ruby Sanctum"}
    };
    
    auto it = mapNames.find(mapId);
    if (it != mapNames.end()) {
        return it->second;
    }
    return "Unknown Map";
}

std::vector<std::pair<uint32_t, std::string>> NavigationManager::GetAllMapNames() {
    std::vector<std::pair<uint32_t, std::string>> result;
    
    // Get all available .mmap files from the maps directory
    NavigationManager& instance = Instance();
    std::string mapsDir = instance.GetMapsDirectory();
    if (mapsDir.empty()) {
        return result;
    }
    
    try {
        for (const auto& entry : std::filesystem::directory_iterator(mapsDir)) {
            if (entry.is_regular_file() && entry.path().extension() == ".mmap") {
                std::string filename = entry.path().stem().string();
                if (filename.length() >= 3) {
                    try {
                        uint32_t mapId = static_cast<uint32_t>(std::stoi(filename.substr(0, 3)));
                        std::string mapName = GetMapName(mapId);
                        result.emplace_back(mapId, mapName);
                    } catch (const std::invalid_argument&) {
                        // Skip invalid filenames
                    }
                }
            }
        }
        
        // Sort by map ID
        std::sort(result.begin(), result.end(), 
                 [](const std::pair<uint32_t, std::string>& a, const std::pair<uint32_t, std::string>& b) {
                     return a.first < b.first;
                 });
    } catch (const std::filesystem::filesystem_error& e) {
        LOG_ERROR("Error scanning maps directory: " + std::string(e.what()));
    }
    
    return result;
}

void NavigationManager::ApplyWallPadding(NavigationPath& path, float padding) {
    if (!m_navMeshQuery || path.waypoints.size() < 2 || padding <= 0.01f) {
        return;
    }

    // We need the navmesh from the currently loaded map
    if (m_loadedMaps.empty()) {
        LOG_WARNING("ApplyWallPadding: No map loaded, cannot perform wall padding.");
        return;
    }
    const dtNavMesh* navMesh = m_loadedMaps.begin()->second.navMesh;
    if (!navMesh) {
        LOG_WARNING("ApplyWallPadding: Current map has no valid navmesh.");
        return;
    }

    const float extents[3] = {2.0f, 4.0f, 2.0f}; // Search extents for findNearestPoly
    const int maxIters = 6;
    int adjustedCount = 0;

    // Iterate all waypoints except the first and last, which are fixed start/end points
    for (size_t i = 1; i < path.waypoints.size() - 1; ++i) {
        Vector3 recastPos = WowToRecast(path.waypoints[i].position);
        bool moved = false;

        for (int iter = 0; iter < maxIters; ++iter) {
            dtPolyRef polyRef;
            float closest[3];
            // Find the polygon under the current position
            if (dtStatusFailed(m_navMeshQuery->findNearestPoly(&recastPos.x, extents, m_filter, &polyRef, closest)) || !polyRef)
                break; // cannot project – abort

            float hitDist = 0.0f;
            float hitNormal[3];
            // Check distance to the nearest wall on that polygon
            if (dtStatusFailed(m_navMeshQuery->findDistanceToWall(polyRef, closest, padding + 0.1f, m_filter, &hitDist, nullptr, hitNormal)))
                break; // error

            // If we are far enough from the wall, we are done with this waypoint
            if (hitDist >= padding)
                break;

            // We are too close. Push the point away from the wall, purely on the horizontal plane.
            float push = (padding - hitDist) * 1.05f; // Small multiplier to ensure we clear the padding threshold
            recastPos.x += hitNormal[0] * push;
            recastPos.z += hitNormal[2] * push; // Only push on X and Z

            // Now that we've moved the point horizontally, we MUST find its new height on the navmesh
            // to prevent creating a steep, unnatural slope.
            float newHeight = recastPos.y;
            if (dtStatusSucceed(m_navMeshQuery->getPolyHeight(polyRef, &recastPos.x, &newHeight))) {
                recastPos.y = newHeight;
            }

            moved = true;
        }

        if (moved) {
            path.waypoints[i].position = RecastToWow(recastPos);
            adjustedCount++;
        }
    }

    if (adjustedCount > 0) {
        LOG_INFO("ApplyWallPadding: Adjusted " + std::to_string(adjustedCount) + " waypoints for wall padding (target padding=" + std::to_string(padding) + ")");
    }
}

dtQueryFilter* NavigationManager::CreateCustomFilter(const PathfindingOptions& options) {
    dtQueryFilter* filter = new dtQueryFilter();
    
    // By default restrict queries to ordinary ground polygons. We will fall back to steep if needed.
    unsigned short includeFlags = NAV_GROUND;
    unsigned short excludeFlags = NAV_GROUND_STEEP | NAV_WATER | NAV_MAGMA_SLIME;

    filter->setIncludeFlags(includeFlags);
    filter->setExcludeFlags(excludeFlags);

    // AUTOMATIC AREA COSTS based on terrain analysis
    float steepCost = options.avoidSteepTerrain ? options.steepTerrainCost : 25.0f; // Heavy-ish penalty for steep terrain
    float waterCost = 5.0f;   // Light penalty for water when allowed
    float magmaCost = 100.0f; // Heavy penalty for lava when allowed

    filter->setAreaCost(NAV_AREA_GROUND,        1.0f);      // Normal ground - preferred
    filter->setAreaCost(NAV_AREA_GROUND_STEEP,  steepCost); // Steep terrain - automatically avoid hills
    filter->setAreaCost(NAV_AREA_WATER,         waterCost); // Water - slight penalty
    filter->setAreaCost(NAV_AREA_MAGMA_SLIME,   magmaCost); // Lava - heavy penalty

    LOG_INFO("CreateCustomFilter: steepCost=" + std::to_string(steepCost) + ", waterCost=" + std::to_string(waterCost) + ", magmaCost=" + std::to_string(magmaCost));

    return filter;
}

void NavigationManager::ApplyElevationSmoothing(NavigationPath& path, const PathfindingOptions& options) {
    if (path.waypoints.size() < 3) {
        return; // Need at least 3 waypoints to smooth
    }
    
    std::vector<Waypoint> smoothedWaypoints;
    smoothedWaypoints.reserve(path.waypoints.size() * 2); // May add intermediate points
    
    // Always keep the start waypoint
    smoothedWaypoints.push_back(path.waypoints[0]);
    
    for (size_t i = 1; i < path.waypoints.size(); ++i) {
        const Vector3& prevPos = smoothedWaypoints.back().position;
        const Vector3& currentPos = path.waypoints[i].position;
        
        float elevationChange = std::abs(currentPos.z - prevPos.z);
        float horizontalDistance = std::sqrt(
            (currentPos.x - prevPos.x) * (currentPos.x - prevPos.x) + 
            (currentPos.y - prevPos.y) * (currentPos.y - prevPos.y)
        );
        
        // If elevation change is too steep, try to add intermediate waypoints
        if (elevationChange > options.maxElevationChange && horizontalDistance > 1.0f) {
            // Calculate how many intermediate points we need
            int intermediatePoints = static_cast<int>(std::ceil(elevationChange / options.maxElevationChange)) - 1;
            intermediatePoints = std::min(intermediatePoints, 5); // Limit to 5 intermediate points max
            
            // Add intermediate waypoints
            for (int j = 1; j <= intermediatePoints; ++j) {
                float t = static_cast<float>(j) / (intermediatePoints + 1);
                Vector3 interpPos = prevPos + (currentPos - prevPos) * t;
                smoothedWaypoints.emplace_back(AdjustToSurface(interpPos, options.mapId));
            }
        }
        
        // Add the current waypoint
        smoothedWaypoints.push_back(path.waypoints[i]);
    }
    
    path.waypoints = std::move(smoothedWaypoints);
}

static void AnalyzeNavMeshTiles(const dtNavMesh* navMesh) {
    if (!navMesh) return;
    int totalPolys = 0;
    int totalVerts = 0;
    int tileCount = 0;
    size_t memory = 0;

    for (int i = 0; i < navMesh->getMaxTiles(); ++i) {
        const dtMeshTile* tile = navMesh->getTile(i);
        if (!tile || !tile->header) continue;

        tileCount++;
        totalPolys += tile->header->polyCount;
        totalVerts += tile->header->vertCount;
        memory += static_cast<size_t>(tile->dataSize);
    }

    LOG_INFO("NavMesh Analysis Complete: " + std::to_string(totalPolys) + " polygons across " + std::to_string(tileCount) + " tiles.");
}

bool NavigationManager::SegmentHitsObstacle(const Vector3& a, const Vector3& b, uint32_t mapId) {
    // SIMPLIFIED OBSTACLE DETECTION - let nav-mesh handle most obstacle avoidance
    // Only check for major issues that nav-mesh might miss
    
    if (!m_navMeshQuery || m_loadedMaps.empty()) {
        return false;
    }

    const dtNavMesh* navMesh = m_loadedMaps.begin()->second.navMesh;
    if (!navMesh) return false;

    // Quick Detour raycast test for unwalkable edges
    Vector3 rcA = WowToRecast(a);
    Vector3 rcB = WowToRecast(b);
    float ext[3] = {4.0f, 8.0f, 4.0f};
    dtPolyRef startRef;
    float closest[3];
    
    if (dtStatusFailed(m_navMeshQuery->findNearestPoly(&rcA.x, ext, m_filter, &startRef, closest)) || !startRef) {
        return false; // Cannot project - assume no obstacle
    }

    dtRaycastHit hit;
    memset(&hit, 0, sizeof(hit));
    hit.t = 1.0f;
    
    dtStatus rs = m_navMeshQuery->raycast(startRef, &rcA.x, &rcB.x, m_filter, 0, &hit, 0);
    if (dtStatusSucceed(rs) && hit.t < 1.0f) {
        return true; // Detour found unwalkable edge
    }

    // OPTIONAL: VMap collision test for doodads/trees not in nav-mesh
    if (m_vmapManager && m_vmapManager->IsLoaded()) {
        if (!m_vmapManager->IsInLineOfSight(a, b, mapId))
            return true;

        // Extra terrain-surface check – ensure the straight line does not dive under ground.
        const float STEP = 1.5f; // yards
        Vector3 dir = b - a;
        float len = dir.Length();
        if (len > STEP) {
            dir = dir * (1.0f / len);
            int samples = static_cast<int>(len / STEP);
            for (int s = 1; s < samples; ++s) {
                Vector3 p = a + dir * (STEP * s);
                float ground = m_vmapManager->GetGroundHeight(p, mapId);
                if (ground != -FLT_MAX && ground - p.z > 0.3f) {
                    return true; // ground above path – segment underground
                }
            }
        }
    }

    return false;
}

void NavigationManager::ValidatePath(NavigationPath& path) {
    // MINIMAL PATH VALIDATION - trust the nav-mesh generation
    // Only remove obviously invalid segments
    if (path.waypoints.size() < 2) return;

    std::vector<Waypoint> validWaypoints;
    validWaypoints.push_back(path.waypoints[0]); // Always keep start

    for (size_t i = 1; i < path.waypoints.size(); ++i) {
        Vector3 current = path.waypoints[i].position;
        Vector3 previous = validWaypoints.back().position;
        
        // Only reject segments that are clearly invalid
        float distance = previous.Distance(current);
        if (distance > 0.1f && distance < 100.0f) { // Reasonable distance range
            validWaypoints.push_back(path.waypoints[i]);
        }
    }

    path.waypoints = std::move(validWaypoints);
}

Vector3 NavigationManager::FindSafePosition(const Vector3& from, const Vector3& to, uint32_t mapId) {
    // Improved fence-avoidance helper. We try lateral offsets in the horizontal (X-Y) plane and
    // project each candidate back onto the nav-mesh so that CTM never receives an off-mesh point.
    const float MIN_FRACTION = 0.2f;   // start testing 20 % towards the goal
    const float MAX_FRACTION = 0.8f;   // stop at 80 %

    Vector3 dir = to - from;
    float dist = dir.Length();
    if (dist < 0.1f) {
        return to; // already there
    }

    dir = dir * (1.0f / dist); // normalize

    // 2-D perpendicular on X-Y plane (ignore Z which is height)
    Vector3 perp(-dir.y, dir.x, 0.0f);
    perp = perp.Normalized();

    // Helper to project an arbitrary WoW position onto the nav-mesh surface
    auto projectToNavMesh = [&](const Vector3& wowPos) -> std::optional<Vector3>
    {
        if (!m_navMeshQuery) return std::nullopt;
        Vector3 rcPos = WowToRecast(wowPos);
        float ext[3] = { 4.0f, 6.0f, 4.0f };
        dtPolyRef ref;
        float nearest[3];
        if (dtStatusFailed(m_navMeshQuery->findNearestPoly(&rcPos.x, ext, m_filter, &ref, nearest)) || ref == 0)
            return std::nullopt;
        return RecastToWow(Vector3(nearest[0], nearest[1], nearest[2]));
    };

    // Search candidates along the segment first (shrinking towards the start)
    for (float t = MAX_FRACTION; t >= MIN_FRACTION; t -= 0.1f) {
        Vector3 mid = from + dir * (dist * t);
        
        bool hasLoS = !m_vmapManager || m_vmapManager->IsInLineOfSight(from, mid, mapId);
        
        if (hasLoS) {
            if (auto projected = projectToNavMesh(mid)) {
                return *projected;
            }
        }
    }

    // Lateral detour: offset left/right up to 5 yards
    for (float offset = 1.0f; offset <= 5.0f; offset += 1.0f) {
        for (int side = -1; side <= 1; side += 2) {
            Vector3 cand = to + perp * (offset * side);
            
            bool hasLoS = !m_vmapManager || m_vmapManager->IsInLineOfSight(from, cand, mapId);
            
            if (hasLoS) {
                if (auto projected = projectToNavMesh(cand)) {
                    return *projected;
                }
            }
        }
    }

    // Fallback – step back a little so caller can retry later.
    Vector3 fallback = from + dir * (dist * 0.5f);
    if (auto projected = projectToNavMesh(fallback)) {
        return *projected;
    }

    return from; // give up – should never happen
}

void NavigationManager::PreloadVMapTilesForPath(const std::vector<Waypoint>& waypoints, uint32_t mapId) {
    uint32_t effectiveMapId = mapId;
    if (effectiveMapId == 0) {
        effectiveMapId = GetCurrentMapId();
    }
    if (!m_vmapManager || !m_vmapManager->IsLoaded() || waypoints.empty()) {
        return;
    }

    std::set<std::pair<int, int>> requiredTiles;
    for (const auto& wp : waypoints) {
        requiredTiles.insert(GetTileFromPosition(wp.position));
    }

    if (requiredTiles.empty()) {
        return;
    }

    int loaded = 0;
    for (const auto& tilePair : requiredTiles) {
        if (m_vmapManager->LoadMapTile(effectiveMapId, tilePair.first, tilePair.second)) {
            loaded++;
        }
    }

    if (loaded > 0) {
        LOG_INFO("Pre-loaded " + std::to_string(loaded) + " VMap tiles for path validation.");
    }
}

std::pair<int, int> NavigationManager::GetTileFromPosition(const Vector3& position) {
    const float ZEROPOINT = 32.0f * VMAP_TILE_SIZE;

    float worldX = position.x;
    float worldY = position.y;

    int tileX = static_cast<int>(floorf((ZEROPOINT - worldY) / VMAP_TILE_SIZE));
    int tileY = static_cast<int>(floorf((ZEROPOINT - worldX) / VMAP_TILE_SIZE));

    // Clamp to valid tile range (0-63)
    tileX = std::max(0, std::min(63, tileX));
    tileY = std::max(0, std::min(63, tileY));

    return { tileX, tileY };
}

Vector3 NavigationManager::AttemptObstacleBypass(const Vector3& start, const Vector3& blockedEnd, const Vector3& targetEnd, uint32_t mapId) {
    if (!m_navMeshQuery || m_loadedMaps.empty()) {
        return blockedEnd;
    }

    // Simple bypass strategy: try a few perpendicular offsets
    Vector3 toTarget = (targetEnd - start).Normalized();
    Vector3 perpendicular(-toTarget.y, toTarget.x, 0.0f);
    perpendicular = perpendicular.Normalized();
    
    float extents[3] = {4.0f, 8.0f, 4.0f};
    
    // Try 3 distances on each side
    float distances[] = {10.0f, 25.0f, 50.0f};
    
    for (float distance : distances) {
        for (int side = -1; side <= 1; side += 2) {
            Vector3 bypassPos = blockedEnd + perpendicular * (distance * side);
            
            // Check if this position is on the navmesh
            Vector3 recastBypass = WowToRecast(bypassPos);
            dtPolyRef bypassPoly;
            float nearestPoint[3];
            
            dtStatus status = m_navMeshQuery->findNearestPoly(&recastBypass.x, extents, m_filter, &bypassPoly, nearestPoint);
            if (dtStatusSucceed(status) && bypassPoly != 0) {
                Vector3 projectedBypass = RecastToWow(Vector3(nearestPoint[0], nearestPoint[1], nearestPoint[2]));
                
                // Quick raycast test from start to bypass
                dtPolyRef startPoly;
                float startNearest[3];
                Vector3 recastStart = WowToRecast(start);
                status = m_navMeshQuery->findNearestPoly(&recastStart.x, extents, m_filter, &startPoly, startNearest);
                if (dtStatusSucceed(status) && startPoly != 0) {
                    dtRaycastHit rayHit;
                    memset(&rayHit, 0, sizeof(rayHit));
                    rayHit.t = 1.0f;
                    
                    status = m_navMeshQuery->raycast(startPoly, startNearest, nearestPoint, m_filter, 0, &rayHit, 0);
                    if (dtStatusSucceed(status) && rayHit.t >= 1.0f) {
                        // We can reach the bypass position, return it
                        return projectedBypass;
                    }
                }
            }
        }
    }
    
    return blockedEnd; // No bypass found
}

} // namespace Navigation 