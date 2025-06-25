#include "WorldToScreenCore.h"
#include "../../logs/Logger.h"
#include "../../memory/memory.h"
#include "../../types/types.h"
#include <cmath>

WorldToScreenCore::WorldToScreenCore() : m_pDevice(nullptr) {
}

WorldToScreenCore::~WorldToScreenCore() {
    Cleanup();
}

bool WorldToScreenCore::Initialize(LPDIRECT3DDEVICE9 pDevice) {
    if (!pDevice) {
        LOG_ERROR("pDevice is null in WorldToScreenCore");
        return false;
    }
    
    m_pDevice = pDevice;
    return true;
}

void WorldToScreenCore::Cleanup() {
    m_pDevice = nullptr;
}

void WorldToScreenCore::OnDeviceLost() {
    // No DirectX resources to handle in core
}

void WorldToScreenCore::OnDeviceReset() {
    // No DirectX resources to handle in core
}

bool WorldToScreenCore::WorldToScreen(const D3DXVECTOR3& worldPos, D3DXVECTOR2& screenPos) {
    if (!m_pDevice) {
        // Set default screen coordinates even on failure
        screenPos.x = 0.0f;
        screenPos.y = 0.0f;
        return false;
    }
    
    // Additional safety check for device validity
    HRESULT cooperativeLevel = m_pDevice->TestCooperativeLevel();
    if (cooperativeLevel != D3D_OK) {
        screenPos.x = 0.0f;
        screenPos.y = 0.0f;
        return false;
    }
    
    try {
        // Get viewport for screen dimensions
        D3DVIEWPORT9 viewport;
        if (FAILED(m_pDevice->GetViewport(&viewport))) {
            static int failCount1 = 0;
            if (++failCount1 % 600 == 0) {
                LOG_DEBUG("WorldToScreen: GetViewport failed");
            }
            screenPos.x = 0.0f;
            screenPos.y = 0.0f;
            return false;
        }
        
        // Get WorldFrame pointer (it's a double pointer)
        CGWorldFrame** ppWorldFrame = reinterpret_cast<CGWorldFrame**>(WORLDFRAME_PTR);
        if (!ppWorldFrame || !*ppWorldFrame) {
            static int failCount2 = 0;
            if (++failCount2 % 600 == 0) {
                LOG_DEBUG("WorldToScreen: WorldFrame pointer is null");
            }
            screenPos.x = 0.0f;
            screenPos.y = 0.0f;
            return false;
        }
        
        CGWorldFrame* pWorldFrame = *ppWorldFrame;
        
        // Get camera position and matrices from WoW's data structures
        CCamera* pCamera = reinterpret_cast<CCamera*>(pWorldFrame->pActiveCamera);
        if (!pCamera) {
            static int failCount3 = 0;
            if (++failCount3 % 600 == 0) {
                LOG_DEBUG("WorldToScreen: Camera pointer is null");
            }
            screenPos.x = 0.0f;
            screenPos.y = 0.0f;
            return false;
        }
        
        // Read camera position
        D3DXVECTOR3 cameraPos(pCamera->position[0], pCamera->position[1], pCamera->position[2]);
        
        // Calculate relative position (world position relative to camera)
        D3DXVECTOR3 relativePos = worldPos - cameraPos;
        
        // Use WoW's view-projection matrix from WorldFrame+0x340
        D3DXMATRIX* pViewProjMatrix = reinterpret_cast<D3DXMATRIX*>(&pWorldFrame->viewProjectionMatrix[0]);
        
        // Transform the relative position using WoW's matrix
        D3DXVECTOR4 clipSpacePos;
        D3DXVECTOR3 homogeneousInput(relativePos.x, relativePos.y, relativePos.z);
        D3DXVec3Transform(&clipSpacePos, &homogeneousInput, pViewProjMatrix);
        
        // ALWAYS COMPUTE SCREEN COORDINATES - even for points behind camera
        bool behindCamera = false;
        bool outsideDepth = false;
        
        // Check for valid W component (avoid division by zero)
        if (clipSpacePos.w <= 0.0001f) {
            static int failCount4 = 0;
            if (++failCount4 % 600 == 0) {
                LOG_DEBUG("WorldToScreen: Point behind camera (w=" + std::to_string(clipSpacePos.w) + ")");
            }
            behindCamera = true;
            // Use a small positive value to avoid division by zero
            clipSpacePos.w = 0.001f;
        }
        
        // Perform perspective division to get normalized device coordinates (NDC)
        float ndcX = clipSpacePos.x / clipSpacePos.w;
        float ndcY = clipSpacePos.y / clipSpacePos.w;
        float ndcZ = clipSpacePos.z / clipSpacePos.w;
        
        // Check depth bounds (NDC Z should be between 0 and 1)
        if (ndcZ < 0.0f || ndcZ > 1.0f) {
            static int failCount5 = 0;
            if (++failCount5 % 600 == 0) {
                LOG_DEBUG("WorldToScreen: Point outside depth bounds (z=" + std::to_string(ndcZ) + ")");
            }
            outsideDepth = true;
        }
        
        // Convert NDC to screen coordinates - ALWAYS DO THIS
        // NDC range is [-1,1] for X and Y, convert to [0, viewport.Width] and [0, viewport.Height]
        screenPos.x = (ndcX + 1.0f) * 0.5f * viewport.Width;
        screenPos.y = (1.0f - ndcY) * 0.5f * viewport.Height; // Flip Y axis
        
        // Return true only if point is properly visible, but coordinates are ALWAYS set
        return !behindCamera && !outsideDepth;
        
    }
    catch (...) {
        static int failCount7 = 0;
        if (++failCount7 % 600 == 0) {
            LOG_DEBUG("WorldToScreen: Exception caught");
        }
        screenPos.x = 0.0f;
        screenPos.y = 0.0f;
        return false;
    }
}

bool WorldToScreenCore::WorldToScreen(float worldX, float worldY, float worldZ, float* screenX, float* screenY) {
    if (!screenX || !screenY) {
        return false;
    }
    
    D3DXVECTOR3 worldPos(worldX, worldY, worldZ);
    D3DXVECTOR2 screenPos;
    
    if (WorldToScreen(worldPos, screenPos)) {
        *screenX = screenPos.x;
        *screenY = screenPos.y;
        return true;
    }
    
    return false;
}

bool WorldToScreenCore::IsValidPlayerPosition(const C3Vector& pos) {
    // Basic validation - check if position is not zero and within reasonable bounds
    return (pos.x != 0.0f || pos.y != 0.0f || pos.z != 0.0f) &&
           abs(pos.x) < 100000.0f && abs(pos.y) < 100000.0f && abs(pos.z) < 100000.0f;
}

bool WorldToScreenCore::GetPlayerPositionSafe(C3Vector& playerPos) {
    // Simple validation without exception handling to avoid object unwinding issues
    playerPos = GetLocalPlayerPosition();
    return IsValidPlayerPosition(playerPos);
} 