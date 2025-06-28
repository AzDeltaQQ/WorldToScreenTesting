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
#include <cfloat>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Detour includes
#include "../../dependencies/recastnavigation/Detour/Include/DetourNavMesh.h"
#include "../../dependencies/recastnavigation/Detour/Include/DetourNavMeshQuery.h"
#include "../../dependencies/recastnavigation/Detour/Include/DetourCommon.h"
#include "../../dependencies/recastnavigation/Detour/Include/DetourNavMeshBuilder.h"

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

// Calculates the data size of a tile from its header.
/* int calculateTileDataSize(const dtMeshHeader* header) {
    const int headerSize = dtAlign4(sizeof(dtMeshHeader));
// ... existing code ...
    return headerSize + vertsSize + polysSize + linksSize +
        detailMeshesSize + detailVertsSize + detailTrisSize +
        bvtreeSize + offMeshLinksSize;
} */

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
static void MarkSteepPolys(dtNavMesh* navMesh, float heightThreshold);
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
    : m_navMesh(nullptr), m_navMeshQuery(nullptr), m_vmapManager(nullptr) {
    // Initialize simple VMapManager
    m_vmapManager = std::make_unique<VMapManager>();
    if (m_vmapManager && m_vmapManager->Initialize("./vmaps/")) {
        LOG_INFO("NavigationManager: VMapManager initialized successfully");
    } else {
        LOG_WARNING("NavigationManager: Failed to initialize VMapManager");
    }
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

    // ---------------------------------------------------------------------
    // Configure the query filter to allow walkable areas
    // Include ground and steep ground, but exclude water/lava to avoid underwater paths
    unsigned short includeFlags = NAV_GROUND | NAV_GROUND_STEEP; // Allow ground and steep areas
    unsigned short excludeFlags = NAV_WATER | NAV_MAGMA_SLIME;   // Exclude water and lava

    m_filter->setIncludeFlags(includeFlags);
    m_filter->setExcludeFlags(excludeFlags);

    // Area costs - prefer normal ground, heavily penalize steep terrain by default
    m_filter->setAreaCost(NAV_AREA_GROUND,        1.0f);   // Normal ground - preferred
    m_filter->setAreaCost(NAV_AREA_GROUND_STEEP,  10.0f);  // Steep terrain - heavily penalized to avoid hills
    m_filter->setAreaCost(NAV_AREA_WATER,         100.0f); // Water - heavily discourage (though excluded)
    m_filter->setAreaCost(NAV_AREA_MAGMA_SLIME,  100.0f);  // Lava/slime - heavily discourage (though excluded)
    
    LOG_INFO("Query filter configured - Include flags: " + std::to_string(includeFlags) + ", Exclude flags: " + std::to_string(excludeFlags));

    // Initialize VMap collision detection
    m_vmapManager = std::make_unique<VMapManager>();
    if (!m_vmapManager->Initialize(m_mapsDirectory)) {
        LOG_WARNING("VMap initialization failed - collision detection will be disabled");
        // Don't fail the entire initialization, just disable VMap features
    } else {
        LOG_INFO("VMap collision detection initialized successfully");
    }

    m_initialized = true;
    LOG_INFO("NavigationManager initialized successfully");
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
    }
}

