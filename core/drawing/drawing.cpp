#include "drawing.h"
#include "../objects/ObjectManager.h"
#include "../objects/WowObject.h"
#include "../logs/Logger.h"
#include "../memory/memory.h"
#include "../types/types.h"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <Windows.h>

// Global instance
WorldToScreenManager g_WorldToScreenManager;

WorldToScreenManager::WorldToScreenManager() : m_pDevice(nullptr) {
}

WorldToScreenManager::~WorldToScreenManager() {
    Cleanup();
}

bool WorldToScreenManager::Initialize(LPDIRECT3DDEVICE9 pDevice) {
    if (!pDevice) {
        LOG_ERROR("pDevice is null");
        return false;
    }
    
    m_pDevice = pDevice;
    
    // Initialize all components
    if (!m_worldToScreenCore.Initialize(pDevice)) {
        LOG_ERROR("Failed to initialize WorldToScreenCore");
        return false;
    }
    
    if (!m_renderEngine.Initialize(pDevice)) {
        LOG_ERROR("Failed to initialize RenderEngine");
        return false;
    }
    
    // Initialize managers with component references
    m_lineManager.Initialize(&m_worldToScreenCore, &m_renderEngine);
    m_markerManager.Initialize(&m_worldToScreenCore, &m_renderEngine);
    m_playerTracker.Initialize(&m_worldToScreenCore, &m_lineManager, &m_markerManager);
    m_objectOverlay.Initialize(&m_worldToScreenCore, &m_renderEngine, nullptr); // ObjectManager set later
    
    // Sync settings
    m_playerTracker.showPlayerArrow = showPlayerArrow;
    m_playerTracker.playerArrowColor = playerArrowColor;
    m_playerTracker.playerArrowSize = playerArrowSize;
    m_playerTracker.lineColor = lineColor;
    
    // Sync MarkerManager text settings
    m_markerManager.textColor = textColor;
    m_markerManager.textScale = textScale;
    
    // Sync ObjectOverlay settings
    m_objectOverlay.showObjectNames = showObjectNames;
    m_objectOverlay.showDistances = showDistances;
    m_objectOverlay.showPlayerNames = showPlayerNames;
    m_objectOverlay.showUnitNames = showUnitNames;
    m_objectOverlay.showGameObjectNames = showGameObjectNames;
    m_objectOverlay.showPlayerDistances = showPlayerDistances;
    m_objectOverlay.showUnitDistances = showUnitDistances;
    m_objectOverlay.showGameObjectDistances = showGameObjectDistances;
    m_objectOverlay.maxDrawDistance = maxDrawDistance;
    m_objectOverlay.textColor = textColor;
    m_objectOverlay.distanceColor = distanceColor;
    m_objectOverlay.textScale = textScale;
    
    LOG_INFO("WorldToScreenManager initialized successfully with modular components");
    
    // Add player arrow by default
    if (showPlayerArrow) {
        AddPlayerArrow();
    }
    
    return true;
}

void WorldToScreenManager::Cleanup() {
    m_playerTracker.Cleanup();
    m_markerManager.Cleanup();
    m_lineManager.Cleanup();
    m_renderEngine.Cleanup();
    m_worldToScreenCore.Cleanup();
    m_objectOverlay.Cleanup();
    
    m_pDevice = nullptr;
}

void WorldToScreenManager::OnDeviceLost() {
    m_renderEngine.OnDeviceLost();
    m_worldToScreenCore.OnDeviceLost();
}

void WorldToScreenManager::OnDeviceReset() {
    m_renderEngine.OnDeviceReset();
    m_worldToScreenCore.OnDeviceReset();
}

void WorldToScreenManager::SetObjectManager(ObjectManager* pObjectManager) {
    m_objectOverlay.Initialize(&m_worldToScreenCore, &m_renderEngine, pObjectManager);
}

// Line management (delegates to LineManager)
int WorldToScreenManager::AddLine(const D3DXVECTOR3& start, const D3DXVECTOR3& end, D3DCOLOR color, float thickness, const std::string& label) {
    return m_lineManager.AddLine(start, end, color, thickness, label);
}

void WorldToScreenManager::RemoveLine(int id) {
    m_lineManager.RemoveLine(id);
}

void WorldToScreenManager::ClearAllLines() {
    m_lineManager.ClearAllLines();
}

// Marker management (delegates to MarkerManager)
int WorldToScreenManager::AddMarker(const D3DXVECTOR3& worldPos, D3DCOLOR color, float size, const std::string& label) {
    return m_markerManager.AddMarker(worldPos, color, size, label);
}

void WorldToScreenManager::RemoveMarker(int id) {
    m_markerManager.RemoveMarker(id);
}

void WorldToScreenManager::ClearAllMarkers() {
    m_markerManager.ClearAllMarkers();
}

// Player functionality (delegates to PlayerTracker)
int WorldToScreenManager::AddPlayerArrow(D3DCOLOR color, float size) {
    return m_playerTracker.AddPlayerArrow(color, size);
}

void WorldToScreenManager::UpdatePlayerArrow() {
    m_playerTracker.UpdatePlayerArrow();
}

int WorldToScreenManager::AddPlayerToEnemyLine(const D3DXVECTOR3& enemyPos, D3DCOLOR color) {
    return m_playerTracker.AddPlayerToEnemyLine(enemyPos, color);
}

void WorldToScreenManager::UpdatePlayerToEnemyLines() {
    // This is handled automatically in PlayerTracker::Update()
}

// Public WorldToScreen function for GUI use (delegates to WorldToScreenCore)
bool WorldToScreenManager::WorldToScreen(float worldX, float worldY, float worldZ, float* screenX, float* screenY) {
    return m_worldToScreenCore.WorldToScreen(worldX, worldY, worldZ, screenX, screenY);
}

