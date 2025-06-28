#pragma once

#include <vector>
#include <string>
#include <unordered_map>
#include <mutex>
#include <fstream>
#include <memory>
#include "../types/types.h"
#include "NavigationTypes.h"
#include "VMapManager.h"
#include "../../dependencies/recastnavigation/Detour/Include/DetourNavMesh.h"
#include "../../dependencies/recastnavigation/Detour/Include/DetourNavMeshQuery.h"

// Forward declarations for Detour types (already provided by Detour headers)
struct dtNavMesh;
struct dtNavMeshQuery;
struct dtQueryFilter;

namespace Navigation {

// Map data structure to hold nav mesh and loaded tiles
struct MapData {
    dtNavMesh* navMesh;
    int mapId;
    std::unordered_map<uint32_t, dtTileRef> loadedTiles; // tileId -> tileRef
    
    MapData() : navMesh(nullptr), mapId(-1) {}
    ~MapData();
    
    // Move constructor and assignment
    MapData(MapData&& other) noexcept;
    MapData& operator=(MapData&& other) noexcept;
    
    // Delete copy constructor and assignment
    MapData(const MapData&) = delete;
    MapData& operator=(const MapData&) = delete;
};

class NavigationManager {
public:
    // Singleton access
    static NavigationManager& Instance();
    static void SetModuleHandle(HMODULE hModule);

    // Core functionality
    bool Initialize();
    void Shutdown();

    // Map loading
    bool LoadMapNavMesh(int mapId);
    bool LoadMapTile(int mapId, int tileX, int tileY);
    bool IsMapLoaded(uint32_t mapId) const;
    void UnloadMap(uint32_t mapId);
    void UnloadAllMaps();

    // Map name utilities
    static std::string GetMapName(uint32_t mapId);
    static std::vector<std::pair<uint32_t, std::string>> GetAllMapNames();

    // Pathfinding
    PathResult FindPath(const Vector3& start, const Vector3& end, 
                       NavigationPath& path, const PathfindingOptions& options = {});
    bool IsPositionValid(const Vector3& position, uint32_t mapId = 0);
    Vector3 GetClosestPoint(const Vector3& position, uint32_t mapId = 0);

    // VMap collision detection
    bool IsInLineOfSight(const Vector3& start, const Vector3& end, uint32_t mapId = 0);
    float GetDistanceToWall(const Vector3& position, const Vector3& direction, float maxDistance = 50.0f, uint32_t mapId = 0);
    bool HasLineOfSightToTarget(const Vector3& start, const Vector3& end, uint32_t mapId = 0);
    std::vector<VMapCollisionResult> GetNearbyWalls(const Vector3& center, float radius = 10.0f, uint32_t mapId = 0);

    // Coordinate conversion utilities
    // TrinityCore MMAP convention:
    //   recastX = -WoW_Y
    //   recastY =  WoW_Z
    //   recastZ = -WoW_X
    static Vector3 WoWToRecast(const Vector3& wowPos);
    static Vector3 RecastToWoW(const Vector3& recastPos);

    // State queries
    bool IsInitialized() const { return m_initialized; }
    uint32_t GetCurrentMapId() const;
    std::vector<MapTile> GetLoadedTiles() const;

    // Settings
    void SetVisualizationEnabled(bool enabled) { m_visualizationSettings.enabled = enabled; }
    bool IsVisualizationEnabled() const { return m_visualizationSettings.enabled; }
    const VisualizationSettings& GetVisualizationSettings() const { return m_visualizationSettings; }
    void SetVisualizationSettings(const VisualizationSettings& settings) { m_visualizationSettings = settings; }

    // Statistics
    NavMeshStats GetNavMeshStats(uint32_t mapId = 0) const;
    std::string GetLastError() const;
    
    // Directory access
    std::string GetMapsDirectory() const { return m_mapsDirectory; }

    // Helper methods for wall distance detection
    float GetMinWallDistance(const Vector3& position, uint32_t mapId, float searchRadius);
    Vector3 GetNearestWallDirection(const Vector3& position, uint32_t mapId, float searchRadius);

private:
    NavigationManager();
    ~NavigationManager();
    static HMODULE s_hModule;

    // Initialization helpers
    bool FindMapsDirectory();
    
    // File operations
    std::string GetMMapFilePath(uint32_t mapId) const;
    bool LoadMMapFile(const std::string& filePath, uint32_t mapId);

    // Thread safety
    mutable std::mutex m_mutex;

    // Core Recast/Detour components
    dtNavMesh* m_navMesh;
    dtNavMeshQuery* m_navMeshQuery;
    dtQueryFilter* m_filter;

    // Enhanced VMap collision detection (using improved VMapManager)
    std::unique_ptr<VMapManager> m_vmapManager;

    // Data management
    std::unordered_map<int, MapData> m_loadedMaps;
    std::string m_mapsDirectory;

    // State
    bool m_initialized;

    // Settings
    VisualizationSettings m_visualizationSettings;

    // Error handling
    std::string m_lastError;

    // Humanization methods for natural pathfinding
    void HumanizePath(NavigationPath& path, const PathfindingOptions& options);
    Vector3 SmoothCorner(const Vector3& prev, const Vector3& current, const Vector3& next, float cornerFactor);

    // Push waypoints away from nearby walls by a minimum padding distance
    void ApplyWallPadding(NavigationPath& path, float padding);

    // Internal implementation methods
    dtQueryFilter* CreateCustomFilter(const PathfindingOptions& options);
    void ApplyElevationSmoothing(NavigationPath& path, const PathfindingOptions& options);
};

// Convenience macros
#define NAVIGATION_MGR Navigation::NavigationManager::Instance() 
} // namespace Navigation 