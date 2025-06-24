#include "RenderEngine.h"
#include "../../logs/Logger.h"
#include <algorithm>

RenderEngine::RenderEngine() : m_pDevice(nullptr), m_pLine(nullptr), m_pFont(nullptr), m_currentTextScale(1.0f) {
}

RenderEngine::~RenderEngine() {
    Cleanup();
}

bool RenderEngine::Initialize(LPDIRECT3DDEVICE9 pDevice) {
    if (!pDevice) {
        LOG_ERROR("pDevice is null in RenderEngine");
        return false;
    }
    
    m_pDevice = pDevice;
    
    // Create ID3DXLine for drawing lines
    HRESULT hr = D3DXCreateLine(m_pDevice, &m_pLine);
    if (FAILED(hr)) {
        LOG_ERROR("Failed to create ID3DXLine, HRESULT: 0x" + std::to_string(hr));
        return false;
    }
    
    // Create font for text rendering
    hr = D3DXCreateFont(m_pDevice, 14, 0, FW_NORMAL, 1, FALSE, DEFAULT_CHARSET,
                        OUT_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                        L"Arial", &m_pFont);
    if (FAILED(hr)) {
        LOG_ERROR("Failed to create ID3DXFont, HRESULT: 0x" + std::to_string(hr));
        // Continue without font - not critical
    }
    
    return true;
}

void RenderEngine::Cleanup() {
    if (m_pLine) {
        m_pLine->Release();
        m_pLine = nullptr;
    }
    
    if (m_pFont) {
        m_pFont->Release();
        m_pFont = nullptr;
    }
    
    m_pDevice = nullptr;
}

void RenderEngine::OnDeviceLost() {
    if (m_pLine) {
        m_pLine->OnLostDevice();
    }
    if (m_pFont) {
        m_pFont->OnLostDevice();
    }
}

void RenderEngine::OnDeviceReset() {
    if (m_pLine) {
        m_pLine->OnResetDevice();
    }
    if (m_pFont) {
        m_pFont->OnResetDevice();
    }
}

void RenderEngine::BeginRender() {
    if (!m_pDevice) return;
    
    // Enable alpha blending
    m_pDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    m_pDevice->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    m_pDevice->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
}

void RenderEngine::EndRender() {
    // Render states are restored by caller if needed
}

void RenderEngine::DrawLine(const D3DXVECTOR2& start, const D3DXVECTOR2& end, D3DCOLOR color, float thickness) {
    if (!m_pLine || !m_pDevice) {
        return;
    }
    
    // Safety check for device validity
    HRESULT cooperativeLevel = m_pDevice->TestCooperativeLevel();
    if (cooperativeLevel != D3D_OK) {
        return;
    }
        
    D3DXVECTOR2 points[2] = { start, end };
    
    m_pLine->SetWidth(thickness);
    m_pLine->Begin();
    m_pLine->Draw(points, 2, color);
    m_pLine->End();
}

void RenderEngine::DrawMarker(const D3DXVECTOR2& pos, D3DCOLOR color, float size) {
    if (!m_pLine || !m_pDevice) {
        return;
    }
    
    // Safety check for device validity
    HRESULT cooperativeLevel = m_pDevice->TestCooperativeLevel();
    if (cooperativeLevel != D3D_OK) {
        return;
    }
        
    float halfSize = size * 0.5f;
    
    // Draw cross marker
    D3DXVECTOR2 horizontal[2] = {
        D3DXVECTOR2(pos.x - halfSize, pos.y),
        D3DXVECTOR2(pos.x + halfSize, pos.y)
    };
    
    D3DXVECTOR2 vertical[2] = {
        D3DXVECTOR2(pos.x, pos.y - halfSize),
        D3DXVECTOR2(pos.x, pos.y + halfSize)
    };
    
    m_pLine->SetWidth(2.0f);
    m_pLine->Begin();
    m_pLine->Draw(horizontal, 2, color);
    m_pLine->Draw(vertical, 2, color);
    m_pLine->End();
}

void RenderEngine::DrawTriangleArrow(const D3DXVECTOR2& pos, D3DCOLOR color, float size) {
    if (!m_pLine || !m_pDevice) {
        return;
    }
    
    // Safety check for device validity
    HRESULT cooperativeLevel = m_pDevice->TestCooperativeLevel();
    if (cooperativeLevel != D3D_OK) {
        return;
    }
    
    float halfSize = size * 0.5f;
    
    // Draw triangle pointing up
    D3DXVECTOR2 triangle[4] = {
        D3DXVECTOR2(pos.x, pos.y - halfSize),           // Top point
        D3DXVECTOR2(pos.x - halfSize, pos.y + halfSize), // Bottom left
        D3DXVECTOR2(pos.x + halfSize, pos.y + halfSize), // Bottom right
        D3DXVECTOR2(pos.x, pos.y - halfSize)            // Back to top (close triangle)
    };
    
    m_pLine->SetWidth(3.0f);
    m_pLine->Begin();
    m_pLine->Draw(triangle, 4, color);
    m_pLine->End();
}

void RenderEngine::DrawText(const std::string& text, const D3DXVECTOR2& pos, D3DCOLOR color, float scale) {
    if (!m_pFont || !m_pDevice || text.empty()) {
        return;
    }
    
    // Safety check for device validity
    HRESULT cooperativeLevel = m_pDevice->TestCooperativeLevel();
    if (cooperativeLevel != D3D_OK) {
        return;
    }
    
    // Update font scale if needed
    if (scale != m_currentTextScale) {
        SetTextScale(scale);
    }
    
    // Calculate scaled text area
    float baseWidth = 100.0f;   // Base text area width
    float baseHeight = 40.0f;   // Base text area height
    float scaledWidth = baseWidth * scale;
    float scaledHeight = baseHeight * scale;
    
    RECT rect;
    rect.left = static_cast<LONG>(pos.x - scaledWidth * 0.5f);
    rect.top = static_cast<LONG>(pos.y - scaledHeight * 0.5f);
    rect.right = static_cast<LONG>(pos.x + scaledWidth * 0.5f);
    rect.bottom = static_cast<LONG>(pos.y + scaledHeight * 0.5f);
    
    // Convert string to wide string
    std::wstring wtext(text.begin(), text.end());
    
    m_pFont->DrawText(nullptr, wtext.c_str(), -1, &rect, DT_CENTER | DT_VCENTER, color);
}

void RenderEngine::SetTextScale(float scale) {
    if (scale != m_currentTextScale) {
        m_currentTextScale = scale;
        CreateScaledFont(scale);
    }
}

bool RenderEngine::CreateScaledFont(float scale) {
    if (!m_pDevice) return false;
    
    // Release existing font
    if (m_pFont) {
        m_pFont->Release();
        m_pFont = nullptr;
    }
    
    // Calculate scaled font size
    int baseFontSize = 14;
    int scaledFontSize = static_cast<int>(baseFontSize * scale);
    if (scaledFontSize < 8) scaledFontSize = 8;   // Minimum readable size
    if (scaledFontSize > 72) scaledFontSize = 72; // Maximum reasonable size
    
    // Create new scaled font
    HRESULT hr = D3DXCreateFont(m_pDevice, scaledFontSize, 0, FW_NORMAL, 1, FALSE, DEFAULT_CHARSET,
                                OUT_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                                L"Arial", &m_pFont);
    
    if (FAILED(hr)) {
        LOG_ERROR("Failed to create scaled ID3DXFont, HRESULT: 0x" + std::to_string(hr));
        return false;
    }
    
    return true;
} 