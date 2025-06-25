#pragma once

#include <memory>
#include <string>
#include <vector>
#include "../../types.h"
#include "../../types/types.h"
#include "../../drawing/components/LineOfSightManager.h"
#include "../../drawing/components/TextureManager.h"

// Forward declarations
class ObjectManager;

namespace GUI {

    class DrawingTab {
    private:
        ObjectManager* m_objectManager;
        
        // Drawing settings
        bool m_showPlayerArrow;
        bool m_showObjectNames;
        bool m_showDistances;
        bool m_showPlayerNames;
        bool m_showUnitNames;
        bool m_showGameObjectNames;
        
        // Distance filters (separate from name filters)
        bool m_showPlayerDistances;
        bool m_showUnitDistances;
        bool m_showGameObjectDistances;
        
        // Filter settings
        float m_maxDrawDistance;
        bool m_onlyShowTargeted;
        bool m_showPlayerToTargetLine;
        
        // Style settings
        float m_textScale;
        int m_arrowSize;
        
        // Color settings
        float m_playerArrowColor[4];
        float m_textColor[4];
        float m_distanceColor[4];
        float m_lineColor[4];
        
        // Cached statistics (updated periodically, not every frame)
        int m_cachedObjectsInRange = 0;
        int m_cachedPlayersInRange = 0;
        int m_cachedUnitsInRange = 0;
        int m_cachedGameObjectsInRange = 0;
        
        // Live data for HUD
        C3Vector m_livePlayerPos;
        WGUID m_liveTargetGUID;
        
        // Line of Sight Manager
        LineOfSightManager m_losManager;
        int m_losLineId = -1;  // Track LoS line for cleanup
        
        // Texture Manager Settings
        TextureSearchSettings m_textureSettings;
        std::string m_selectedTexturePath;
        std::vector<std::string> m_filteredTextures;
        
        void UpdateStatistics();

    public:
        DrawingTab();
        ~DrawingTab() = default;
        
        void SetObjectManager(ObjectManager* objManager);
        void Render();
        void Update(float deltaTime);
        
        // Getters for the drawing system
        bool ShouldShowPlayerArrow() const { return m_showPlayerArrow; }
        bool ShouldShowObjectNames() const { return m_showObjectNames; }
        bool ShouldShowDistances() const { return m_showDistances; }
        bool ShouldShowPlayerNames() const { return m_showPlayerNames; }
        bool ShouldShowUnitNames() const { return m_showUnitNames; }
        bool ShouldShowGameObjectNames() const { return m_showGameObjectNames; }
        
        float GetMaxDrawDistance() const { return m_maxDrawDistance; }
        bool ShouldOnlyShowTargeted() const { return m_onlyShowTargeted; }
        float GetTextScale() const { return m_textScale; }
        int GetArrowSize() const { return m_arrowSize; }
        
        // Color getters
        const float* GetPlayerArrowColor() const { return m_playerArrowColor; }
        const float* GetTextColor() const { return m_textColor; }
        const float* GetDistanceColor() const { return m_distanceColor; }
        const float* GetLineColor() const { return m_lineColor; }
    };

} 