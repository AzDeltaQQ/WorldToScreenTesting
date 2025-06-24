#pragma once

#include "types.h"
#include <vector>
#include <string>
#include <d3d9.h>
#include <d3dx9.h>

struct LineData {
    D3DXVECTOR3 start;
    D3DXVECTOR3 end;
    D3DCOLOR color;
    float thickness;
    bool isVisible;
    int id;
    std::string label;
};

struct MarkerData {
    D3DXVECTOR3 worldPos;
    D3DXVECTOR2 screenPos;
    D3DCOLOR color;
    float size;
    bool isVisible;
    int id;
    std::string label;
};

class WorldToScreenManager {
private:
    std::vector<LineData> lines;
    std::vector<MarkerData> markers;
    int nextId;
    
    // Direct3D resources
    LPDIRECT3DDEVICE9 m_pDevice;
    ID3DXLine* m_pLine;
    ID3DXFont* m_pFont;
    
    // World to screen conversion using DirectX
    bool WorldToScreen(const D3DXVECTOR3& worldPos, D3DXVECTOR2& screenPos);
    
    // Drawing functions
    void DrawLine(const D3DXVECTOR2& start, const D3DXVECTOR2& end, D3DCOLOR color, float thickness);
    void DrawMarker(const D3DXVECTOR2& pos, D3DCOLOR color, float size);
    void DrawText(const std::string& text, const D3DXVECTOR2& pos, D3DCOLOR color);
    void DrawTriangleArrow(const D3DXVECTOR2& pos, D3DCOLOR color, float size);
    
    // Helper functions
    bool IsValidPlayerPosition(const C3Vector& pos);
    bool GetPlayerPositionSafe(C3Vector& playerPos);
    
public:
    WorldToScreenManager();
    ~WorldToScreenManager();
    
    // Initialization/cleanup
    bool Initialize(LPDIRECT3DDEVICE9 pDevice);
    void Cleanup();
    void OnDeviceLost();
    void OnDeviceReset();
    
    // Line management
    int AddLine(const D3DXVECTOR3& start, const D3DXVECTOR3& end, D3DCOLOR color = 0xFF00FF00, float thickness = 2.0f, const std::string& label = "");
    void RemoveLine(int id);
    void ClearAllLines();
    
    // Marker management  
    int AddMarker(const D3DXVECTOR3& worldPos, D3DCOLOR color = 0xFFFF0000, float size = 10.0f, const std::string& label = "");
    void RemoveMarker(int id);
    void ClearAllMarkers();
    
    // Player arrow management
    int AddPlayerArrow(D3DCOLOR color = 0xFFFF0000, float size = 20.0f);
    void UpdatePlayerArrow();
    
    // Player-to-enemy line helpers
    int AddPlayerToEnemyLine(const D3DXVECTOR3& enemyPos, D3DCOLOR color = 0xFF00FF00);
    void UpdatePlayerToEnemyLines();
    
    // Update and render
    void Update();
    void Render();
    
    // Public WorldToScreen function for GUI use
    bool WorldToScreen(float worldX, float worldY, float worldZ, float* screenX, float* screenY);
    
    // Get data for external use
    const std::vector<LineData>& GetLines() const { return lines; }
    const std::vector<MarkerData>& GetMarkers() const { return markers; }
    
    // Settings
    bool showPlayerArrow = true;
    bool showObjectNames = false;
    bool showDistances = false;
    float maxDrawDistance = 50.0f;
}; 