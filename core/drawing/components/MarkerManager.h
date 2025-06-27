#pragma once

#include "../../types/types.h"
#include "WorldToScreenCore.h"
#include "RenderEngine.h"
#include <vector>
#include <string>
#include <d3dx9.h>

struct MarkerData {
    D3DXVECTOR3 worldPos;
    D3DXVECTOR2 screenPos;
    D3DCOLOR color;
    float size;
    bool isVisible;
    int id;
    std::string label;
};

class MarkerManager {
private:
    std::vector<MarkerData> m_markers;
    int m_nextId;
    WorldToScreenCore* m_pWorldToScreen;
    RenderEngine* m_pRenderEngine;
    
public:
    MarkerManager();
    ~MarkerManager();
    
    // Initialization
    void Initialize(WorldToScreenCore* pWorldToScreen, RenderEngine* pRenderEngine);
    void Cleanup();
    
    // Marker management
    int AddMarker(const D3DXVECTOR3& worldPos, D3DCOLOR color = 0xFFFF0000, float size = 10.0f, const std::string& label = "");
    void RemoveMarker(int id);
    void ClearAllMarkers();
    
    // Marker updating
    bool UpdateMarkerPosition(const std::string& label, const D3DXVECTOR3& newPos);
    bool UpdateMarkerProperties(const std::string& label, D3DCOLOR color, float size);
    
    // Update and render
    void Update();
    void Render();
    
    // Access
    const std::vector<MarkerData>& GetMarkers() const { return m_markers; }
    size_t GetMarkerCount() const { return m_markers.size(); }
    int GetVisibleCount() const;
    
    // Text styling settings (synchronized from GUI)
    D3DCOLOR textColor = 0xFFFFFFFF; // Default white
    float textScale = 1.0f; // Default scale
}; 