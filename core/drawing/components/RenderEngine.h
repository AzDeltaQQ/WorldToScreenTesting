#pragma once

#include <d3d9.h>
#include <d3dx9.h>
#include <string>

class RenderEngine {
private:
    LPDIRECT3DDEVICE9 m_pDevice;
    ID3DXLine* m_pLine;
    ID3DXFont* m_pFont;
    float m_currentTextScale; // Track current text scale for font recreation
    
public:
    RenderEngine();
    ~RenderEngine();
    
    // Initialization
    bool Initialize(LPDIRECT3DDEVICE9 pDevice);
    void Cleanup();
    
    // Device management
    void OnDeviceLost();
    void OnDeviceReset();
    
    // Text scale management
    void SetTextScale(float scale);
    bool CreateScaledFont(float scale);
    
    // Rendering primitives
    void DrawLine(const D3DXVECTOR2& start, const D3DXVECTOR2& end, D3DCOLOR color, float thickness);
    void DrawMarker(const D3DXVECTOR2& pos, D3DCOLOR color, float size);
    void DrawTriangleArrow(const D3DXVECTOR2& pos, D3DCOLOR color, float size);
    void DrawText(const std::string& text, const D3DXVECTOR2& pos, D3DCOLOR color, float scale = 1.0f);
    
    // Render state management
    void BeginRender();
    void EndRender();
    
    // Utility
    bool IsInitialized() const { return m_pDevice && m_pLine; }
}; 