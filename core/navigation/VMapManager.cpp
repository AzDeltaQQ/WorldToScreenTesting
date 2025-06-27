#include "VMapManager.h"
#include "../logs/Logger.h"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <algorithm>

using namespace Navigation;

VMapManager::VMapManager() = default;

VMapManager::~VMapManager() {
    Shutdown();
}

bool VMapManager::Initialize(const std::string& mapsDirectory) {
    try {
        LOG_INFO("Initializing VMapManager with directory: " + mapsDirectory);
        
        m_mapsDirectory = mapsDirectory;
        
        // Determine base maps directory (parent of mmaps if path ends with /mmaps)
        std::filesystem::path basePath = m_mapsDirectory;
        if (basePath.filename() == "mmaps") {
            basePath = basePath.parent_path();
        }

        std::string vmapPath = (basePath / "vmaps").string();
        
        if (!std::filesystem::exists(vmapPath)) {
            LOG_WARNING("VMap directory not found: " + vmapPath);
            LOG_WARNING("VMap collision detection will be disabled");
            m_isInitialized = false;
            return false;
        }
        
        m_isInitialized = true;
        LOG_INFO("VMapManager initialized successfully (simplified mode)");
        return true;
    }
    catch (const std::exception& e) {
        LOG_ERROR("Exception during VMapManager initialization: " + std::string(e.what()));
        return false;
    }
}

void VMapManager::Shutdown() {
    if (m_isInitialized) {
        LOG_INFO("Shutting down VMapManager");
        m_loadedTiles.clear();
        m_loadedTileCount = 0;
    }
    m_isInitialized = false;
}

bool VMapManager::LoadMapTile(uint32_t mapId, uint32_t tileX, uint32_t tileY) {
    if (!m_isInitialized) {
        LOG_ERROR("VMapManager not initialized");
        return false;
    }

    try {
        // Generate VMap file path
        std::stringstream ss;
        ss << m_mapsDirectory << "/vmaps/" << std::setfill('0') << std::setw(3) << mapId 
           << "_" << std::setw(2) << tileX << "_" << std::setw(2) << tileY << ".vmtree";
        std::string filePath = ss.str();
        
        // Check if file exists
        if (!std::filesystem::exists(filePath)) {
            LOG_INFO("VMap tile file not found (no collision data): " + filePath);
            return true; // Not an error, just no collision data for this tile
        }
        
        // Track the loaded tile
        uint64_t tileKey = GetTileKey(tileX, tileY);
        m_loadedTiles[mapId].insert(tileKey);
        m_loadedTileCount++;
        
        LOG_INFO("Loaded VMap tile - MapID: " + std::to_string(mapId) + 
                ", TileX: " + std::to_string(tileX) + 
                ", TileY: " + std::to_string(tileY));
        return true;
    }
    catch (const std::exception& e) {
        LOG_ERROR("Exception loading VMap tile: " + std::string(e.what()));
        return false;
    }
}

void VMapManager::UnloadMapTile(uint32_t mapId, uint32_t tileX, uint32_t tileY) {
    if (!m_isInitialized) {
        return;
    }

    try {
        uint64_t tileKey = GetTileKey(tileX, tileY);
        auto mapIt = m_loadedTiles.find(mapId);
        if (mapIt != m_loadedTiles.end()) {
            mapIt->second.erase(tileKey);
            if (m_loadedTileCount > 0) {
                m_loadedTileCount--;
            }
        }
        
        LOG_INFO("Unloaded VMap tile - MapID: " + std::to_string(mapId) + 
                ", TileX: " + std::to_string(tileX) + 
                ", TileY: " + std::to_string(tileY));
    }
    catch (const std::exception& e) {
        LOG_ERROR("Exception unloading VMap tile: " + std::string(e.what()));
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
    if (mapIt != m_loadedTiles.end() && mapIt->second.find(tileKey) != mapIt->second.end()) {
        return; // Already loaded
    }
    
    // Load the tile
    LoadMapTile(mapId, tileCoords.first, tileCoords.second);
}

bool VMapManager::IsInLineOfSight(const Vector3& start, const Vector3& end, uint32_t mapId) {
    if (!m_isInitialized) {
        return true; // Default to true if no collision data
    }

    // For now, return a simplified line of sight check
    // This would need proper VMap file parsing to implement correctly
    return CheckLineOfSightSimple(start, end, mapId);
}

float VMapManager::GetDistanceToWall(const Vector3& position, const Vector3& direction, float maxDistance, uint32_t mapId) {
    if (!m_isInitialized) {
        return maxDistance; // Return max distance if no collision data
    }

    // For now, return a simplified wall distance check
    // This would need proper VMap file parsing to implement correctly
    return GetWallDistanceSimple(position, direction, maxDistance, mapId);
}

std::vector<VMapCollisionResult> VMapManager::GetNearbyWalls(const Vector3& center, float radius, uint32_t mapId) {
    std::vector<VMapCollisionResult> results;
    
    if (!m_isInitialized) {
        return results; // Return empty vector if no collision data
    }

    // For now, return a simplified nearby walls check
    // This would need proper VMap file parsing to implement correctly
    LOG_INFO("GetNearbyWalls called - simplified implementation returns empty results");
    
    return results;
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

bool VMapManager::CheckLineOfSightSimple(const Vector3& start, const Vector3& end, uint32_t mapId) {
    // Simplified line of sight check
    // In a real implementation, this would:
    // 1. Load the appropriate VMap tiles
    // 2. Parse the collision geometry
    // 3. Perform ray-triangle intersection tests
    
    // For now, just return true (clear line of sight)
    // This could be enhanced with basic terrain height checks
    return true;
}

float VMapManager::GetWallDistanceSimple(const Vector3& position, const Vector3& direction, float maxDistance, uint32_t mapId) {
    // Simplified wall distance check
    // In a real implementation, this would:
    // 1. Load the appropriate VMap tiles
    // 2. Cast a ray in the given direction
    // 3. Find the nearest collision point
    
    // For now, just return the max distance
    return maxDistance;
} 