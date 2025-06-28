#include "NavigationTab.h"
#include "../../navigation/NavigationManager.h"
#include "../../logs/Logger.h"
#include "../../../dependencies/ImGui/imgui.h"
#include "../../movement/MovementController.h"
#include "../../drawing/drawing.h"

#include <filesystem>
#include <sstream>
#include <limits>

extern WorldToScreenManager g_WorldToScreenManager;

namespace GUI {

    NavigationTab::NavigationTab()
        : m_isVisible(true)
        , m_autoLoadCurrentMap(false)
        , m_selectedMapIndex(-1)
        , m_searchRadius(50.0f)
        , m_statusMessage("Ready")
        , m_isPathfindingInProgress(false)
        , m_isStatusError(false)
        , m_wallPadding(8.0f)
    {
        // Initialize input buffers
        memset(m_startPosBuf, 0, sizeof(m_startPosBuf));
        memset(m_endPosBuf, 0, sizeof(m_endPosBuf));
        
        // Set default values
        strcpy_s(m_startPosBuf, "0.0, 0.0, 0.0");
        strcpy_s(m_endPosBuf, "10.0, 0.0, 10.0");
        
        // Initialize settings
        m_visSettings = Navigation::VisualizationSettings();
        m_pathfindingOptions = Navigation::PathfindingOptions();
        
        if (Navigation::NavigationManager::Instance().IsInitialized()) {
            SetStatusMessage("NavigationManager initialized successfully", false);
            RefreshMapList();
        } else {
            SetStatusMessage("NavigationManager not initialized", true);
        }
    }

    NavigationTab::~NavigationTab() {
    }

    void NavigationTab::Render() {
        if (!m_isVisible) {
            return;
        }

        // Status bar at the top
        ImGui::Text("Navigation System - Status: %s", m_statusMessage.c_str());
        ImGui::Separator();
        
        // Organize content in collapsible sections instead of tabs
        if (ImGui::CollapsingHeader("Map Management", ImGuiTreeNodeFlags_DefaultOpen)) {
            RenderMapManagement();
            ImGui::Spacing();
        }
        
        if (ImGui::CollapsingHeader("Pathfinding", ImGuiTreeNodeFlags_DefaultOpen)) {
            RenderPathfinding();
            ImGui::Spacing();
        }
        
        if (ImGui::CollapsingHeader("Visualization", ImGuiTreeNodeFlags_DefaultOpen)) {
            RenderVisualization();
            ImGui::Spacing();
        }
        
        if (ImGui::CollapsingHeader("Statistics")) {
            RenderStatistics();
            ImGui::Spacing();
        }
        
        if (ImGui::CollapsingHeader("Debug")) {
            RenderDebugInfo();
        }

        ImGui::Separator();
        ImGui::Text("Path Refinement");
        
        ImGui::SliderFloat("Wall Padding (yd)", &m_wallPadding, 0.0f, 15.0f, "%.2f");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Minimum distance path should keep from walls/obstacles. Set to 0 to disable.");
        }
        
        ImGui::Separator();
        ImGui::Text("Terrain Avoidance");
        
