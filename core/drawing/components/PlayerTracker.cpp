#include "PlayerTracker.h"
#include "../../logs/Logger.h"
#include "../../objects/ObjectManager.h"
#include "../../memory/memory.h"
#include "../../types/types.h"
#include <cmath>

PlayerTracker::PlayerTracker() 
    : m_pWorldToScreen(nullptr), m_pLineManager(nullptr), m_pMarkerManager(nullptr), m_targetLineId(-1) {
}

PlayerTracker::~PlayerTracker() {
    Cleanup();
}

void PlayerTracker::Initialize(WorldToScreenCore* pWorldToScreen, LineManager* pLineManager, MarkerManager* pMarkerManager) {
    m_pWorldToScreen = pWorldToScreen;
    m_pLineManager = pLineManager;
    m_pMarkerManager = pMarkerManager;
    m_targetLineId = -1;
}

void PlayerTracker::Cleanup() {
    m_pWorldToScreen = nullptr;
    m_pLineManager = nullptr;
    m_pMarkerManager = nullptr;
    m_targetLineId = -1;
}

int PlayerTracker::AddPlayerArrow(D3DCOLOR color, float size) {
    if (!m_pMarkerManager || !m_pWorldToScreen) {
        return -1;
    }
    
    // Get player position
    C3Vector playerPos;
    if (!m_pWorldToScreen->GetPlayerPositionSafe(playerPos)) {
        LOG_WARNING("Could not get player position for arrow");
        return -1;
    }
    
    D3DXVECTOR3 playerD3DPos(playerPos.x, playerPos.y, playerPos.z);
    
    LOG_DEBUG("Adding player arrow at position (" + std::to_string(playerPos.x) + ", " + 
             std::to_string(playerPos.y) + ", " + std::to_string(playerPos.z) + ")");
    
    return m_pMarkerManager->AddMarker(playerD3DPos, color, size, "YOU");
}

void PlayerTracker::UpdatePlayerArrow() {
    if (!showPlayerArrow || !m_pMarkerManager || !m_pWorldToScreen) {
        // If player arrow is disabled, remove any existing arrow
        const auto& markers = m_pMarkerManager->GetMarkers();
        for (const auto& marker : markers) {
            if (marker.label == "YOU") {
                m_pMarkerManager->RemoveMarker(marker.id);
                break;
            }
        }
        return;
    }
    
    // Get player position
    C3Vector playerPos;
    if (!m_pWorldToScreen->GetPlayerPositionSafe(playerPos)) {
        return;
    }
    
    D3DXVECTOR3 playerD3DPos(playerPos.x, playerPos.y, playerPos.z);
    
    // Try to update existing player arrow position
    if (!m_pMarkerManager->UpdateMarkerPosition("YOU", playerD3DPos)) {
        // Player arrow doesn't exist, create it
        m_pMarkerManager->AddMarker(playerD3DPos, playerArrowColor, playerArrowSize, "YOU");
        LOG_DEBUG("Created player arrow at position (" + std::to_string(playerPos.x) + ", " + 
                 std::to_string(playerPos.y) + ", " + std::to_string(playerPos.z) + ")");
    } else {
        // Player arrow exists, update its properties (color and size) as well
        m_pMarkerManager->UpdateMarkerProperties("YOU", playerArrowColor, playerArrowSize);
    }
}