void NavigationManager::UnloadAllMaps() {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (!m_loadedMaps.empty()) {
        LOG_INFO("Unloading all " + std::to_string(m_loadedMaps.size()) + " loaded maps");
        m_loadedMaps.clear();
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
        LOG_ERROR("Could not open MMap file: " + mmapPath.string());
        return false;
    }

    // Read navmesh params
    dtNavMeshParams params;
    file.read(reinterpret_cast<char*>(&params), sizeof(dtNavMeshParams));
    if (file.gcount() != sizeof(dtNavMeshParams)) {
        LOG_ERROR("Failed to read dtNavMeshParams from: " + mmapPath.string());
        file.close();
        return false;
    }
    
    LOG_INFO("NavMeshParams for map " + std::to_string(mapId) + ":");
    LOG_INFO("  orig: (" + std::to_string(params.orig[0]) + ", " + std::to_string(params.orig[1]) + ", " + std::to_string(params.orig[2]) + ")");
    LOG_INFO("  tileWidth: " + std::to_string(params.tileWidth));
    LOG_INFO("  tileHeight: " + std::to_string(params.tileHeight));
    LOG_INFO("  maxTiles: " + std::to_string(params.maxTiles));
    LOG_INFO("  maxPolys: " + std::to_string(params.maxPolys));

    LOG_INFO("File state after reading header: good=" + std::to_string(file.good()) + ", eof=" + std::to_string(file.eof()) + ", fail=" + std::to_string(file.fail()) + ", bad=" + std::to_string(file.bad()));
    auto currentPos = file.tellg();
    file.seekg(0, std::ios::end);
    LOG_INFO("Total file size: " + std::to_string(file.tellg()) + ", header size: " + std::to_string(sizeof(dtNavMeshParams)));
    file.seekg(currentPos);

    // Detour packs tile-poly refs into 22 bits: 22 - tileBits for polys.
    // If the supplied maxPolys cannot fit, clamp so navMesh->init() succeeds.
    auto nextPow2 = [](unsigned int v) {
        v--;
        v |= v >> 1;
        v |= v >> 2;
        v |= v >> 4;
        v |= v >> 8;
        v |= v >> 16;
        v++;
        return v;
    };

    auto ilog2 = [](unsigned int v) {
        unsigned int r = 0;
        while (v >>= 1) ++r;
        return r;
    };

#ifndef DT_POLYREF64
    unsigned int tileBits = ilog2(nextPow2(params.maxTiles));
    // Detour 32-bit polyRef layout: saltBits >= 10 therefore polyBits <= 22 - tileBits
    unsigned int maxPolyBits = 22u - tileBits;
    unsigned int allowedMaxPolys = 1u << maxPolyBits; // e.g. tileBits=10 -> polyBits=12 -> 4096

    if (params.maxPolys > static_cast<int>(allowedMaxPolys)) {
        int original = params.maxPolys;
        params.maxPolys = static_cast<int>(allowedMaxPolys);
        LOG_WARNING("Clamped maxPolys from " + std::to_string(original) + " to " + std::to_string(params.maxPolys) +
                    " (tileBits=" + std::to_string(tileBits) + ", polyBits=" + std::to_string(maxPolyBits) + ")");
    }
#else
    // 64-bit poly refs have ample bits; keep generator-supplied maxPolys.
#endif

    // Create and initialize the navigation mesh
    dtNavMesh* navMesh = dtAllocNavMesh();
    if (!navMesh) {
        LOG_ERROR("Failed to allocate navigation mesh for map " + std::to_string(mapId));
        return false;
    }

    dtStatus status = navMesh->init(&params);
    if (dtStatusFailed(status)) {
        LOG_ERROR("Failed to initialize navigation mesh for map " + std::to_string(mapId));
        dtFreeNavMesh(navMesh);
        return false;
    }

    // Close the params file as we will load separate tile files next
    file.close();

    // ---------------------------------------------------------------------
    // Load ALL available tiles for this map to ensure coverage everywhere
    // The real fix is to regenerate mmaps with smaller walkableRadius to prevent wall clipping
    // ---------------------------------------------------------------------

    std::unordered_map<uint32_t, dtTileRef> loadedTilesTemp;
    int tilesLoaded = 0;
    
    LOG_INFO("Loading ALL available tiles for map " + std::to_string(mapId) + " to ensure complete coverage");
    
    for (const auto& entry : std::filesystem::directory_iterator(m_mapsDirectory)) {
        if (!entry.is_regular_file())
            continue;

        const std::filesystem::path& path = entry.path();
        if (path.extension() != ".mmtile")
            continue;

        std::string filename = path.stem().string(); // without extension
        if (filename.size() != 7)
            continue; // invalid length

        // Check if first 3 chars match mapId
        int fileMapId = std::stoi(filename.substr(0, 3));
        if (fileMapId != mapId)
            continue;

        // Extract tileY and tileX
        int tileY = std::stoi(filename.substr(3, 2));
        int tileX = std::stoi(filename.substr(5, 2));

        std::ifstream tileFile(path, std::ios::binary);
        if (!tileFile.is_open()) {
            LOG_WARNING("Could not open tile file: " + path.string());
            continue;
        }

        MmapTileHeader tileHeader;
        tileFile.read(reinterpret_cast<char*>(&tileHeader), sizeof(MmapTileHeader));
        if (tileFile.gcount() != sizeof(MmapTileHeader)) {
            LOG_WARNING("Failed to read tile header from: " + path.string());
            continue;
        }

        if (tileHeader.mmapMagic != MMAP_MAGIC ||
            tileHeader.dtVersion != DT_NAVMESH_VERSION ||
            tileHeader.mmapVersion != MMAP_VERSION) {
            LOG_WARNING("Tile file version mismatch or invalid magic: " + path.string());
            continue;
        }

        if (tileHeader.size == 0) {
            LOG_WARNING("Tile file reports zero data size: " + path.string());
            continue;
        }

        // Allocate permanent buffer for Detour – navMesh will take ownership and free it with dtFree
        unsigned char* tileData = static_cast<unsigned char*>(dtAlloc(tileHeader.size, DT_ALLOC_PERM));
        if (!tileData) {
            LOG_ERROR("Failed to allocate " + std::to_string(tileHeader.size) + " bytes for tile (" + std::to_string(tileX) + "," + std::to_string(tileY) + ")");
            continue;
        }

        tileFile.seekg(sizeof(MmapTileHeader), std::ios::beg); // Ensure we're right after header
        tileFile.read(reinterpret_cast<char*>(tileData), tileHeader.size);
        if (static_cast<uint32_t>(tileFile.gcount()) != tileHeader.size) {
            LOG_WARNING("Failed to read complete tile data from: " + path.string());
            dtFree(tileData);
            continue;
        }

        dtTileRef lastRef = 0;
        dtStatus addStatus = navMesh->addTile(tileData, tileHeader.size, DT_TILE_FREE_DATA, 0, &lastRef);
        if (dtStatusFailed(addStatus)) {
            LOG_WARNING("Detour rejected tile (" + std::to_string(tileX) + "," + std::to_string(tileY) + ") for map " + std::to_string(mapId));
            dtFree(tileData);
            continue;
        }

        // Store mapping so we can unload later
        uint32_t tileIdKey = (static_cast<uint32_t>(tileX) << 16) | static_cast<uint32_t>(tileY);
        loadedTilesTemp[tileIdKey] = lastRef;

        tilesLoaded++;
    }

    LOG_INFO("Loaded " + std::to_string(tilesLoaded) + " tiles from MMap tile files for map " + std::to_string(mapId));

    LOG_INFO("NavMesh for map " + std::to_string(mapId) + " loaded. Analyzing tiles...");
    int totalPolys = 0;
    int totalVerts = 0;
    int loadedTilesWithPolys = 0;
    const dtNavMesh* constNavMesh = navMesh;
    for (int i = 0; i < constNavMesh->getMaxTiles(); ++i) {
        const dtMeshTile* tile = constNavMesh->getTile(i);
        if (tile && tile->header && tile->header->polyCount > 0) {
            totalPolys += tile->header->polyCount;
            totalVerts += tile->header->vertCount;
            loadedTilesWithPolys++;
            LOG_INFO("  Tile " + std::to_string(i) + " at (" + std::to_string(tile->header->x) + ", " + std::to_string(tile->header->y) +
                ") with " + std::to_string(tile->header->polyCount) + " polys, " + std::to_string(tile->header->vertCount) + " verts.");
            LOG_INFO("    bmin: (" + std::to_string(tile->header->bmin[0]) + ", " + std::to_string(tile->header->bmin[1]) + ", " + std::to_string(tile->header->bmin[2]) + ")");
            LOG_INFO("    bmax: (" + std::to_string(tile->header->bmax[0]) + ", " + std::to_string(tile->header->bmax[1]) + ", " + std::to_string(tile->header->bmax[2]) + ")");
        }
    }
    LOG_INFO("NavMesh for map " + std::to_string(mapId) + " analysis complete.");
    LOG_INFO("  Total loaded tiles with polygons: " + std::to_string(loadedTilesWithPolys));
    LOG_INFO("  Total polygons: " + std::to_string(totalPolys));
    LOG_INFO("  Total vertices: " + std::to_string(totalVerts));

    // Create map data structure
    MapData mapData;
    mapData.navMesh = navMesh;
    mapData.mapId = mapId;
    mapData.loadedTiles = std::move(loadedTilesTemp);
    
    // Store the loaded map
    m_loadedMaps[mapId] = std::move(mapData);

    LOG_INFO("Successfully loaded navigation mesh for map " + std::to_string(mapId));

    // Post-process the mesh to mark steep areas.
    MarkSteepPolys(navMesh, 1.5f);

    // Analyze loaded tiles for debugging.
    AnalyzeNavMeshTiles(navMesh);

    return true;
}