        ImGui::Checkbox("Avoid Steep Terrain", &m_pathfindingOptions.avoidSteepTerrain);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Strongly prefer flat terrain over hills and slopes to prevent falling off paths.");
        }
        
        if (m_pathfindingOptions.avoidSteepTerrain) {
            ImGui::SliderFloat("Steep Terrain Cost", &m_pathfindingOptions.steepTerrainCost, 10.0f, 500.0f, "%.1f");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Higher values make pathfinding avoid steep terrain more aggressively. Default: 100.0. Use 200+ for very hilly areas.");
            }
            
            ImGui::SliderFloat("Max Elevation Change (yd)", &m_pathfindingOptions.maxElevationChange, 5.0f, 30.0f, "%.1f");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Maximum elevation change allowed between waypoints. Larger changes will be smoothed with intermediate waypoints.");
            }
            
            ImGui::Checkbox("Prefer Lower Elevation", &m_pathfindingOptions.preferLowerElevation);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("When multiple paths are available, prefer routes that stay at lower elevations.");
            }
        }

        ImGui::Separator();
        RenderVMapFeatures();
        ImGui::Separator();
    }

    void NavigationTab::Update() {
        static bool first_run = true;
        if (first_run && Navigation::NavigationManager::Instance().IsInitialized()) {
            RefreshMapList();
            first_run = false;
        }
    }

    void NavigationTab::RenderMapManagement() {
        ImGui::Text("Map Management");

        if (m_availableMaps.empty()) {
            ImGui::Text("No map files found.");
        } else {
            // Determine the display name for the currently selected map
            const char* current_map_name = "Select a map";
            if (m_selectedMapIndex != -1 && m_selectedMapIndex < m_availableMaps.size()) {
                // Format: "000 - Eastern Kingdom"
                static std::string displayName;
                const auto& mapPair = m_availableMaps[m_selectedMapIndex];
                displayName = std::to_string(mapPair.first) + " - " + mapPair.second;
                current_map_name = displayName.c_str();
            }

            if (ImGui::BeginCombo("Map", current_map_name)) {
                for (int i = 0; i < m_availableMaps.size(); ++i) {
                    const bool is_selected = (m_selectedMapIndex == i);
                    const auto& mapPair = m_availableMaps[i];
                    std::string displayText = std::to_string(mapPair.first) + " - " + mapPair.second;
                    
                    if (ImGui::Selectable(displayText.c_str(), is_selected)) {
                        m_selectedMapIndex = i;
                    }
                    if (is_selected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }
        }

        if (ImGui::Button("Load Map")) {
            LoadSelectedMap();
        }
        ImGui::SameLine();
        if (ImGui::Button("Unload Map")) {
            UnloadSelectedMap();
        }
        ImGui::SameLine();
        if (ImGui::Button("Unload All")) {
            Navigation::NavigationManager::Instance().UnloadAllMaps();
            SetStatusMessage("All maps unloaded", false);
            m_navMeshStats = {}; // Reset stats
        }
        ImGui::SameLine();
        if (ImGui::Button("Refresh List")) {
            RefreshMapList();
        }
    }

    void NavigationTab::RenderPathfinding() {
        ImGui::Text("Pathfinding");

        // Start position
        ImGui::InputText("Start Position", m_startPosBuf, sizeof(m_startPosBuf));
        ImGui::SameLine();
        if (ImGui::Button("Use Current##Start")) {
            Vector3 currentPos;
            if (MovementController::GetInstance().GetPlayerPosition(currentPos)) {
                snprintf(m_startPosBuf, sizeof(m_startPosBuf), "%.3f, %.3f, %.3f", currentPos.x, currentPos.y, currentPos.z);
            }
        }

        // End position
        ImGui::InputText("End Position", m_endPosBuf, sizeof(m_endPosBuf));
        ImGui::SameLine();
        if (ImGui::Button("Use Current##End")) {
            Vector3 currentPos;
            if (MovementController::GetInstance().GetPlayerPosition(currentPos)) {
                snprintf(m_endPosBuf, sizeof(m_endPosBuf), "%.3f, %.3f, %.3f", currentPos.x, currentPos.y, currentPos.z);
            }
        }

        if (ImGui::Button("Find Path")) {
            FindPathFromInputs();
        }

        ImGui::SameLine();
        if (ImGui::Button("Walk Path")) {
            if (!m_hasValidPath || m_lastPath.waypoints.size() < 2) {
                SetStatusMessage("No valid path to walk. Compute a path first.", true);
            } else {
                // Determine the waypoint that is currently closest to the player and start walking from that point.
                Vector3 playerPos;
                auto& mover = MovementController::GetInstance();

                if (!mover.GetPlayerPosition(playerPos)) {
                    SetStatusMessage("Unable to obtain player position.", true);
                    return;
                }

                size_t closestIndex = 0;
                float closestDist   = std::numeric_limits<float>::max();
                for (size_t i = 0; i < m_lastPath.waypoints.size(); ++i) {
                    float d = m_lastPath.waypoints[i].position.Distance(playerPos);
                    if (d < closestDist) {
                        closestDist = d;
                        closestIndex = i;
                    }
                }

                // If we are already very close to the final waypoint, nothing to do.
                const float DEST_EPSILON = 1.5f; // yards
                if (playerPos.Distance(m_lastPath.waypoints.back().position) <= DEST_EPSILON) {
                    SetStatusMessage("Already at destination.", false);
                    return;
                }

                // Queue movement commands from the closest waypoint onward (skip the point we are already near).
                size_t segmentsQueued = 0;
                // Optionally skip the first target if we are already very close (<= 1 yd)
                size_t firstTargetIdx = closestIndex;
                if (closestDist <= 1.0f && (closestIndex + 1) < m_lastPath.waypoints.size()) {
                    firstTargetIdx = closestIndex + 1;
                }

                for (size_t i = firstTargetIdx; i < m_lastPath.waypoints.size(); ++i) {
                    mover.ClickToMove(m_lastPath.waypoints[i].position);
                    ++segmentsQueued;
                }

                SetStatusMessage("Walking along path (" + std::to_string(segmentsQueued) + " segments)", false);
            }
        }

        ImGui::SameLine();
        ImGui::Text("%s", m_pathfindingStatus.c_str());

        if (!m_statusMessage.empty()) {
            ImVec4 color = m_isStatusError ? ImVec4(1.0f, 0.0f, 0.0f, 1.0f) : ImVec4(0.0f, 1.0f, 0.0f, 1.0f);
            ImGui::TextColored(color, "%s", m_statusMessage.c_str());
        }
    }

    void NavigationTab::RenderVisualization() {
        ImGui::Text("Visualization Settings");
        ImGui::Separator();
        
        auto& navManager = Navigation::NavigationManager::Instance();
        auto settings = navManager.GetVisualizationSettings(); // Make a copy, not a const reference
        bool changed = false;
        
        changed |= ImGui::Checkbox("Enable Visualization", &settings.enabled);
        if (settings.enabled) {
            changed |= ImGui::Checkbox("Show NavMesh", &settings.showNavMesh);
            changed |= ImGui::Checkbox("Show Tile Bounds", &settings.showTileBounds);
            changed |= ImGui::Checkbox("Show Paths", &settings.showPaths);
            changed |= ImGui::Checkbox("Show Waypoints", &settings.showWaypoints);
            changed |= ImGui::Checkbox("Show Debug Info", &settings.showDebugInfo);
            changed |= ImGui::SliderFloat("Mesh Opacity", &settings.meshOpacity, 0.1f, 1.0f);
        }
        
        if (changed) {
            navManager.SetVisualizationSettings(settings);
        }

        // Add navigation mesh debugging info
        if (ImGui::Button("Debug NavMesh Info")) {
            DebugNavMeshInfo();
        }
        ImGui::SameLine();
        if (ImGui::Button("List Loaded Tiles")) {
            ListLoadedTiles();
        }
    }

    void NavigationTab::RenderStatistics() {
        ImGui::Text("Navigation Statistics");
        ImGui::Separator();
        
        if (!Navigation::NavigationManager::Instance().IsInitialized()) {
            ImGui::Text("NavigationManager not initialized");
            return;
        }
        
        auto stats = Navigation::NavigationManager::Instance().GetNavMeshStats();
        std::vector<Navigation::MapTile> loadedTiles = Navigation::NavigationManager::Instance().GetLoadedTiles();
        
        ImGui::Text("Current Map ID: %u", stats.currentMapId);
        ImGui::Text("Total Tiles: %u", stats.totalTiles);
        ImGui::Text("Loaded Tiles: %u", stats.loadedTiles);
        ImGui::Text("Total Polygons: %u", stats.totalPolygons);
        ImGui::Text("Total Vertices: %u", stats.totalVertices);
        ImGui::Text("Memory Usage: %zu bytes", stats.memoryUsage);
        
        ImGui::Spacing();
        
        ImGui::Text("Loaded Tiles Details:");
        if (loadedTiles.empty()) {
            ImGui::Text("No tiles loaded");
        } else {
            if (ImGui::BeginTable("TilesTable", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
                ImGui::TableSetupColumn("Map ID");
                ImGui::TableSetupColumn("Tile X");
                ImGui::TableSetupColumn("Tile Y");
                ImGui::TableSetupColumn("Status");
                ImGui::TableHeadersRow();
                
                for (const auto& tile : loadedTiles) {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::Text("%u", tile.mapId);
                    ImGui::TableSetColumnIndex(1);
                    ImGui::Text("%u", tile.tileX);
                    ImGui::TableSetColumnIndex(2);
                    ImGui::Text("%u", tile.tileY);
                    ImGui::TableSetColumnIndex(3);
                    ImGui::Text("%s", tile.loaded ? "Loaded" : "Failed");
                }
                ImGui::EndTable();
            }
        }
    }

    void NavigationTab::RenderDebugInfo() {
        ImGui::Text("Debug Information");
        ImGui::Separator();
        
        ImGui::Text("Navigation Manager State:");
        ImGui::Text("- Initialized: %s", Navigation::NavigationManager::Instance().IsInitialized() ? "Yes" : "No");
        ImGui::Text("- Current Map: %u", Navigation::NavigationManager::Instance().GetCurrentMapId());
        
        ImGui::Spacing();
        
        ImGui::Text("Last Status: %s", m_statusMessage.c_str());
        ImGui::Text("Pathfinding in Progress: %s", m_isPathfindingInProgress ? "Yes" : "No");
        
        ImGui::Spacing();
        
        ImGui::Text("Available Maps: %zu", m_availableMaps.size());
        for (const auto& mapPair : m_availableMaps) {
            ImGui::Text("- Map %u: %s", mapPair.first, mapPair.second.c_str());
        }
        
        ImGui::Spacing();
        
        if (ImGui::Button("Force Refresh Status")) {
            RefreshMapList();
            SetStatusMessage("Status refreshed");
        }
    }

    void NavigationTab::LoadSelectedMap() {
        if (m_selectedMapIndex != -1 && m_selectedMapIndex < m_availableMaps.size()) {
            const auto& mapPair = m_availableMaps[m_selectedMapIndex];
            uint32_t mapId = mapPair.first;
            const std::string& mapName = mapPair.second;
            
            LOG_INFO("[NavigationTab] Loading map " + std::to_string(mapId) + " (" + mapName + ")...");
            if (Navigation::NavigationManager::Instance().LoadMapNavMesh(static_cast<int>(mapId))) {
                SetStatusMessage("Map " + std::to_string(mapId) + " (" + mapName + ") loaded successfully", false);
                m_navMeshStats = Navigation::NavigationManager::Instance().GetNavMeshStats(mapId);
            } else {
                SetStatusMessage("Failed to load map " + std::to_string(mapId) + " (" + mapName + ")", true);
            }
        } else {
            SetStatusMessage("Please select a map to load", true);
        }
    }

    void NavigationTab::UnloadSelectedMap() {
        if (m_selectedMapIndex != -1 && m_selectedMapIndex < m_availableMaps.size()) {
            const auto& mapPair = m_availableMaps[m_selectedMapIndex];
            uint32_t mapId = mapPair.first;
            const std::string& mapName = mapPair.second;
            
            Navigation::NavigationManager::Instance().UnloadMap(mapId);
            SetStatusMessage("Map " + std::to_string(mapId) + " (" + mapName + ") unloaded.", false);
            m_navMeshStats = {}; // Reset stats
        } else {
            SetStatusMessage("Please select a map to unload", true);
        }
    }

    void NavigationTab::UpdateVisualizationSettings() {
        auto& navManager = Navigation::NavigationManager::Instance();
        navManager.SetVisualizationSettings(m_visSettings);
        SetStatusMessage("Visualization settings updated.", false);
    }

    void NavigationTab::RefreshMapList() {
        ScanAvailableMaps();
        SetStatusMessage("Map list refreshed", false);
    }

    void NavigationTab::ScanAvailableMaps() {
        m_availableMaps = Navigation::NavigationManager::GetAllMapNames();
        LOG_INFO("[NavigationTab] Found " + std::to_string(m_availableMaps.size()) + " available maps");
    }

    bool NavigationTab::ParseVector3FromInput(const char* input, Vector3& result) {
        std::string s(input);
        std::replace(s.begin(), s.end(), ',', ' ');
        std::istringstream iss(s);
        if (!(iss >> result.x >> result.y >> result.z)) {
            return false;
        }
        return true;
    }

    void NavigationTab::SetStatusMessage(const std::string& message, bool isError) {
        m_statusMessage = message;
        m_isStatusError = isError;
        if (isError) {
            LOG_ERROR("[NavigationTab] " + message);
        } else {
            LOG_INFO("[NavigationTab] " + message);
        }
    }

    void NavigationTab::DebugNavMeshInfo() {
        auto& navManager = Navigation::NavigationManager::Instance();
        auto stats = navManager.GetNavMeshStats();
        
        LOG_INFO("=== NavMesh Debug Info ===");
        LOG_INFO("Current Map ID: " + std::to_string(stats.currentMapId));
        LOG_INFO("Total Tiles: " + std::to_string(stats.totalTiles));
        LOG_INFO("Loaded Tiles: " + std::to_string(stats.loadedTiles));
        LOG_INFO("Total Polygons: " + std::to_string(stats.totalPolygons));
        LOG_INFO("Total Vertices: " + std::to_string(stats.totalVertices));
        LOG_INFO("Memory Usage: " + std::to_string(stats.memoryUsage / (1024 * 1024)) + " MB");
        
        SetStatusMessage("NavMesh debug info logged. Check console for details.");
    }

    void NavigationTab::ListLoadedTiles() {
        auto& navManager = Navigation::NavigationManager::Instance();
        auto stats = navManager.GetNavMeshStats();
        
        LOG_INFO("=== Loaded Tiles for Map " + std::to_string(stats.currentMapId) + " ===");
        LOG_INFO("Found " + std::to_string(stats.loadedTiles) + " loaded tiles with " + std::to_string(stats.totalPolygons) + " total polygons");
        
        // If no polygons, the navigation mesh might be empty or not loaded correctly
        if (stats.totalPolygons == 0) {
            LOG_WARNING("No polygons found in navigation mesh! Check if VMAP data was included during generation.");
        }
        
        SetStatusMessage("Tile list logged. Check console for details.");
    }

    void NavigationTab::FindPathFromInputs() {
        Vector3 startPos, endPos;
        if (!ParseVector3FromInput(m_startPosBuf, startPos)) {
            SetStatusMessage("Invalid start position format. Use 'x, y, z'", true);
            return;
        }
        if (!ParseVector3FromInput(m_endPosBuf, endPos)) {
            SetStatusMessage("Invalid end position format. Use 'x, y, z'", true);
            return;
        }

        // Configure pathfinding options with humanization settings
        m_pathfindingOptions.cornerCutting = 0.0f; // Disable shaping
        m_pathfindingOptions.wallPadding = m_wallPadding;

        LOG_INFO("[NavigationTab] Finding path from (" +
                 std::to_string(startPos.x) + ", " + std::to_string(startPos.y) + ", " + std::to_string(startPos.z) +
                 ") to (" + std::to_string(endPos.x) + ", " + std::to_string(endPos.y) + ", " + std::to_string(endPos.z) + ")");
        Navigation::NavigationPath path;
        auto& navManager = Navigation::NavigationManager::Instance();

        Navigation::PathResult result = navManager.FindPath(startPos, endPos, path, m_pathfindingOptions);
        
        if (result == Navigation::PathResult::SUCCESS) {
            SetStatusMessage("Path found successfully! Waypoints: " + std::to_string(path.waypoints.size()), false);

            // Clear previous visualization
            for (int id : m_pathLineIds) {
                g_WorldToScreenManager.RemoveLine(id);
            }
            m_pathLineIds.clear();
            for (int id : m_waypointMarkerIds) {
                g_WorldToScreenManager.RemoveMarker(id);
            }
            m_waypointMarkerIds.clear();

            // Fetch latest visualization settings (user may have toggled)
            m_visSettings = Navigation::NavigationManager::Instance().GetVisualizationSettings();

            // Store path
            m_lastPath = path;
            m_hasValidPath = true;

            // Draw lines between consecutive waypoints if enabled
            if (m_visSettings.showPaths && path.waypoints.size() >= 2) {
                for (size_t i = 0; i + 1 < path.waypoints.size(); ++i) {
                    const auto& wpA = path.waypoints[i].position;
                    const auto& wpB = path.waypoints[i + 1].position;
                    D3DXVECTOR3 start(static_cast<float>(wpA.x), static_cast<float>(wpA.y), static_cast<float>(wpA.z));
                    D3DXVECTOR3 end  (static_cast<float>(wpB.x), static_cast<float>(wpB.y), static_cast<float>(wpB.z));
                    int lineId = g_WorldToScreenManager.AddLine(start, end, m_visSettings.pathColor, 2.5f, "PathLine");
                    m_pathLineIds.push_back(lineId);
                }
            }

            // Draw waypoint markers if enabled
            if (m_visSettings.showWaypoints) {
                for (size_t i = 0; i < path.waypoints.size(); ++i) {
                    const auto& wp = path.waypoints[i].position;
                    D3DXVECTOR3 pos(static_cast<float>(wp.x), static_cast<float>(wp.y), static_cast<float>(wp.z));
                    std::string label = "WP" + std::to_string(i);
                    int markerId = g_WorldToScreenManager.AddMarker(pos, m_visSettings.waypointColor, 10.0f, label);
                    m_waypointMarkerIds.push_back(markerId);
                }
            }
        } else {
            m_hasValidPath = false;
            SetStatusMessage("Pathfinding failed: " + navManager.GetLastError(), true);
        }
    }

    void NavigationTab::RenderVMapFeatures() {
        ImGui::Text("VMap Collision Detection");
        
        auto& navManager = Navigation::NavigationManager::Instance();
        if (!navManager.IsInitialized()) {
            ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "NavigationManager not initialized");
            return;
        }

        // Line of Sight Testing
        ImGui::Text("Line of Sight Test");
        ImGui::InputText("LOS Start", m_losStartBuf, sizeof(m_losStartBuf));
        ImGui::SameLine();
        if (ImGui::Button("Current##LOS1")) {
            Vector3 currentPos;
            if (MovementController::GetInstance().GetPlayerPosition(currentPos)) {
                snprintf(m_losStartBuf, sizeof(m_losStartBuf), "%.3f, %.3f, %.3f", currentPos.x, currentPos.y, currentPos.z);
            }
        }

        ImGui::InputText("LOS End", m_losEndBuf, sizeof(m_losEndBuf));
        ImGui::SameLine();
        if (ImGui::Button("Current##LOS2")) {
            Vector3 currentPos;
            if (MovementController::GetInstance().GetPlayerPosition(currentPos)) {
                snprintf(m_losEndBuf, sizeof(m_losEndBuf), "%.3f, %.3f, %.3f", currentPos.x, currentPos.y, currentPos.z);
            }
        }

        if (ImGui::Button("Test Line of Sight")) {
            Vector3 startPos, endPos;
            if (ParseVector3FromInput(m_losStartBuf, startPos) && ParseVector3FromInput(m_losEndBuf, endPos)) {
                bool hasLOS = navManager.IsInLineOfSight(startPos, endPos);
                SetStatusMessage("Line of Sight: " + std::string(hasLOS ? "CLEAR" : "BLOCKED"), !hasLOS);
            } else {
                SetStatusMessage("Invalid position format. Use: x, y, z", true);
            }
        }

        ImGui::Spacing();

        // Wall Distance Testing
        ImGui::Text("Wall Distance Test");
        ImGui::InputText("Position", m_wallTestPosBuf, sizeof(m_wallTestPosBuf));
        ImGui::SameLine();
        if (ImGui::Button("Current##Wall")) {
            Vector3 currentPos;
            if (MovementController::GetInstance().GetPlayerPosition(currentPos)) {
                snprintf(m_wallTestPosBuf, sizeof(m_wallTestPosBuf), "%.3f, %.3f, %.3f", currentPos.x, currentPos.y, currentPos.z);
            }
        }

        ImGui::InputText("Direction", m_wallTestDirBuf, sizeof(m_wallTestDirBuf));
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Direction vector (e.g., 1, 0, 0 for forward)");
        }

        ImGui::SliderFloat("Max Distance", &m_wallTestMaxDist, 1.0f, 50.0f, "%.1f yards");

        if (ImGui::Button("Test Wall Distance")) {
            Vector3 position, direction;
            if (ParseVector3FromInput(m_wallTestPosBuf, position) && ParseVector3FromInput(m_wallTestDirBuf, direction)) {
                float distance = navManager.GetDistanceToWall(position, direction, m_wallTestMaxDist);
                SetStatusMessage("Distance to wall: " + std::to_string(distance) + " yards", false);
            } else {
                SetStatusMessage("Invalid position/direction format. Use: x, y, z", true);
            }
        }

        ImGui::Spacing();

        // Nearby Walls Test
        if (ImGui::Button("Find Nearby Walls")) {
            Vector3 currentPos;
            if (MovementController::GetInstance().GetPlayerPosition(currentPos)) {
                auto walls = navManager.GetNearbyWalls(currentPos, 15.0f);
                SetStatusMessage("Found " + std::to_string(walls.size()) + " nearby walls", false);
                
                // Log wall details
                for (size_t i = 0; i < walls.size(); ++i) {
                    const auto& wall = walls[i];
                    LOG_INFO("Wall " + std::to_string(i+1) + ": Distance " + std::to_string(wall.distance) + 
                            " yards at (" + std::to_string(wall.hitPoint.x) + ", " + 
                            std::to_string(wall.hitPoint.y) + ", " + std::to_string(wall.hitPoint.z) + ")");
                }
            } else {
                SetStatusMessage("Could not get player position", true);
            }
        }

        ImGui::Spacing();
    }
} 