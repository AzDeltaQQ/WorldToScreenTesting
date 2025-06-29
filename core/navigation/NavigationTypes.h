#pragma once

#include <vector>
#include <string>
#include <cstdint>
#include "types.h"

namespace Navigation {

    // Result of a pathfinding query
    enum class PathResult {
        SUCCESS,
        FAILED_INVALID_START,
        FAILED_INVALID_END,
        FAILED_NO_PATH,
        FAILED_NO_NAVMESH,
        FAILED_TIMEOUT,
        FAILED_INTERNAL_ERROR,
        PARTIAL_PATH,
        FAILED_INVALID_INPUT,
        FAILED_START_POLY,
        FAILED_END_POLY,
        FAILED_PATHFIND
    };

    // Type of movement for a path segment
    enum class MovementType {
        WALK,
        JUMP,
        SWIM,
        FLY
    };

    // Represents a single tile in the navigation mesh grid
    struct MapTile {
        uint32_t mapId;
        uint32_t tileX;
        uint32_t tileY;
        bool loaded;
        std::string filePath;
    };

    // A single point in a navigation path
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
        void Clear() { waypoints.clear(); totalLength = 0.0f; isComplete = false; result = PathResult::FAILED_NO_NAVMESH; }
    };

    // Options for pathfinding
    struct PathfindingOptions {
        uint32_t mapId;
        MovementType movementType;
        float stepSize;
        float maxSearchDistance;
        bool allowPartialPath;
        bool smoothPath;
        
        // Humanization parameters
        float cornerCutting;       // How much to cut corners -1.0 to 1.0 (negative = more angular, positive = smoother)
        bool avoidEdges;          // Try to stay away from cliff edges (default: true)
        bool preferCenterPath;    // Prefer walking in center of walkable areas (default: true)
        
        // Obstacle avoidance
        float wallPadding;

        // Terrain-aware pathfinding
        bool avoidSteepTerrain;   // Strongly prefer flat terrain over hills/slopes
        float steepTerrainCost;   // Cost multiplier for steep terrain (higher = more avoidance)
        float maxElevationChange; // Maximum elevation change to allow between waypoints (yards)
        bool preferLowerElevation; // When multiple paths available, prefer lower elevation routes

        PathfindingOptions() : mapId(0), movementType(MovementType::WALK), stepSize(8.0f), maxSearchDistance(200.0f),
                               allowPartialPath(true), smoothPath(true), cornerCutting(0.2f), avoidEdges(true),
                               preferCenterPath(true), wallPadding(0.5f), avoidSteepTerrain(true),
                               steepTerrainCost(25.0f), maxElevationChange(5.0f), preferLowerElevation(false) {}
    };

    // Statistics for a loaded nav-mesh
    struct NavMeshStats {
        uint32_t totalTiles;
        uint32_t loadedTiles;
        uint32_t totalPolygons;
        uint32_t totalVertices;
        size_t memoryUsage;
        uint32_t currentMapId;
    };


    // Visualization settings for debugging
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