bool NavigationManager::LoadMapTile(int mapId, int tileX, int tileY) {
    // TODO: Implement tile loading
    return false;
}

PathResult NavigationManager::FindPath(const Vector3& start, const Vector3& end,
                                     NavigationPath& path, const PathfindingOptions& options) {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_loadedMaps.empty()) {
        LOG_ERROR("FindPath failed: No maps are loaded.");
        path.result = PathResult::FAILED_NO_NAVMESH;
        return path.result;
    }

    // For now, use the first loaded map. This should be improved later.
    MapData& mapData = m_loadedMaps.begin()->second;
    dtNavMesh* navMesh = mapData.navMesh;
    
    if (!navMesh || !m_navMeshQuery) {
        LOG_ERROR("FindPath failed: NavMesh or NavMeshQuery not initialized for map " + std::to_string(mapData.mapId));
        path.result = PathResult::FAILED_NO_NAVMESH;
        return path.result;
    }

    // Initialize the dtNavMeshQuery for this map.
    {
        int maxNodes = 32768;
        int totalPossiblePolys = navMesh->getMaxTiles() * navMesh->getParams()->maxPolys;
        maxNodes = std::max(maxNodes, totalPossiblePolys / 4);
        if (maxNodes > 0xFFFF)
            maxNodes = 0xFFFF; // dtNavMeshQuery hard limit

        dtStatus qs = m_navMeshQuery->init(navMesh, maxNodes);
        if (dtStatusFailed(qs)) {
            LOG_ERROR("FindPath failed: Could not initialize NavMeshQuery for map " + std::to_string(mapData.mapId));
            path.result = PathResult::FAILED_NO_NAVMESH;
            return path.result;
        }
    }

    // Use direct coordinate mapping (WoW_X -> recastX, WoW_Z -> recastY, WoW_Y -> recastZ)
    // No additional origin shift – MMaps are stored in absolute world-space coordinates.
    Vector3 recastStart = WoWToRecast(start);
    Vector3 recastEnd   = WoWToRecast(end);

    LOG_INFO("  recastStart: (" + std::to_string(recastStart.x) + ", " + std::to_string(recastStart.y) + ", " + std::to_string(recastStart.z) + ")");
    LOG_INFO("  recastEnd  : (" + std::to_string(recastEnd.x)   + ", " + std::to_string(recastEnd.y)   + ", " + std::to_string(recastEnd.z)   + ")");

    // Configure filter based on pathfinding options for terrain avoidance
    dtQueryFilter* filter = CreateCustomFilter(options);

    auto FindNearestPoly = [&](const Vector3& pos, dtPolyRef& outRef, float outPt[3], const char* tag) -> bool {
        // Debug: Check what tiles exist around this position
        LOG_INFO(std::string("    Searching for ") + tag + " polygon at: (" + std::to_string(pos.x) + ", " + std::to_string(pos.y) + ", " + std::to_string(pos.z) + ")");
        
        // TrinityCore's proven polygon search approach with expanded search ranges
        // First try with reasonable search box
        float extents[3] = {10.0f, 15.0f, 10.0f};    // Larger initial search area
        float closestPoint[3] = {0.0f, 0.0f, 0.0f};
        
        dtStatus res = m_navMeshQuery->findNearestPoly(&pos.x, extents, filter, &outRef, closestPoint);
        if (dtStatusSucceed(res) && outRef != 0) {
            float distance = dtVdist(closestPoint, &pos.x);
            if (distance <= 20.0f) { // More generous distance validation
                outPt[0] = closestPoint[0];
                outPt[1] = closestPoint[1]; 
                outPt[2] = closestPoint[2];
                LOG_INFO(std::string("    ") + tag + " nearestPoly found (normal search) distance=" + std::to_string(distance));
                return true;
            } else {
                LOG_WARNING(std::string("    ") + tag + " found polygon but distance too far: " + std::to_string(distance));
            }
        } else {
            LOG_WARNING(std::string("    ") + tag + " findNearestPoly failed with status: " + std::to_string(res));
        }

        // Fallback to very large search box if normal search fails
        extents[0] = 50.0f; // Horizontal
        extents[1] = 100.0f; // Vertical - very generous for elevation differences
        extents[2] = 50.0f; // Horizontal
        res = m_navMeshQuery->findNearestPoly(&pos.x, extents, filter, &outRef, closestPoint);
        if (dtStatusSucceed(res) && outRef != 0) {
            float distance = dtVdist(closestPoint, &pos.x);
            if (distance <= 100.0f) { // Very lenient for fallback search
                outPt[0] = closestPoint[0];
                outPt[1] = closestPoint[1];
                outPt[2] = closestPoint[2];
                LOG_INFO(std::string("    ") + tag + " nearestPoly found (wide search) distance=" + std::to_string(distance));
                return true;
            } else {
                LOG_WARNING(std::string("    ") + tag + " found polygon in wide search but distance too far: " + std::to_string(distance));
            }
        } else {
            LOG_WARNING(std::string("    ") + tag + " wide search also failed with status: " + std::to_string(res));
        }

        LOG_ERROR(std::string("    ") + tag + " could not find a valid polygon within acceptable distance");
        return false;
    };

    dtPolyRef startPoly = 0, endPoly = 0;
    float nearestStartPt[3], nearestEndPt[3];

    if (!FindNearestPoly(recastStart, startPoly, nearestStartPt, "start")) {
        m_lastError = "FindPath failed: Could not find a polygon near the start point.";
        LOG_ERROR(m_lastError);
        path.result = PathResult::FAILED_START_POLY;
        return path.result;
    }

    if (!FindNearestPoly(recastEnd, endPoly, nearestEndPt, "end")) {
        m_lastError = "FindPath failed: Could not find a polygon near the end point.";
        LOG_ERROR(m_lastError);
        path.result = PathResult::FAILED_END_POLY;
        return path.result;
    }

    // ----- poly path -----
    const int MAX_POLYS = 8192; // generous; node pool is 65k so this is safe
    std::vector<dtPolyRef> polyPath(MAX_POLYS);
    int polyPathCount = 0;

    dtStatus pathStatus = m_navMeshQuery->findPath(startPoly, endPoly, nearestStartPt, nearestEndPt,
                                                   filter, polyPath.data(), &polyPathCount, MAX_POLYS);

    if (dtStatusFailed(pathStatus) || polyPathCount == 0) {
        m_lastError = "FindPath failed: Could not create a path between the points.";
        LOG_ERROR(m_lastError);
        path.result = PathResult::FAILED_PATHFIND;
        return path.result;
    }

    LOG_INFO("  Polygon path found with " + std::to_string(polyPathCount) + " polygons");
    
    // Log first few and last few polygon IDs for debugging
    for (int i = 0; i < std::min(5, polyPathCount); ++i) {
        LOG_INFO("    Poly[" + std::to_string(i) + "]: " + std::to_string(polyPath[i]));
    }
    if (polyPathCount > 10) {
        LOG_INFO("    ... (" + std::to_string(polyPathCount - 10) + " polygons skipped) ...");
        for (int i = polyPathCount - 5; i < polyPathCount; ++i) {
            LOG_INFO("    Poly[" + std::to_string(i) + "]: " + std::to_string(polyPath[i]));
        }
    } else if (polyPathCount > 5) {
        for (int i = 5; i < polyPathCount; ++i) {
            LOG_INFO("    Poly[" + std::to_string(i) + "]: " + std::to_string(polyPath[i]));
        }
    }
    
    // Analyze area types in the polygon path to understand terrain usage
    std::map<unsigned char, int> areaTypeCounts;
    const dtNavMesh* attachedNavMesh = m_navMeshQuery->getAttachedNavMesh();
    for (int i = 0; i < polyPathCount; ++i) {
        // Get the tile and polygon from the polygon reference
        const dtMeshTile* tile = 0;
        const dtPoly* poly = 0;
        if (dtStatusSucceed(attachedNavMesh->getTileAndPolyByRef(polyPath[i], &tile, &poly))) {
            unsigned char areaType = poly->getArea();
            areaTypeCounts[areaType]++;
        }
    }
    
    LOG_INFO("  Area types in polygon path:");
    for (const auto& pair : areaTypeCounts) {
        std::string areaName;
        switch (pair.first) {
            case NAV_AREA_GROUND: areaName = "GROUND"; break;
            case NAV_AREA_GROUND_STEEP: areaName = "GROUND_STEEP"; break;
            case NAV_AREA_WATER: areaName = "WATER"; break;
            case NAV_AREA_MAGMA_SLIME: areaName = "MAGMA_SLIME"; break;
            default: areaName = "UNKNOWN(" + std::to_string(pair.first) + ")"; break;
        }
        float areaCost = filter->getAreaCost(pair.first);
        LOG_INFO("    " + areaName + " (area " + std::to_string(pair.first) + "): " + 
                 std::to_string(pair.second) + " polygons, cost=" + std::to_string(areaCost));
    }

    // ----- straight path -----
    int maxStraight = polyPathCount * 3; // worst-case heuristic
    std::vector<float> straightPath(maxStraight * 3); // verts
    std::vector<unsigned char> straightFlags(maxStraight);
    std::vector<dtPolyRef> straightPolys(maxStraight);
    int waypointCount = 0;

    // Use flags that provide a detailed path around corners.
    const int STRAIGHT_PATH_OPTIONS = DT_STRAIGHTPATH_ALL_CROSSINGS;
    dtStatus straightPathStatus = m_navMeshQuery->findStraightPath(nearestStartPt, nearestEndPt,
                                                                   polyPath.data(), polyPathCount,
                                                                   straightPath.data(), straightFlags.data(), straightPolys.data(),
                                                                   &waypointCount, maxStraight, STRAIGHT_PATH_OPTIONS);

    if (dtStatusFailed(straightPathStatus) || waypointCount == 0) {
        m_lastError = "FindPath failed: Could not create a straight path.";
        LOG_ERROR(m_lastError);
        path.result = PathResult::FAILED_PATHFIND;
        return path.result;
    }

    LOG_INFO("  Straight path generated with " + std::to_string(waypointCount) + " waypoints from " + std::to_string(polyPathCount) + " polygons");

    // ---- populate waypoint list with surface-level validation ----
    path.waypoints.clear();
    path.totalLength = 0.0f;

    // Always push the exact start position as WP0
    path.waypoints.emplace_back(start);

    for (int i = 0; i < waypointCount; ++i)
    {
        float* v = &straightPath[i * 3]; // x,y,z from Detour
        Vector3 recastPos(v[0], v[1], v[2]);
        Vector3 wowPos = RecastToWoW(recastPos);

        // CRITICAL: Surface-level validation to prevent underground waypoints
        // If this waypoint is significantly below the start position, it's likely underground
        float heightDifference = start.z - wowPos.z;
        if (heightDifference > 10.0f) { // More than 10 yards below start = underground
            LOG_WARNING("  WP" + std::to_string(i) + " rejected - underground (height diff: " + std::to_string(heightDifference) + ")");
            continue;
        }

        // Skip if this converts to start or end (will add end later)
        if (wowPos.Distance(start) < 0.05f || wowPos.Distance(end) < 0.05f)
            continue;

        // Skip duplicates
        if (!path.waypoints.empty() && path.waypoints.back().position.Distance(wowPos) < 0.05f)
            continue;

        path.waypoints.emplace_back(wowPos);
        LOG_INFO("  WP" + std::to_string(path.waypoints.size()-1) + " recast: (" + std::to_string(recastPos.x) + ", " + std::to_string(recastPos.y) + ", " + std::to_string(recastPos.z) + ") -> wow: (" +
                 std::to_string(wowPos.x) + ", " + std::to_string(wowPos.y) + ", " + std::to_string(wowPos.z) + ")");
    }

    // Always push the exact end position as the final waypoint
    path.waypoints.emplace_back(end);

    // Apply human-like path adjustments for safety and natural movement
    if (options.cornerCutting != 0.0f) {
        LOG_INFO("Applying corner shaping with cornerCutting=" + std::to_string(options.cornerCutting));
        HumanizePath(path, options);
    }

    // Compute initial path length after corner smoothing
    path.totalLength = 0.0f;
    for (size_t i = 1; i < path.waypoints.size(); ++i) {
        path.totalLength += path.waypoints[i-1].position.Distance(path.waypoints[i].position);
    }

    // After human-like corner shaping, apply wall padding if requested
    if (options.wallPadding > 0.01f) {
        LOG_INFO("Applying wall padding of " + std::to_string(options.wallPadding) + " yards to waypoints");
        ApplyWallPadding(path, options.wallPadding);

        // Recalculate path length again after padding
        path.totalLength = 0.0f;
        for (size_t i = 1; i < path.waypoints.size(); ++i) {
            path.totalLength += path.waypoints[i-1].position.Distance(path.waypoints[i].position);
        }
    }

    // Apply elevation smoothing to avoid steep terrain if requested
    if (options.avoidSteepTerrain) {
        LOG_INFO("Applying elevation smoothing with maxElevationChange=" + std::to_string(options.maxElevationChange) + " yards");
        ApplyElevationSmoothing(path, options);

        // Recalculate path length again after elevation smoothing
        path.totalLength = 0.0f;
        for (size_t i = 1; i < path.waypoints.size(); ++i) {
            path.totalLength += path.waypoints[i-1].position.Distance(path.waypoints[i].position);
        }
    }

    LOG_INFO("Successfully found path with " + std::to_string(path.waypoints.size()) + " waypoints. Total length: " + std::to_string(path.totalLength));
    path.result = PathResult::SUCCESS;
    return path.result;
}

