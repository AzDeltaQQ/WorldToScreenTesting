#pragma once

#include "../../navigation/NavigationTypes.h"
#include <vector>
#include <string>

namespace GUI {

    class NavigationTab {
    public:
        NavigationTab();
        ~NavigationTab();
        void Render();
        void Update();

    private:
        // Rendering helpers
        void RenderMapManagement();
        void RenderPathfinding();
        void RenderVisualization();
        void RenderStatistics();
        void RenderDebugInfo();
        void RenderVMapFeatures();

        // Actions
        void LoadSelectedMap();
        void UnloadSelectedMap();
        void FindPathFromInputs();
        void UpdateVisualizationSettings();
        void RefreshMapList();

        // Utilities
        bool ParseVector3FromInput(const char* input, Vector3& result);
        void SetStatusMessage(const std::string& message, bool isError = false);
        void ScanAvailableMaps();
        void DebugNavMeshInfo();
        void ListLoadedTiles();

        // Member variables
        bool m_isVisible;
        bool m_autoLoadCurrentMap;
        char m_startPosBuf[128] = "";
        char m_endPosBuf[128] = "";
        float m_searchRadius;
        std::string m_statusMessage;
        bool m_isPathfindingInProgress;
        bool m_isStatusError;
        std::string m_pathfindingStatus;

        std::vector<std::string> m_availableMaps;
        int m_selectedMapId;

        Navigation::VisualizationSettings m_visSettings;
        Navigation::PathfindingOptions m_pathfindingOptions;
        Navigation::NavMeshStats m_navMeshStats;

        // Visualization handles – IDs of lines/markers in WorldToScreen so we can remove/redraw.
        std::vector<int> m_pathLineIds;
        std::vector<int> m_waypointMarkerIds;

        // Store last computed path for Walk Path action
        Navigation::NavigationPath m_lastPath;
        bool m_hasValidPath = false;
        
        // Wall padding setting (yards)
        float m_wallPadding = 8.0f;
        
        // VMap collision testing
        char m_losStartBuf[128] = "";
        char m_losEndBuf[128] = "";
        char m_wallTestPosBuf[128] = "";
        char m_wallTestDirBuf[128] = "";
        float m_wallTestMaxDist = 10.0f;
    };

} // namespace GUI 