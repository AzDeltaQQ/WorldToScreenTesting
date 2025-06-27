#pragma once

#include "../types/types.h"
#include <string>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <cstdint>

namespace Navigation {

    struct VMapCollisionResult {
        bool hit = false;
        Vector3 hitPoint;
        Vector3 hitNormal;
        float distance = 0.0f;
        std::string modelName;
    };

    class VMapManager {
    public:
        VMapManager();
        ~VMapManager();

        // Initialize VMap system with maps directory
        bool Initialize(const std::string& mapsDirectory);
        void Shutdown();

        // Load/unload map tiles
        bool LoadMapTile(uint32_t mapId, uint32_t tileX, uint32_t tileY);
        void UnloadMapTile(uint32_t mapId, uint32_t tileX, uint32_t tileY);
        void LoadTileIfNeeded(uint32_t mapId, const Vector3& position);

        // Collision queries
        bool IsInLineOfSight(const Vector3& start, const Vector3& end, uint32_t mapId);
        float GetDistanceToWall(const Vector3& position, const Vector3& direction, float maxDistance, uint32_t mapId);
        std::vector<VMapCollisionResult> GetNearbyWalls(const Vector3& center, float radius, uint32_t mapId);

        // Utility
        bool IsLoaded() const { return m_isInitialized; }
        uint32_t GetLoadedTileCount() const { return m_loadedTileCount; }

    private:
        std::string m_mapsDirectory;
        bool m_isInitialized = false;
        uint32_t m_loadedTileCount = 0;
        
        // Track loaded tiles per map
        std::unordered_map<uint32_t, std::unordered_set<uint64_t>> m_loadedTiles;

        // Helper functions
        std::pair<uint32_t, uint32_t> GetTileCoordinates(const Vector3& position);
        uint64_t GetTileKey(uint32_t tileX, uint32_t tileY);
        bool LoadVMapFile(const std::string& filePath);
        
        // Simplified collision detection
        bool CheckLineOfSightSimple(const Vector3& start, const Vector3& end, uint32_t mapId);
        float GetWallDistanceSimple(const Vector3& position, const Vector3& direction, float maxDistance, uint32_t mapId);
    };

} // namespace Navigation 