bool NavigationManager::IsPositionValid(const Vector3& position, uint32_t mapId) {
    // TODO: Implement position validation
    return false;
}

Vector3 NavigationManager::GetClosestPoint(const Vector3& position, uint32_t mapId) {
    // TODO: Implement closest point calculation
    return position;
}

Vector3 NavigationManager::WoWToRecast(const Vector3& wowPos) {
    // TrinityCore coordinate mapping: WoW(x,y,z) -> Detour(y,z,x)
    // This matches exactly how TrinityCore PathGenerator converts coordinates
    return Vector3(wowPos.y, wowPos.z, wowPos.x);
}

Vector3 NavigationManager::RecastToWoW(const Vector3& recastPos) {
    // Inverse of TrinityCore mapping: Detour(y,z,x) -> WoW(x,y,z)
    return Vector3(recastPos.z, recastPos.x, recastPos.y);
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
        LOG_INFO("HumanizePath: Not enough waypoints to humanize (" + std::to_string(path.waypoints.size()) + ")");
        return; // Need at least 3 points to humanize
    }

    LOG_INFO("HumanizePath: Processing " + std::to_string(path.waypoints.size()) + " waypoints");
    
    std::vector<Waypoint> humanizedWaypoints;
    humanizedWaypoints.reserve(path.waypoints.size() * 2); // May add extra points

    // Keep the start point
    humanizedWaypoints.push_back(path.waypoints[0]);

    for (size_t i = 1; i < path.waypoints.size() - 1; ++i) {
        Vector3 prevPos = path.waypoints[i-1].position;
        Vector3 currentPos = path.waypoints[i].position;
        Vector3 nextPos = path.waypoints[i+1].position;

        LOG_INFO("HumanizePath: Processing waypoint " + std::to_string(i) + " at (" + 
                 std::to_string(currentPos.x) + ", " + std::to_string(currentPos.y) + ", " + std::to_string(currentPos.z) + ")");

        // Apply corner cutting/shaping
        Vector3 adjustedPos = SmoothCorner(prevPos, currentPos, nextPos, options.cornerCutting);

        // Check if position was actually changed
        float distanceMoved = currentPos.Distance(adjustedPos);
        if (distanceMoved > 0.01f) {
            LOG_INFO("HumanizePath: Adjusted waypoint " + std::to_string(i) + " by " + std::to_string(distanceMoved) + " yards to (" +
                     std::to_string(adjustedPos.x) + ", " + std::to_string(adjustedPos.y) + ", " + std::to_string(adjustedPos.z) + ")");
        }

        humanizedWaypoints.emplace_back(adjustedPos);
    }

    // Keep the end point
    humanizedWaypoints.push_back(path.waypoints.back());

    // Replace the original waypoints
    path.waypoints = std::move(humanizedWaypoints);
    
    LOG_INFO("HumanizePath: Completed, final waypoint count: " + std::to_string(path.waypoints.size()));
}

