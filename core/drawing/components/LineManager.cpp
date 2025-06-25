#include "LineManager.h"
#include "../../logs/Logger.h"
#include <algorithm>
#include <cmath>

LineManager::LineManager() : m_nextId(1), m_pWorldToScreen(nullptr), m_pRenderEngine(nullptr) {
}

LineManager::~LineManager() {
    Cleanup();
}

void LineManager::Initialize(WorldToScreenCore* pWorldToScreen, RenderEngine* pRenderEngine) {
    m_pWorldToScreen = pWorldToScreen;
    m_pRenderEngine = pRenderEngine;
}

void LineManager::Cleanup() {
    m_lines.clear();
    m_pWorldToScreen = nullptr;
    m_pRenderEngine = nullptr;
}

int LineManager::AddLine(const D3DXVECTOR3& start, const D3DXVECTOR3& end, D3DCOLOR color, float thickness, const std::string& label) {
    LineData line;
    line.start = start;
    line.end = end;
    line.color = color;
    line.thickness = thickness;
    line.isVisible = true;
    line.id = m_nextId++;
    line.label = label.empty() ? ("Line" + std::to_string(line.id)) : label;
    
    m_lines.push_back(line);
    return line.id;
}

void LineManager::RemoveLine(int id) {
    m_lines.erase(
        std::remove_if(m_lines.begin(), m_lines.end(),
            [id](const LineData& line) { return line.id == id; }),
        m_lines.end()
    );
}

void LineManager::ClearAllLines() {
    m_lines.clear();
}

void LineManager::Update() {
    if (!m_pWorldToScreen) return;
    
    // All lines are always visible - no complex visibility checks needed
    for (auto& line : m_lines) {
        line.isVisible = true;
    }
}

void LineManager::Render() {
    if (!m_pWorldToScreen || !m_pRenderEngine) return;
    
    // Render all lines - force them to be drawn regardless of WorldToScreen result
    for (const auto& line : m_lines) {
        if (!line.isVisible) continue;
        
        D3DXVECTOR2 startScreen(0.0f, 0.0f), endScreen(0.0f, 0.0f);
        m_pWorldToScreen->WorldToScreen(line.start, startScreen);
        m_pWorldToScreen->WorldToScreen(line.end, endScreen);
        
        // Always draw the line - GPU handles clipping for off-screen coordinates
        m_pRenderEngine->DrawLine(startScreen, endScreen, line.color, line.thickness);
    }
} 