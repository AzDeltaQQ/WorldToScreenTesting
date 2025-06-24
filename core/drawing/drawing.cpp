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
    
    // Sync settings
    m_playerTracker.showPlayerArrow = showPlayerArrow;
    
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
    
    // Update all components with exception handling
    try {
        m_lineManager.Update();
        m_markerManager.Update();
        m_playerTracker.Update();
    }
    catch (...) {
        // Log error but don't crash
        static int errorCount = 0;
        if (++errorCount % 60 == 0) { // Log every second at 60 FPS
            LOG_ERROR("Exception in WorldToScreenManager::Update()");
        }
        return;
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
    if (!m_pDevice || !m_renderEngine.IsInitialized()) {
        return;
    }
    
    // Check device state before rendering
    HRESULT cooperativeLevel = m_pDevice->TestCooperativeLevel();
    if (cooperativeLevel != D3D_OK) {
        return;
    }
    
    try {
        // Save render states
        DWORD oldAlphaBlend, oldSrcBlend, oldDestBlend;
        m_pDevice->GetRenderState(D3DRS_ALPHABLENDENABLE, &oldAlphaBlend);
        m_pDevice->GetRenderState(D3DRS_SRCBLEND, &oldSrcBlend);
        m_pDevice->GetRenderState(D3DRS_DESTBLEND, &oldDestBlend);
        
        // Begin rendering
        m_renderEngine.BeginRender();
        
        // Render all components
        m_lineManager.Render();
        m_markerManager.Render();
        
        // End rendering and restore render states
        m_renderEngine.EndRender();
        m_pDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, oldAlphaBlend);
        m_pDevice->SetRenderState(D3DRS_SRCBLEND, oldSrcBlend);
        m_pDevice->SetRenderState(D3DRS_DESTBLEND, oldDestBlend);
    }
    catch (...) {
        // Log error but don't crash
        static int renderErrorCount = 0;
        if (++renderErrorCount % 60 == 0) { // Log every second at 60 FPS
            LOG_ERROR("Exception in WorldToScreenManager::Render()");
        }
        return;
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