Vector3 NavigationManager::SmoothCorner(const Vector3& prev, const Vector3& current, const Vector3& next, float cornerFactor) {
    if (cornerFactor == 0.0f) return current;
    
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
            Vector3 recastPos = WoWToRecast(smoothedPos);
            dtPolyRef polyRef;
            float closestPoint[3];
            float extents[3] = {2.0f, 5.0f, 2.0f};
            
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
            Vector3 recastPos = WoWToRecast(angularPos);
            dtPolyRef polyRef;
            float closestPoint[3];
            float extents[3] = {2.0f, 5.0f, 2.0f};
            
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

    if (!m_vmapManager || !m_vmapManager->IsLoaded()) {
        LOG_WARNING("ApplyWallPadding: VMapManager not available. Cannot apply wall padding.");
        return;
    }

    // We need mapId from the calling context - for now, use 0 as default
    // TODO: Pass mapId as parameter or store it in NavigationPath
    uint32_t mapId = 0;

    // Extents used when searching for polygons near a point
    const float extents[3] = { 4.0f, 6.0f, 4.0f };
    const int maxIterations = 5;

    int adjustedCount = 0;
    
    for (size_t i = 1; i < path.waypoints.size() - 1; ++i) {
        Vector3 waypoint = path.waypoints[i].position;
        
        // Find minimum wall distance in all directions using enhanced VMap system
        float minWallDistance = GetMinWallDistance(waypoint, mapId, padding + 5.0f);
        
        LOG_INFO("ApplyWallPadding: WP" + std::to_string(i) + " at (" + 
                 std::to_string(waypoint.x) + ", " + std::to_string(waypoint.y) + ", " + std::to_string(waypoint.z) + 
                 ") minWallDistance=" + std::to_string(minWallDistance) + ", padding=" + std::to_string(padding));

        if (minWallDistance < padding) {
            Vector3 adjustedPos = waypoint;
            bool adjusted = false;
            
            // Try to move the waypoint away from walls iteratively
            for (int iteration = 0; iteration < maxIterations; ++iteration) {
                // Find the direction to the nearest wall
                Vector3 wallDirection = GetNearestWallDirection(adjustedPos, mapId, padding + 5.0f);
                
                if (wallDirection.x == 0.0f && wallDirection.y == 0.0f && wallDirection.z == 0.0f) {
                    // No wall found or direction couldn't be determined
                    break;
                }
                
                // Move away from the wall by the required padding distance
                float moveDistance = padding - minWallDistance + 0.5f; // Add small buffer
                Vector3 moveVector = {
                    -wallDirection.x * moveDistance,
                    -wallDirection.y * moveDistance,
                    0.0f // Keep Z unchanged for now
                };
                
                Vector3 newPos = {
                    adjustedPos.x + moveVector.x,
                    adjustedPos.y + moveVector.y,
                    adjustedPos.z + moveVector.z
                };
                
                // Project the new position onto the navigation mesh
                float nearestPt[3];
                dtPolyRef nearestPoly;
                dtStatus status = m_navMeshQuery->findNearestPoly(
                    &newPos.x, extents, m_filter, &nearestPoly, nearestPt);
                
                if (dtStatusSucceed(status) && nearestPoly != 0) {
                    // Try to move along the surface to the new position
                    float resultPos[3];
                    dtPolyRef visited[16];
                    int visitedCount = 0;
                    
                    status = m_navMeshQuery->moveAlongSurface(
                        nearestPoly, &adjustedPos.x, nearestPt, m_filter,
                        resultPos, visited, &visitedCount, 16);
                    
                    if (dtStatusSucceed(status)) {
                        adjustedPos = { resultPos[0], resultPos[1], resultPos[2] };
                        
                        // Check if we've improved the wall distance
                        float newMinWallDistance = GetMinWallDistance(adjustedPos, mapId, padding + 5.0f);
                        
                        LOG_INFO("ApplyWallPadding: WP" + std::to_string(i) + " iteration " + std::to_string(iteration) + 
                                 ": moved to (" + std::to_string(adjustedPos.x) + ", " + std::to_string(adjustedPos.y) + ", " + 
                                 std::to_string(adjustedPos.z) + "), newMinWallDistance=" + std::to_string(newMinWallDistance));
                        
                        if (newMinWallDistance >= padding) {
                            adjusted = true;
                            break;
                        }
                        
                        minWallDistance = newMinWallDistance;
                    } else {
                        LOG_WARNING("ApplyWallPadding: moveAlongSurface failed for WP" + std::to_string(i) + " iteration " + std::to_string(iteration));
                        break;
                    }
                } else {
                    LOG_WARNING("ApplyWallPadding: findNearestPoly failed for WP" + std::to_string(i) + " iteration " + std::to_string(iteration));
                    break;
                }
            }
            
            if (adjusted) {
                path.waypoints[i] = adjustedPos;
                adjustedCount++;
                LOG_INFO("ApplyWallPadding: Adjusted WP" + std::to_string(i) + " to (" + 
                         std::to_string(adjustedPos.x) + ", " + std::to_string(adjustedPos.y) + ", " + std::to_string(adjustedPos.z) + ")");
            } else {
                LOG_WARNING("ApplyWallPadding: Failed to adjust WP" + std::to_string(i) + " after " + std::to_string(maxIterations) + " iterations");
            }
        } else {
            LOG_INFO("ApplyWallPadding: WP" + std::to_string(i) + " has sufficient wall distance (" + 
                     std::to_string(minWallDistance) + " >= " + std::to_string(padding) + "), skipping");
        }
    }
    
    if (adjustedCount > 0) {
        LOG_INFO("ApplyWallPadding: Adjusted " + std::to_string(adjustedCount) + " waypoints for wall padding");
    } else {
        LOG_INFO("ApplyWallPadding: No waypoints needed adjustment");
    }
}

