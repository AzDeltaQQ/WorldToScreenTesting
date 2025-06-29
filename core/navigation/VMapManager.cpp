#include "VMapManager.h"
#include "../logs/Logger.h"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include "../../TrinityCore-3.3.5/src/common/Collision/VMapDefinitions.h"
#include "../../TrinityCore-3.3.5/src/common/Collision/Management/VMapManager2.h"
#include "../../TrinityCore-3.3.5/src/common/Collision/Management/IVMapManager.h"

using namespace VMAP; // TrinityCore collision namespace
using namespace Navigation;

namespace {
    // Convert WoW coordinates to VMap internal representation (same as TrinityCore's convertPositionToInternalRep)
    Vector3 WowToVMap(const Vector3& wowPos) {
        const float mid = 0.5f * 64.0f * 533.33333333f; // 17066.666...
        return Vector3(mid - wowPos.x, mid - wowPos.y, wowPos.z);
    }
    
    // Convert VMap coordinates back to WoW coordinates  
    Vector3 VMapToWow(const Vector3& vmapPos) {
        const float mid = 0.5f * 64.0f * 533.33333333f; // 17066.666...
        return Vector3(mid - vmapPos.x, mid - vmapPos.y, vmapPos.z);
    }
}

VMapManager::VMapManager() = default;

VMapManager::~VMapManager() { Shutdown(); }

bool VMapManager::Initialize(const std::string& mmapsDirectory)
{
    m_mmapsDirectory = mmapsDirectory;

    if (!std::filesystem::exists(m_mmapsDirectory))
    {
        LOG_ERROR("mmaps directory does not exist: " + m_mmapsDirectory);
        return false;
    }

    // derive vmaps sibling directory ( …/maps/vmaps )
    std::filesystem::path basePath = std::filesystem::path(m_mmapsDirectory).parent_path();
    m_vmapsDirectory = (basePath / "vmaps").string();

    if (!std::filesystem::exists(m_vmapsDirectory))
    {
        LOG_ERROR("vmaps directory does not exist: " + m_vmapsDirectory);
        return false;
    }

    m_tcVMapMgr = std::make_unique<VMapManager2>();

    // Try to load the main map tree for Kalimdor (map 1) to test VMap functionality
    LOG_DEBUG("Testing VMap loading for map 1 (Kalimdor)...");
    int loadResult = m_tcVMapMgr->loadMap(m_vmapsDirectory.c_str(), 1, 32, 32);
    if (loadResult == VMAP::VMAP_LOAD_RESULT_OK) {
        LOG_INFO("VMap test load successful for map 1 tile (32,32)");
        // Keep it loaded for testing
        uint64_t tileKey = GetTileKey(32, 32);
        m_loadedTiles[1].insert(tileKey);
        m_loadedTileCount++;
    } else {
        LOG_WARNING("VMap test load failed for map 1 tile (32,32), result code: " + std::to_string(loadResult));
    }

    m_isInitialized = true;

    LOG_INFO("VMapManager initialized. vmaps path: " + m_vmapsDirectory);
    return true;
}

void VMapManager::Shutdown()
{
    if (m_tcVMapMgr)
    {
        m_tcVMapMgr->unloadMap(0); // unload all via VMapManager2
        m_tcVMapMgr.reset();
    }

    m_loadedTiles.clear();
    m_loadedTileCount = 0;
    m_isInitialized = false;
}

bool VMapManager::IsInLineOfSight(const Vector3& start, const Vector3& end, uint32_t mapId)
{
    if (!m_isInitialized || !m_tcVMapMgr)
        return true;

    Vector3 vmapStart = WowToVMap(start);
    Vector3 vmapEnd = WowToVMap(end);

    return m_tcVMapMgr->isInLineOfSight(
        mapId,
        vmapStart.x, vmapStart.y, vmapStart.z,
        vmapEnd.x,   vmapEnd.y,   vmapEnd.z,
        ModelIgnoreFlags::Nothing);
}

bool VMapManager::IsPointWalkable(const Vector3& point, uint32_t mapId) const {
    if (!m_tcVMapMgr)
        return true;

    Vector3 vmapPoint = WowToVMap(point);
    float ground = m_tcVMapMgr->getHeight(mapId, vmapPoint.x, vmapPoint.y, vmapPoint.z + 50.0f, 100.0f);
    return ground < VMAP_INVALID_HEIGHT; // if we got a height value, assume walkable.
}

bool VMapManager::HasTerrainObstacle(const Vector3& point, uint32_t mapId) const { return false; }

float VMapManager::GetGroundHeight(const Vector3& point, uint32_t mapId) const
{
    if (!m_tcVMapMgr)
        return -FLT_MAX; // VMap system not available

    // Ensure the VMap tile covering this point is loaded
    const_cast<VMapManager*>(this)->LoadTileIfNeeded(mapId, point);

    // TrinityCore VMap API expects INTERNAL coordinates (mid - x, mid - y, z).
    // Convert the input WoW world-space position to that space.
    Vector3 vmapPoint = WowToVMap(point);

    // Query slightly above the point so we can search downward.
    constexpr float SEARCH_OFFSET   = 5.0f;   // yards
    constexpr float MAX_SEARCH_DIST = 100.0f; // yards

    float height = m_tcVMapMgr->getHeight(mapId,
                                          vmapPoint.x,
                                          vmapPoint.y,
                                          vmapPoint.z + SEARCH_OFFSET,
                                          MAX_SEARCH_DIST);

    // `getHeight` returns either a valid Z value or the constant
    // VMAP_INVALID_HEIGHT_VALUE (-200000.f).  Treat anything below
    // VMAP_INVALID_HEIGHT as invalid.
    if (height > VMAP_INVALID_HEIGHT)
        return height;

    return -FLT_MAX; // indicate failure
}

