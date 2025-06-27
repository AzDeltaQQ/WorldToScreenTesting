#pragma once

#include "../types/types.h"
#include "WorldToScreenCore.h"
#include "RenderEngine.h"
#include <vector>
#include <string>
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

class LineManager {
private:
    std::vector<LineData> m_lines;
    int m_nextId;
    WorldToScreenCore* m_pWorldToScreen;
    RenderEngine* m_pRenderEngine;
    
public:
    LineManager();
    ~LineManager();
    
    // Initialization
    void Initialize(WorldToScreenCore* pWorldToScreen, RenderEngine* pRenderEngine);
    void Cleanup();
    
    // Line management
    int AddLine(const D3DXVECTOR3& start, const D3DXVECTOR3& end, D3DCOLOR color = 0xFF00FF00, float thickness = 2.0f, const std::string& label = "");
    void RemoveLine(int id);
    void ClearAllLines();
    
    // Update and render
    void Update();
    void Render();
    
    // Access
    const std::vector<LineData>& GetLines() const { return m_lines; }
    size_t GetLineCount() const { return m_lines.size(); }
}; 