float NavigationManager::GetMinWallDistance(const Vector3& position, uint32_t mapId, float searchRadius) {
    if (!m_vmapManager || !m_vmapManager->IsLoaded()) {
        return searchRadius; // Return max distance if VMap not available
    }
    
    float minDistance = searchRadius;
    
    // Cast rays in 8 directions around the point to find walls
    const int numDirections = 8;
    const float angleStep = 2.0f * M_PI / numDirections;
    
    for (int i = 0; i < numDirections; ++i) {
        float angle = i * angleStep;
        Vector3 direction = {
            cos(angle),
            sin(angle),
            0.0f // Horizontal plane only
        };
        
        // Use the enhanced VMapManager to get wall distance
        float wallDistance = m_vmapManager->GetDistanceToWall(position, direction, searchRadius, mapId);
        
        if (wallDistance < minDistance) {
            minDistance = wallDistance;
        }
    }
    
    return minDistance;
}

Vector3 NavigationManager::GetNearestWallDirection(const Vector3& position, uint32_t mapId, float searchRadius) {
    if (!m_vmapManager || !m_vmapManager->IsLoaded()) {
        return {0.0f, 0.0f, 0.0f}; // Return zero vector if VMap not available
    }
    
    float nearestDistance = searchRadius;
    Vector3 nearestDirection = {0.0f, 0.0f, 0.0f};
    
    // Cast rays in 8 directions around the point to find the nearest wall
    const int numDirections = 8;
    const float angleStep = 2.0f * M_PI / numDirections;
    
    for (int i = 0; i < numDirections; ++i) {
        float angle = i * angleStep;
        Vector3 direction = {
            cos(angle),
            sin(angle),
            0.0f // Horizontal plane only
        };
        
        // Use the enhanced VMapManager to get wall distance
        float wallDistance = m_vmapManager->GetDistanceToWall(position, direction, searchRadius, mapId);
        
        if (wallDistance < nearestDistance) {
            nearestDistance = wallDistance;
            nearestDirection = direction; // Direction TO the wall
        }
    }
    
    return nearestDirection;
}

