#pragma once

#include "../../types.h"
#include <d3d9.h>
#include <d3dx9.h>

class WorldToScreenCore {
private:
    LPDIRECT3DDEVICE9 m_pDevice;
    
public:
    WorldToScreenCore();
    ~WorldToScreenCore();
    
    // Initialization
    bool Initialize(LPDIRECT3DDEVICE9 pDevice);
    void Cleanup();
    
    // Core coordinate transformation
    bool WorldToScreen(const D3DXVECTOR3& worldPos, D3DXVECTOR2& screenPos);
    
    // Public interface for external use
    bool WorldToScreen(float worldX, float worldY, float worldZ, float* screenX, float* screenY);
    
    // Device management
    void OnDeviceLost();
    void OnDeviceReset();
    
    // Validation helpers
    bool IsValidPlayerPosition(const C3Vector& pos);
    bool GetPlayerPositionSafe(C3Vector& playerPos);
    
    // Device access
    LPDIRECT3DDEVICE9 GetDevice() const { return m_pDevice; }
}; 