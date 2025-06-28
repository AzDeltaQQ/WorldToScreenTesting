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

    return m_tcVMapMgr->isInLineOfSight(
        mapId,
        start.x, start.y, start.z,
        end.x, end.y, end.z,
        ModelIgnoreFlags::Nothing);
}

bool VMapManager::IsPointWalkable(const Vector3& point, uint32_t mapId) const {
    if (!m_tcVMapMgr)
        return true;

    float ground = m_tcVMapMgr->getHeight(mapId, point.x, point.y, point.z + 50.0f /*search up*/, 100.0f);
    return ground < VMAP_INVALID_HEIGHT; // if we got a height value, assume walkable.
}

bool VMapManager::HasTerrainObstacle(const Vector3& point, uint32_t mapId) const { return false; }

float VMapManager::GetGroundHeight(const Vector3& point, uint32_t mapId) const
{
    if (!m_tcVMapMgr)
        return point.z;

    float h = m_tcVMapMgr->getHeight(mapId, point.x, point.y, point.z + 50.0f, 100.0f);
    if (h < VMAP_INVALID_HEIGHT) return point.z;
    return h;
}

bool VMapManager::LoadMapTile(uint32_t mapId, uint32_t tileX, uint32_t tileY) {
    if (!m_isInitialized) {
        LOG_ERROR("VMapManager not initialized");
        return false;
    }

    int result = m_tcVMapMgr->loadMap(m_vmapsDirectory.c_str(), mapId, static_cast<int>(tileX), static_cast<int>(tileY));
    if (result == VMAP::VMAP_LOAD_RESULT_OK || result == VMAP::VMAP_LOAD_RESULT_IGNORED)
    {
        uint64_t tileKey = GetTileKey(tileX, tileY);
        m_loadedTiles[mapId].insert(tileKey);
        m_loadedTileCount++;
        return true;
    }

    LOG_WARNING("Failed to load VMap tile (code " + std::to_string(result) + ") map=" + std::to_string(mapId) + " x=" + std::to_string(tileX) + " y=" + std::to_string(tileY));
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

    float rx = end.x, ry = end.y, rz = end.z;
    if (m_tcVMapMgr->getObjectHitPos(mapId,
        position.x, position.y, position.z,
        end.x, end.y, end.z,
        rx, ry, rz, 0.0f))
    {
        Vector3 hit(rx, ry, rz);
        return position.Distance(hit);
    }

    return maxDistance;
}

std::vector<VMapCollisionResult> VMapManager::GetNearbyWalls(const Vector3& center, float radius, uint32_t mapId) {
    std::vector<VMapCollisionResult> out; // TrinityCore API does not expose neighbourhood query; keep empty.
    return out;
}

// Private helper methods
std::pair<uint32_t, uint32_t> VMapManager::GetTileCoordinates(const Vector3& position) {
    // WoW uses 533.33333 yards per tile
    const float TILE_SIZE = 533.33333f;
    const float MAP_SIZE = 64.0f * TILE_SIZE; // 64x64 tiles
    const float MAP_HALF_SIZE = MAP_SIZE * 0.5f;
    
    // Convert world coordinates to tile coordinates
    uint32_t tileX = static_cast<uint32_t>((MAP_HALF_SIZE - position.x) / TILE_SIZE);
    uint32_t tileY = static_cast<uint32_t>((MAP_HALF_SIZE - position.y) / TILE_SIZE);
    
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