dtQueryFilter* NavigationManager::CreateCustomFilter(const PathfindingOptions& options) {
    static float originalSteepCost = -1.0f;
    static float originalWaterCost = -1.0f;
    static float originalMagmaCost = -1.0f;

    if (originalSteepCost < 0.0f) {
        originalSteepCost = m_filter->getAreaCost(NAV_AREA_GROUND_STEEP);
        originalWaterCost = m_filter->getAreaCost(NAV_AREA_WATER);
        originalMagmaCost = m_filter->getAreaCost(NAV_AREA_MAGMA_SLIME);
    }

    if (options.avoidSteepTerrain) {
        float steepCost = std::max(50.0f, options.steepTerrainCost);
        m_filter->setAreaCost(NAV_AREA_GROUND_STEEP, steepCost);
        m_filter->setAreaCost(NAV_AREA_WATER, 5.0f);
        m_filter->setAreaCost(NAV_AREA_MAGMA_SLIME, 100.0f);
        LOG_INFO("CreateCustomFilter: steepCost=" + std::to_string(steepCost) + ", waterCost=5, magmaCost=100");
    } else {
        m_filter->setAreaCost(NAV_AREA_GROUND_STEEP, originalSteepCost);
        m_filter->setAreaCost(NAV_AREA_WATER, originalWaterCost);
        m_filter->setAreaCost(NAV_AREA_MAGMA_SLIME, originalMagmaCost);
    }

    return m_filter;
}

