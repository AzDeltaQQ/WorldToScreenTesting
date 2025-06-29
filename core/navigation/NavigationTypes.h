#pragma once

#include "../types/types.h"
#include <vector>
#include <string>
#include <memory>

// Forward declarations - remove dtPolyRef forward declaration as it conflicts with Recast typedef
class dtNavMesh;
class dtNavMeshQuery;
class dtQueryFilter;

namespace Navigation {

    // Navigation result status
    enum class PathResult {
        SUCCESS,
        FAILED_NO_NAVMESH,
        FAILED_START_POLY,
        FAILED_END_POLY,
        FAILED_PATHFIND,
        FAILED_INVALID_INPUT,
        PARTIAL_PATH
    };

    // Navigation movement types
    enum class MovementType {
        WALK,
        FLY,
        SWIM,
        AUTO_DETECT
    };

    // Map tile information
    struct MapTile {
        uint32_t mapId;
        uint32_t tileX;
        uint32_t tileY;
        bool loaded;
        std::string filePath;
    };

    // Navigation path waypoint
    struct Waypoint {
        Vector3 position;
        float radius;
        MovementType type;
        bool isOffMeshConnection;

        Waypoint() : position(0.0f, 0.0f, 0.0f), radius(1.0f), type(MovementType::WALK), isOffMeshConnection(false) {}
        Waypoint(const Vector3& pos, float r = 1.0f, MovementType mt = MovementType::WALK) 
            : position(pos), radius(r), type(mt), isOffMeshConnection(false) {}
    };

    // Complete navigation path
    struct NavigationPath {
        std::vector<Waypoint> waypoints;
        PathResult result;
        float totalLength;
        MovementType movementType;
        bool isComplete;

        NavigationPath() : result(PathResult::FAILED_NO_NAVMESH), totalLength(0.0f), 
                          movementType(MovementType::WALK), isComplete(false) {}

        bool IsValid() const { return result == PathResult::SUCCESS && !waypoints.empty(); }
        size_t GetWaypointCount() const { return waypoints.size(); }
    };

    // Navigation query parameters
    struct PathfindingOptions {
        uint32_t mapId;
        MovementType movementType;
        float stepSize;
        float maxSearchDistance;
        bool allowPartialPath;
        bool smoothPath;
        
        // Human-like pathfinding options
        float cornerCutting;       // How much to cut corners -1.0 to 1.0 (negative = more angular, positive = smoother)
        bool avoidEdges;          // Try to stay away from cliff edges (default: true)
        bool preferCenterPath;    // Prefer walking in center of walkable areas (default: true)

        // Minimum distance to keep from walls/obstacles when adjusting waypoints (0 = disabled)
        float wallPadding;
        
        // Terrain preference options
        bool avoidSteepTerrain;   // Strongly prefer flat terrain over hills/slopes
        float steepTerrainCost;   // Cost multiplier for steep terrain (higher = more avoidance)
        float maxElevationChange; // Maximum elevation change to allow between waypoints (yards)
        bool preferLowerElevation; // When multiple paths available, prefer lower elevation routes

        PathfindingOptions() : mapId(0), movementType(MovementType::WALK), stepSize(0.5f), 
                              maxSearchDistance(50.0f), allowPartialPath(true), smoothPath(true),
                              cornerCutting(0.0f), avoidEdges(true), preferCenterPath(true), wallPadding(2.5f),
                              avoidSteepTerrain(true), steepTerrainCost(10.0f), maxElevationChange(2.5f), 
                              preferLowerElevation(true) {}
    };

    // MMap file header structure (based on reference implementation)
    struct MMapFileHeader {
        char magic[8];          // "MMAP001"
        uint32_t version;
        uint32_t tileCount;
        uint32_t reserved[4];
    };

    // Navigation mesh statistics
    struct NavMeshStats {
        uint32_t totalTiles;
        uint32_t loadedTiles;
        uint32_t totalPolygons;
        uint32_t totalVertices;
        size_t memoryUsage;
        uint32_t currentMapId;

        NavMeshStats() : totalTiles(0), loadedTiles(0), totalPolygons(0), 
                        totalVertices(0), memoryUsage(0), currentMapId(0) {}
    };

    // Visualization settings
    struct VisualizationSettings {
        bool enabled;
        bool showNavMesh;
        bool showTileBounds;
        bool showPaths;
        bool showWaypoints;
        bool showDebugInfo;
        float meshOpacity;
        uint32_t pathColor;
        uint32_t waypointColor;
        uint32_t tileColor;

        VisualizationSettings() 
            : enabled(false), showNavMesh(false), showTileBounds(false), showPaths(true), showWaypoints(true)
            , showDebugInfo(false), meshOpacity(0.3f), pathColor(0xFF00FF00)
            , waypointColor(0xFFFF0000), tileColor(0xFF0000FF) {}
    };

} // namespace Navigation 