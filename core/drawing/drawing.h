#pragma once

#include "../../types.h"
#include "components/WorldToScreenCore.h"
#include "components/RenderEngine.h"
#include "components/LineManager.h"
#include "components/MarkerManager.h"
#include "components/PlayerTracker.h"
#include "components/ObjectOverlay.h"
#include <d3d9.h>
#include <d3dx9.h>

class ObjectManager; // Forward declaration

class WorldToScreenManager {
private:
    // Modular components
    WorldToScreenCore m_worldToScreenCore;
    RenderEngine m_renderEngine;
    LineManager m_lineManager;
    MarkerManager m_markerManager;
    PlayerTracker m_playerTracker;
    ObjectOverlay m_objectOverlay;
    
    // Device reference
    LPDIRECT3DDEVICE9 m_pDevice;
    
public:
    WorldToScreenManager();
    ~WorldToScreenManager();
    
    // Initialization/cleanup
    bool Initialize(LPDIRECT3DDEVICE9 pDevice);
    void Cleanup();
    void OnDeviceLost();
    void OnDeviceReset();
    
    // Object manager setup (for object name rendering)
    void SetObjectManager(ObjectManager* pObjectManager);
    
    // Line management (delegates to LineManager)
    int AddLine(const D3DXVECTOR3& start, const D3DXVECTOR3& end, D3DCOLOR color = 0xFF00FF00, float thickness = 2.0f, const std::string& label = "");
    void RemoveLine(int id);
    void ClearAllLines();
    
    // Marker management (delegates to MarkerManager)
    int AddMarker(const D3DXVECTOR3& worldPos, D3DCOLOR color = 0xFFFF0000, float size = 10.0f, const std::string& label = "");
    void RemoveMarker(int id);
    void ClearAllMarkers();
    
    // Player functionality (delegates to PlayerTracker)
    int AddPlayerArrow(D3DCOLOR color = 0xFFFF0000, float size = 20.0f);
    void UpdatePlayerArrow();
    int AddPlayerToEnemyLine(const D3DXVECTOR3& enemyPos, D3DCOLOR color = 0xFF00FF00);
    void UpdatePlayerToEnemyLines();
    
    // Update and render
    void Update();
    void Render();
    
    // Public WorldToScreen function for GUI use (delegates to WorldToScreenCore)
    bool WorldToScreen(float worldX, float worldY, float worldZ, float* screenX, float* screenY);
    
    // Get data for external use
    const std::vector<LineData>& GetLines() const;
    const std::vector<MarkerData>& GetMarkers() const;
    
    // Settings
    bool showPlayerArrow = true;
    bool showObjectNames = false;
    bool showDistances = false;
    float maxDrawDistance = 50.0f;
    
    // Arrow color and size settings (for GUI synchronization)
    D3DCOLOR playerArrowColor = 0xFFFF0000; // Default red
    float playerArrowSize = 20.0f; // Default size
    
    // Line color settings
    D3DCOLOR lineColor = 0xFF00FF00; // Default green
    float lineThickness = 2.0f; // Default thickness
    
    // Text and distance color settings
    D3DCOLOR textColor = 0xFFFFFFFF; // Default white
    D3DCOLOR distanceColor = 0xFFCCCCCC; // Default light gray
    float textScale = 1.0f; // Default scale
    
    // Object name filters
    bool showPlayerNames = true;
    bool showUnitNames = false;
    bool showGameObjectNames = false;
    
    // Distance filters (separate from name filters)
    bool showPlayerDistances = true;
    bool showUnitDistances = false;
    bool showGameObjectDistances = false;
}; 