// Get data for external use
const std::vector<LineData>& WorldToScreenManager::GetLines() const {
    return m_lineManager.GetLines();
}

const std::vector<MarkerData>& WorldToScreenManager::GetMarkers() const {
    return m_markerManager.GetMarkers();
}

void WorldToScreenManager::Update() {
    // Safety check - don't update if device is not valid
    if (!m_pDevice) {
        return;
    }
    
    // Check device state
    HRESULT cooperativeLevel = m_pDevice->TestCooperativeLevel();
    if (cooperativeLevel != D3D_OK) {
        return;
    }
    
    static int updateCount = 0;
    updateCount++;
    
    // Sync settings
    m_playerTracker.showPlayerArrow = showPlayerArrow;
    m_playerTracker.playerArrowColor = playerArrowColor;
    m_playerTracker.playerArrowSize = playerArrowSize;
    m_playerTracker.lineColor = lineColor;
    
    // Sync MarkerManager text settings
    m_markerManager.textColor = textColor;
    m_markerManager.textScale = textScale;
    
    // Sync ObjectOverlay settings
    m_objectOverlay.showObjectNames = showObjectNames;
    m_objectOverlay.showDistances = showDistances;
    m_objectOverlay.showPlayerNames = showPlayerNames;
    m_objectOverlay.showUnitNames = showUnitNames;
    m_objectOverlay.showGameObjectNames = showGameObjectNames;
    m_objectOverlay.showPlayerDistances = showPlayerDistances;
    m_objectOverlay.showUnitDistances = showUnitDistances;
    m_objectOverlay.showGameObjectDistances = showGameObjectDistances;
    m_objectOverlay.maxDrawDistance = maxDrawDistance;
    m_objectOverlay.textColor = textColor;
    m_objectOverlay.distanceColor = distanceColor;
    m_objectOverlay.textScale = textScale;
    
    // Update all components with exception handling
    try {
        m_lineManager.Update();
    } catch (...) {
        LOG_WARNING("LineManager update failed");
    }
    
    try {
        m_markerManager.Update();
    } catch (...) {
        LOG_WARNING("MarkerManager update failed");
    }
    
    try {
        m_playerTracker.Update();
    } catch (...) {
        LOG_WARNING("PlayerTracker update failed");
    }
    
    try {
        m_objectOverlay.Update();
    } catch (...) {
        LOG_WARNING("ObjectOverlay update failed");
    }
    
    // Debug output every 600 updates (about once per 10 seconds at 60 FPS)
    if (updateCount % 600 == 0) {
        int visibleMarkers = m_markerManager.GetVisibleCount();
        LOG_DEBUG("Update #" + std::to_string(updateCount) + ": " + 
                 std::to_string(m_markerManager.GetMarkerCount()) + " markers total, " + 
                 std::to_string(visibleMarkers) + " visible, " +
                 std::to_string(m_lineManager.GetLineCount()) + " lines");
    }
}

void WorldToScreenManager::Render() {
    if (!m_pDevice) return;
    
    try {
        // Render all components in order
        m_lineManager.Render();
        m_markerManager.Render();
        m_objectOverlay.Render(); // Render object names and distances
    } catch (...) {
        // Log error but don't crash
        static int errorCount = 0;
        if (++errorCount % 60 == 0) { // Log every second at 60 FPS
            LOG_ERROR("Exception in WorldToScreenManager::Render()");
        }
    }
}

// Global functions for easy access
extern "C" {
    __declspec(dllexport) bool InitializeWorldToScreen(LPDIRECT3DDEVICE9 pDevice) {
        return g_WorldToScreenManager.Initialize(pDevice);
    }
    
    __declspec(dllexport) void CleanupWorldToScreen() {
        g_WorldToScreenManager.Cleanup();
    }
    
    __declspec(dllexport) void OnWorldToScreenDeviceLost() {
        g_WorldToScreenManager.OnDeviceLost();
    }
    
    __declspec(dllexport) void OnWorldToScreenDeviceReset() {
        g_WorldToScreenManager.OnDeviceReset();
    }
    
    __declspec(dllexport) int AddWorldToScreenLine(float startX, float startY, float startZ, 
                                                   float endX, float endY, float endZ, 
                                                   unsigned long color, float thickness) {
        D3DXVECTOR3 start(startX, startY, startZ);
        D3DXVECTOR3 end(endX, endY, endZ);
        return g_WorldToScreenManager.AddLine(start, end, color, thickness);
    }
    
    __declspec(dllexport) int AddPlayerToEnemyLine(float enemyX, float enemyY, float enemyZ, unsigned long color) {
        D3DXVECTOR3 enemyPos(enemyX, enemyY, enemyZ);
        return g_WorldToScreenManager.AddPlayerToEnemyLine(enemyPos, color);
    }
    
    __declspec(dllexport) void RemoveWorldToScreenLine(int id) {
        g_WorldToScreenManager.RemoveLine(id);
    }
    
    __declspec(dllexport) void ClearAllWorldToScreenLines() {
        g_WorldToScreenManager.ClearAllLines();
    }
    
    __declspec(dllexport) int AddWorldToScreenMarker(float x, float y, float z, unsigned long color, float size) {
        D3DXVECTOR3 pos(x, y, z);
        return g_WorldToScreenManager.AddMarker(pos, color, size);
    }
    
    __declspec(dllexport) void RemoveWorldToScreenMarker(int id) {
        g_WorldToScreenManager.RemoveMarker(id);
    }
    
    __declspec(dllexport) void UpdateWorldToScreen() {
        g_WorldToScreenManager.Update();
    }
    
    __declspec(dllexport) void RenderWorldToScreen() {
        g_WorldToScreenManager.Render();
    }
} 