void NavigationManager::ApplyElevationSmoothing(NavigationPath& path, const PathfindingOptions& options) {
    if (path.waypoints.size() < 3) {
        return; // Need at least 3 waypoints to smooth
    }
    
    LOG_INFO("ApplyElevationSmoothing: Processing " + std::to_string(path.waypoints.size()) + " waypoints");
    
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
        
        // Calculate slope percentage
        float slope = (horizontalDistance > 0.1f) ? (elevationChange / horizontalDistance) * 100.0f : 0.0f;
        
        LOG_INFO("ApplyElevationSmoothing: WP" + std::to_string(i) + " elevation change: " + 
                 std::to_string(elevationChange) + "y, horizontal: " + std::to_string(horizontalDistance) + 
                 "y, slope: " + std::to_string(slope) + "%");
        
        // If elevation change is too steep, try to add intermediate waypoints
        if (elevationChange > options.maxElevationChange && horizontalDistance > 2.0f) {
            LOG_INFO("ApplyElevationSmoothing: Steep elevation change detected, adding intermediate waypoints");
            
            // Calculate how many intermediate points we need
            int numIntermediatePoints = static_cast<int>(std::ceil(elevationChange / options.maxElevationChange)) - 1;
            numIntermediatePoints = std::min(numIntermediatePoints, 5); // Limit to max 5 intermediate points
            
            // Add intermediate waypoints with gradual elevation changes
            for (int j = 1; j <= numIntermediatePoints; ++j) {
                float t = static_cast<float>(j) / static_cast<float>(numIntermediatePoints + 1);
                
                Vector3 intermediatePos = {
                    prevPos.x + t * (currentPos.x - prevPos.x),
                    prevPos.y + t * (currentPos.y - prevPos.y),
                    prevPos.z + t * (currentPos.z - prevPos.z)
                };
                
                // Try to project this intermediate point onto the navmesh
                Vector3 recastPos = WoWToRecast(intermediatePos);
                dtPolyRef polyRef;
                float closestPoint[3];
                float extents[3] = {5.0f, 10.0f, 5.0f}; // Generous search area
                
                dtStatus status = m_navMeshQuery->findNearestPoly(&recastPos.x, extents, m_filter, &polyRef, closestPoint);
                if (dtStatusSucceed(status) && polyRef != 0) {
                    Vector3 projectedPos = RecastToWoW(Vector3(closestPoint[0], closestPoint[1], closestPoint[2]));
                    smoothedWaypoints.emplace_back(projectedPos);
                    
                    LOG_INFO("ApplyElevationSmoothing: Added intermediate WP at (" + 
                             std::to_string(projectedPos.x) + ", " + std::to_string(projectedPos.y) + 
                             ", " + std::to_string(projectedPos.z) + ")");
                } else {
                    LOG_WARNING("ApplyElevationSmoothing: Failed to project intermediate point onto navmesh");
                }
            }
        }
        
        // Add the current waypoint
        smoothedWaypoints.push_back(path.waypoints[i]);
    }
    
    // Replace the original waypoints
    path.waypoints = std::move(smoothedWaypoints);
    
    LOG_INFO("ApplyElevationSmoothing: Completed, final waypoint count: " + std::to_string(path.waypoints.size()));
}

static void MarkSteepPolys(dtNavMesh* navMesh, float heightThreshold) {
    if (!navMesh) return;
    int changed = 0;
    
    const dtNavMesh* constNavMesh = navMesh;

    for (int i = 0; i < constNavMesh->getMaxTiles(); ++i) {
        const dtMeshTile* tile = constNavMesh->getTile(i);
        if (!tile || !tile->header) continue;
        
        dtPolyRef base = constNavMesh->getPolyRefBase(tile);
        
        for (int j = 0; j < tile->header->polyCount; ++j) {
            const dtPoly* p = &tile->polys[j];
            
            if (p->getType() == DT_POLYTYPE_GROUND) {
                float minY = FLT_MAX, maxY = -FLT_MAX;
                for (int k = 0; k < p->vertCount; ++k) {
                    const float* v = &tile->verts[p->verts[k] * 3];
                    minY = std::min(minY, v[1]);
                    maxY = std::max(maxY, v[1]);
                }
                
                if ((maxY - minY) > heightThreshold) {
                    dtPolyRef polyRef = base | (dtPolyRef)j;
                    // Use the public setPolyArea function to modify the area flag
                    navMesh->setPolyArea(polyRef, NAV_AREA_GROUND_STEEP);
                    changed++;
                }
            }
        }
    }
    
    if (changed > 0) {
        LOG_INFO("MarkSteepPolys: Marked " + std::to_string(changed) + " polygons as steep.");
    } else {
        LOG_INFO("MarkSteepPolys: No polygons met steepness threshold of " + std::to_string(heightThreshold) + "y.");
    }
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

} // namespace Navigation 