bool VMapManager::LoadMapTile(uint32_t mapId, uint32_t tileX, uint32_t tileY) {
    if (!m_isInitialized) {
        LOG_ERROR("VMapManager not initialized");
        return false;
    }

    // TrinityCore uses a different coordinate system for VMap tiles
    // The tile coordinates need to be converted from WoW world coordinates
    LOG_DEBUG("Loading VMap tile for map " + std::to_string(mapId) + " tile (" + std::to_string(tileX) + ", " + std::to_string(tileY) + ")");

    int result = m_tcVMapMgr->loadMap(m_vmapsDirectory.c_str(), mapId, static_cast<int>(tileX), static_cast<int>(tileY));
    if (result == VMAP::VMAP_LOAD_RESULT_OK)
    {
        uint64_t tileKey = GetTileKey(tileX, tileY);
        m_loadedTiles[mapId].insert(tileKey);
        m_loadedTileCount++;
        LOG_DEBUG("Successfully loaded VMap tile (" + std::to_string(tileX) + ", " + std::to_string(tileY) + ") for map " + std::to_string(mapId));
        return true;
    }

    if (result == VMAP::VMAP_LOAD_RESULT_IGNORED)
    {
        LOG_DEBUG("No VMap tile present for map " + std::to_string(mapId) + " (" + std::to_string(tileX) + ", " + std::to_string(tileY) + ") – file missing, skipping.");
    }
    else
    {
        LOG_WARNING("Failed to load VMap tile (code " + std::to_string(result) + ") map=" + std::to_string(mapId) + " x=" + std::to_string(tileX) + " y=" + std::to_string(tileY));
    }
    return false;
}

void VMapManager::UnloadMapTile(uint32_t mapId, uint32_t tileX, uint32_t tileY) {
    if (!m_isInitialized) {
        return;
    }

    if (m_tcVMapMgr)
        m_tcVMapMgr->unloadMap(mapId, static_cast<int>(tileX), static_cast<int>(tileY));

    uint64_t tileKey = GetTileKey(tileX, tileY);
    auto it = m_loadedTiles.find(mapId);
    if (it != m_loadedTiles.end())
    {
        it->second.erase(tileKey);
        if (m_loadedTileCount) --m_loadedTileCount;
    }
}

void VMapManager::LoadTileIfNeeded(uint32_t mapId, const Vector3& position) {
    if (!m_isInitialized) {
        return;
    }

    auto tileCoords = GetTileCoordinates(position);
    uint64_t tileKey = GetTileKey(tileCoords.first, tileCoords.second);
    
    // Check if tile is already loaded
    auto mapIt = m_loadedTiles.find(mapId);
    if (mapIt != m_loadedTiles.end() && mapIt->second.count(tileKey) > 0) {
        return; // Already loaded
    }
    
    // Load the tile
    LoadMapTile(mapId, tileCoords.first, tileCoords.second);
}

float VMapManager::GetDistanceToWall(const Vector3& position, const Vector3& direction, float maxDistance, uint32_t mapId) {
    if (!m_isInitialized || !m_tcVMapMgr)
        return maxDistance;

    Vector3 end = position + direction.Normalized() * maxDistance;
    Vector3 vmapStart = WowToVMap(position);
    Vector3 vmapEnd = WowToVMap(end);

    float rx, ry, rz; // result placeholders
    if (m_tcVMapMgr->getObjectHitPos(mapId,
        vmapStart.x, vmapStart.y, vmapStart.z,
        vmapEnd.x,   vmapEnd.y,   vmapEnd.z,
        rx, ry, rz, 0.0f))
    {
        Vector3 vmapHit(rx, ry, rz);
        Vector3 wowHit = VMapToWow(vmapHit);
        return position.Distance(wowHit);
    }

    return maxDistance;
}

std::vector<VMapCollisionResult> VMapManager::GetNearbyWalls(const Vector3& center, float radius, uint32_t mapId) {
    std::vector<VMapCollisionResult> out; // TrinityCore API does not expose neighbourhood query; keep empty.
    return out;
}

// Private helper methods
std::pair<uint32_t, uint32_t> VMapManager::GetTileCoordinates(const Vector3& position) const {
    // According to WoW coordinate system:
    //   World X axis -> South (+x decreases tileY)
    //   World Y axis -> West  (+y increases tileX)
    // VMap tile indexing:
    //   tileX = floor( (ZEROPOINT - worldY) / TILE_SIZE )
    //   tileY = floor( (ZEROPOINT - worldX) / TILE_SIZE )
    const float TILE_SIZE = 533.33333f;
    const float ZEROPOINT = 32.0f * TILE_SIZE;

    float wowY = position.y; // Vector3.y holds WoW Y
    float wowX = position.x; // Vector3.x holds WoW X

    uint32_t tileX = static_cast<uint32_t>(floorf((ZEROPOINT - wowY) / TILE_SIZE));
    uint32_t tileY = static_cast<uint32_t>(floorf((ZEROPOINT - wowX) / TILE_SIZE));
    
    // Clamp to valid tile range [0, 63]
    tileX = std::min(63u, std::max(0u, tileX));
    tileY = std::min(63u, std::max(0u, tileY));
    
    return std::make_pair(tileX, tileY);
}

uint64_t VMapManager::GetTileKey(uint32_t tileX, uint32_t tileY) {
    return (static_cast<uint64_t>(tileX) << 32) | static_cast<uint64_t>(tileY);
}

bool VMapManager::LoadVMapFile(const std::string& filePath) {
    // This would parse the actual VMap file format
    // For now, just check if the file exists
    return std::filesystem::exists(filePath);
} 