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
    line.isVisible = true; // Always visible now
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
    
    // All lines are always visible now - no complex visibility checks needed
    for (auto& line : m_lines) {
        line.isVisible = true;
        
        // Debug log line positions occasionally
        static int lineDebugCounter = 0;
        if (++lineDebugCounter % 300 == 0) { // Log every 5 seconds
            LOG_DEBUG("Line " + line.label + " always visible - startPos(" + 
                     std::to_string(line.start.x) + "," + std::to_string(line.start.y) + "," + std::to_string(line.start.z) + ")" +
                     " endPos(" + std::to_string(line.end.x) + "," + std::to_string(line.end.y) + "," + std::to_string(line.end.z) + ")");
        }
    }
}

void LineManager::Render() {
    if (!m_pWorldToScreen || !m_pRenderEngine) return;
    
    int renderedLines = 0;
    
    // Render all lines - ALWAYS force them to be drawn
    for (const auto& line : m_lines) {
        if (!line.isVisible) continue; // This should always be true now
        
        D3DXVECTOR2 startScreen(0.0f, 0.0f), endScreen(0.0f, 0.0f);
        bool startVisible = m_pWorldToScreen->WorldToScreen(line.start, startScreen);
        bool endVisible = m_pWorldToScreen->WorldToScreen(line.end, endScreen);
        
        // ALWAYS DRAW THE LINE - regardless of WorldToScreen success/failure
        // Even if WorldToScreen fails, we still get screen coordinates (they might just be off-screen)
        // The GPU will handle clipping, so we don't need to worry about it
        
        m_pRenderEngine->DrawLine(startScreen, endScreen, line.color, line.thickness);
        renderedLines++;
        
        // Debug log when coordinates seem problematic
        static int debugCounter = 0;
        if (++debugCounter % 120 == 0) { // Log every 2 seconds
            LOG_DEBUG("Line " + line.label + " rendered: startVisible=" + std::to_string(startVisible) + 
                     " endVisible=" + std::to_string(endVisible) + 
                     " startScreen(" + std::to_string(startScreen.x) + "," + std::to_string(startScreen.y) + ")" +
                     " endScreen(" + std::to_string(endScreen.x) + "," + std::to_string(endScreen.y) + ")");
        }
    }
    
    // Debug log occasionally
    static int renderCounter = 0;
    if (++renderCounter % 600 == 0) {
        LOG_DEBUG("LineManager rendered " + std::to_string(renderedLines) + " out of " + 
                 std::to_string(m_lines.size()) + " lines");
    }
} 