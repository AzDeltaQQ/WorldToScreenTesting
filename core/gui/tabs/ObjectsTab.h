#pragma once

#include <vector>
#include <string>
#include <memory>
#include "../../types/types.h"

// Forward declarations
class ObjectManager;
class WowObject;
class WowUnit;
class WowGameObject;

namespace GUI {

class ObjectsTab {
private:
    ObjectManager* m_objectManager;
    
    // Filter settings
    bool m_showPlayers;
    bool m_showUnits;
    bool m_showGameObjects;
    bool m_showOther;
    
    float m_maxDistance;
    std::string m_nameFilter;
    
    // Selection
    WGUID m_selectedObjectGuid;
    
    // Stats
    int m_totalObjectCount;
    int m_visibleObjectCount;
    int m_playerCount;
    int m_unitCount;
    int m_gameObjectCount;
    
    // Cached filtered objects (updated periodically)
    struct CachedObjectInfo {
        std::shared_ptr<WowObject> object;
        float distance;
        std::string displayText;
    };
    std::vector<CachedObjectInfo> m_cachedFilteredObjects;
    
    // Refresh control
    float m_refreshInterval;
    float m_timeSinceLastRefresh;
    bool m_needsRefresh;
    
    // Helper methods
    void UpdateFilteredObjects();
    void UpdateStats();
    bool PassesTypeFilter(WowObjectType type) const;
    bool PassesNameFilter(const std::string& objectName) const;
    bool PassesDistanceFilter(float distance) const;
    float CalculateDistanceToPlayer(std::shared_ptr<WowObject> obj) const;
    
    // Rendering helpers
    void RenderFilterControls();
    void RenderObjectList();
    void RenderObjectDetails();
    void RenderStatsOverlay();
    
    std::string FormatDistance(float distance) const;
    std::string FormatGUID(uint64_t guid) const;

public:
    ObjectsTab();
    ~ObjectsTab() = default;

    void SetObjectManager(ObjectManager* objManager);
    void Render();
    void Update(float deltaTime);
    void ForceStatsUpdate();
    
    // Configuration
    void SetRefreshInterval(float interval) { m_refreshInterval = interval; }
    void SetMaxDistance(float distance) { m_maxDistance = distance; m_needsRefresh = true; }
    void SetNameFilter(const std::string& filter) { m_nameFilter = filter; m_needsRefresh = true; }
    
    // Selection
    void SelectObject(const WGUID& guid);
    
    // Stats
    int GetTotalObjectCount() const { return m_totalObjectCount; }
    int GetVisibleObjectCount() const { return m_visibleObjectCount; }
    int GetPlayerCount() const { return m_playerCount; }
    int GetUnitCount() const { return m_unitCount; }
    int GetGameObjectCount() const { return m_gameObjectCount; }
};

} // namespace GUI 