void PlayerTracker::UpdatePlayerToTargetLine() {
    if (!m_pLineManager || !m_pWorldToScreen) return;
    
    // Check if player-to-target lines are enabled
    if (!showPlayerToTargetLine) {
        // Clear line if disabled
        if (m_targetLineId != -1) { 
            m_pLineManager->RemoveLine(m_targetLineId); 
            m_targetLineId = -1; 
        }
        return;
    }
    
    auto objMgr = ObjectManager::GetInstance();
    if (!objMgr || !objMgr->IsInitialized()) {
        // Clear line if object manager not available
        if (m_targetLineId != -1) { 
            m_pLineManager->RemoveLine(m_targetLineId); 
            m_targetLineId = -1; 
        }
        return;
    }
    
    auto player = objMgr->GetLocalPlayer();
    if (!player) {
        // Clear line if no player
        if (m_targetLineId != -1) { 
            m_pLineManager->RemoveLine(m_targetLineId); 
            m_targetLineId = -1; 
        }
        return;
    }
    
    // Read current target GUID from memory
    uint64_t tgtGuid64 = Memory::Read<uint64_t>(GameOffsets::CURRENT_TARGET_GUID_ADDR);
    WGUID tgtGuid(tgtGuid64);
    
    // Check if we have a valid target
    bool hasValidTarget = tgtGuid.IsValid();
    
    // Debug logging for target state changes
    static WGUID lastTargetGuid;
    static bool wasValid = false;
    if (tgtGuid.ToUint64() != lastTargetGuid.ToUint64() || hasValidTarget != wasValid) {
        if (hasValidTarget) {
            LOG_DEBUG("Target acquired: GUID 0x" + std::to_string(tgtGuid.ToUint64()));
        } else {
            LOG_DEBUG("Target cleared: GUID was 0x" + std::to_string(tgtGuid.ToUint64()));
        }
        lastTargetGuid = tgtGuid;
        wasValid = hasValidTarget;
    }
    
    if (hasValidTarget) {
        auto targetObj = objMgr->GetObjectByGUID(tgtGuid);
        if (targetObj) {
            // Get player position using C3Vector (correct coordinate system)
            C3Vector playerPosC3;
            if (m_pWorldToScreen->GetPlayerPositionSafe(playerPosC3)) {
                // Get target position using Vector3 (now same coordinate system) 
                Vector3 tgtPosV = targetObj->GetPosition();
                
                // Convert to D3DXVECTOR3: Both are now in the same coordinate system
                D3DXVECTOR3 pPos(playerPosC3.x, playerPosC3.y, playerPosC3.z);
                D3DXVECTOR3 tPos(tgtPosV.x, tgtPosV.y, tgtPosV.z);
                
                // Remove old line each frame to keep simple
                if (m_targetLineId != -1) m_pLineManager->RemoveLine(m_targetLineId);
                m_targetLineId = m_pLineManager->AddLine(pPos, tPos, lineColor, 2.0f, "PlayerToTarget");
            } else {
                // Failed to get player position, clear line
                if (m_targetLineId != -1) { 
                    m_pLineManager->RemoveLine(m_targetLineId); 
                    m_targetLineId = -1; 
                }
            }
        } else {
            // Target object not found, clear line
            if (m_targetLineId != -1) { 
                m_pLineManager->RemoveLine(m_targetLineId); 
                m_targetLineId = -1; 
                LOG_DEBUG("Target object not found, clearing PlayerToTarget line");
            }
        }
    } else {
        // No valid target, clear line
        if (m_targetLineId != -1) { 
            m_pLineManager->RemoveLine(m_targetLineId); 
            m_targetLineId = -1; 
            LOG_DEBUG("No valid target, clearing PlayerToTarget line");
        }
    }
}

int PlayerTracker::AddPlayerToEnemyLine(const D3DXVECTOR3& enemyPos, D3DCOLOR color) {
    if (!m_pLineManager || !m_pWorldToScreen) {
        return -1;
    }
    
    // Get player position
    C3Vector playerPos;
    if (!m_pWorldToScreen->GetPlayerPositionSafe(playerPos)) {
        return -1;
    }
    
    D3DXVECTOR3 playerD3DPos(playerPos.x, playerPos.y, playerPos.z);
    return m_pLineManager->AddLine(playerD3DPos, enemyPos, lineColor, 2.0f, "PlayerToEnemy");
}

void PlayerTracker::Update() {
    // Update player arrow position
    UpdatePlayerArrow();
    
    // Update player-to-target line
    UpdatePlayerToTargetLine();
} 