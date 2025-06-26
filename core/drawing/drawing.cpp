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
    
    // Initialize Line of Sight manager
    if (!m_losManager.Initialize()) {
        LOG_WARNING("Failed to initialize LineOfSightManager - LoS features will be disabled");
    }
    
    // Initialize Texture manager
    if (!m_textureManager.Initialize(&m_worldToScreenCore, &m_renderEngine, pDevice)) {
        LOG_WARNING("Failed to initialize TextureManager - texture features will be disabled");
    }
    
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
    m_textureManager.Cleanup();
    m_losManager.Shutdown();
    m_playerTracker.Cleanup();
    m_markerManager.Cleanup();
    m_lineManager.Cleanup();
    m_renderEngine.Cleanup();
    m_worldToScreenCore.Cleanup();
    m_objectOverlay.Cleanup();
    
    m_pDevice = nullptr;
}

void WorldToScreenManager::OnDeviceLost() {
    m_textureManager.OnDeviceLost();
    m_renderEngine.OnDeviceLost();
    m_worldToScreenCore.OnDeviceLost();
}

void WorldToScreenManager::OnDeviceReset() {
    m_textureManager.OnDeviceReset();
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

// Player position access (delegates to WorldToScreenCore)
bool WorldToScreenManager::GetPlayerPositionSafe(C3Vector& playerPos) {
    return m_worldToScreenCore.GetPlayerPositionSafe(playerPos);
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
    m_playerTracker.showPlayerToTargetLine = showPlayerToTargetLine;
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
    
    try {
        m_textureManager.Update();
    } catch (...) {
        LOG_WARNING("TextureManager update failed");
    }
    
    // Update Line of Sight manager
    try {
        if (m_losManager.IsInitialized()) {
            // Get current player position for LoS updates (use same method as other systems)
            C3Vector playerPosC3;
            if (!GetPlayerPositionSafe(playerPosC3)) {
                // If we can't get a valid player position, skip LoS updates
                return;
            }
            // Convert to Vector3 only for LoS manager update
            Vector3 playerPos(playerPosC3.x, playerPosC3.y, playerPosC3.z);
            static float deltaTime = 1.0f / 60.0f; // Approximate frame time
            m_losManager.Update(deltaTime, playerPos);
            
            // Add LoS lines to the rendering system if enabled
            if (m_losManager.GetSettings().enableLoSChecks && m_losManager.GetSettings().showLoSLines) {
                // Clear existing LoS lines (identified by label prefix)
                static std::vector<int> losLineIds;
                for (int id : losLineIds) {
                    m_lineManager.RemoveLine(id);
                }
                losLineIds.clear();
                
                // Create LoS line to current target (similar to how PlayerTracker works)
                auto objMgr = ObjectManager::GetInstance();
                if (objMgr && objMgr->IsInitialized()) {
                    // Read current target GUID from memory (same method as PlayerTracker)
                    uint64_t tgtGuid64 = Memory::Read<uint64_t>(GameOffsets::CURRENT_TARGET_GUID_ADDR);
                    WGUID tgtGuid(tgtGuid64);
                    
                    if (tgtGuid.IsValid()) {
                        auto targetObj = objMgr->GetObjectByGUID(tgtGuid);
                        if (targetObj) {
                            // Get target position - use head position for units, feet for others
                            Vector3 tgtPosV;
                            auto targetUnit = std::dynamic_pointer_cast<WowUnit>(targetObj);
                            if (targetUnit) {
                                // Use head position for units for head-to-head LoS
                                tgtPosV = targetUnit->GetHeadPosition();
                            } else {
                                // Use feet position for non-units (GameObjects, etc.)
                                tgtPosV = targetObj->GetPosition();
                            }
                            
                            // Convert to D3DXVECTOR3: Both are now in the same coordinate system
                            D3DXVECTOR3 pPos(playerPosC3.x, playerPosC3.y, playerPosC3.z);
                            D3DXVECTOR3 tPos(tgtPosV.x, tgtPosV.y, tgtPosV.z);
                            
                            // Perform LoS check using Vector3 for the LoS system (head to head for units)
                            Vector3 playerHeadPos(playerPosC3.x, playerPosC3.y, playerPosC3.z + 2.5f); // Player eye level
                            Vector3 targetPosForLoS(tgtPosV.x, tgtPosV.y, tgtPosV.z);
                            auto losResult = m_losManager.CheckLineOfSight(playerHeadPos, targetPosForLoS, false);
                            
                            if (losResult.isValid) {
                                // Create line from player head to target head (for units) or target feet (for objects)
                                D3DXVECTOR3 start = pPos;
                                start.z += 2.5f;  // Player eye level (2.5 units above feet)
                                D3DXVECTOR3 end = tPos;  // Target position (already head for units, feet for objects)
                                
                                D3DCOLOR lineColor;
                                
                                auto& settings = m_losManager.GetSettings();
                                if (losResult.isBlocked) {
                                    // Red line to target (blocked LoS)
                                    BYTE r = (BYTE)(settings.blockedLoSColor[0] * 255.0f);
                                    BYTE g = (BYTE)(settings.blockedLoSColor[1] * 255.0f);
                                    BYTE b = (BYTE)(settings.blockedLoSColor[2] * 255.0f);
                                    BYTE a = (BYTE)(settings.blockedLoSColor[3] * 255.0f);
                                    lineColor = (a << 24) | (r << 16) | (g << 8) | b;
                                } else {
                                    // Green line to target (clear LoS)
                                    BYTE r = (BYTE)(settings.clearLoSColor[0] * 255.0f);
                                    BYTE g = (BYTE)(settings.clearLoSColor[1] * 255.0f);
                                    BYTE b = (BYTE)(settings.clearLoSColor[2] * 255.0f);
                                    BYTE a = (BYTE)(settings.clearLoSColor[3] * 255.0f);
                                    lineColor = (a << 24) | (r << 16) | (g << 8) | b;
                                }
                                
                                std::string label = "LoS_Target_" + std::to_string(tgtGuid.ToUint64());
                                int lineId = m_lineManager.AddLine(start, end, lineColor, m_losManager.GetSettings().losLineWidth, label);
                                losLineIds.push_back(lineId);
                            }
                        }
                    }
                }
            }
        }
    } catch (...) {
        LOG_WARNING("LineOfSightManager update failed");
    }
}

void WorldToScreenManager::Render() {
    if (!m_pDevice) return;
    
    try {
        // Render all components in order
        m_lineManager.Render();
        m_markerManager.Render();
        m_objectOverlay.Render(); // Render object names and distances
        m_textureManager.Render(); // Render textures
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