#include "MarkerManager.h"
#include "../../logs/Logger.h"
#include <algorithm>

MarkerManager::MarkerManager() : m_nextId(1), m_pWorldToScreen(nullptr), m_pRenderEngine(nullptr) {
}

MarkerManager::~MarkerManager() {
    Cleanup();
}

void MarkerManager::Initialize(WorldToScreenCore* pWorldToScreen, RenderEngine* pRenderEngine) {
    m_pWorldToScreen = pWorldToScreen;
    m_pRenderEngine = pRenderEngine;
}

void MarkerManager::Cleanup() {
    m_markers.clear();
    m_pWorldToScreen = nullptr;
    m_pRenderEngine = nullptr;
}

int MarkerManager::AddMarker(const D3DXVECTOR3& worldPos, D3DCOLOR color, float size, const std::string& label) {
    MarkerData marker;
    marker.worldPos = worldPos;
    marker.color = color;
    marker.size = size;
    marker.isVisible = true;
    marker.id = m_nextId++;
    marker.label = label.empty() ? ("Marker" + std::to_string(marker.id)) : label;
    
    m_markers.push_back(marker);
    return marker.id;
}

void MarkerManager::RemoveMarker(int id) {
    m_markers.erase(
        std::remove_if(m_markers.begin(), m_markers.end(),
            [id](const MarkerData& marker) { return marker.id == id; }),
        m_markers.end()
    );
}

void MarkerManager::ClearAllMarkers() {
    m_markers.clear();
}

bool MarkerManager::UpdateMarkerPosition(const std::string& label, const D3DXVECTOR3& newPos) {
    for (auto& marker : m_markers) {
        if (marker.label == label) {
            marker.worldPos = newPos;
            return true;
        }
    }
    return false;
}

bool MarkerManager::UpdateMarkerProperties(const std::string& label, D3DCOLOR color, float size) {
    for (auto& marker : m_markers) {
        if (marker.label == label) {
            marker.color = color;
            marker.size = size;
            return true;
        }
    }
    return false;
}

void MarkerManager::Update() {
    if (!m_pWorldToScreen) return;
    
    // Update marker screen positions using WorldToScreen
    for (auto& marker : m_markers) {
        D3DXVECTOR2 screenPos;
        marker.isVisible = m_pWorldToScreen->WorldToScreen(marker.worldPos, screenPos);
        if (marker.isVisible) {
            marker.screenPos = screenPos;
        } else {
            // Debug log why WorldToScreen failed occasionally
            static int debugCounter = 0;
            if (++debugCounter % 600 == 0) { // Log every 10 seconds
                LOG_DEBUG("WorldToScreen failed for marker " + marker.label + 
                         " at pos (" + std::to_string(marker.worldPos.x) + ", " + 
                         std::to_string(marker.worldPos.y) + ", " + 
                         std::to_string(marker.worldPos.z) + ")");
            }
        }
    }
}

void MarkerManager::Render() {
    if (!m_pWorldToScreen || !m_pRenderEngine) return;
    
    // Render markers
    for (const auto& marker : m_markers) {
        if (marker.isVisible) {
            // Draw triangle arrow for player marker, cross for others
            if (marker.label == "YOU") {
                m_pRenderEngine->DrawTriangleArrow(marker.screenPos, marker.color, marker.size);
            } else {
                m_pRenderEngine->DrawMarker(marker.screenPos, marker.color, marker.size);
            }
            
            // Draw label if available
            if (!marker.label.empty()) {
                D3DXVECTOR2 textPos = marker.screenPos;
                textPos.y -= marker.size + 5; // Position text above marker
                m_pRenderEngine->DrawText(marker.label, textPos, textColor, textScale);
            }
        }
    }
}

int MarkerManager::GetVisibleCount() const {
    int count = 0;
    for (const auto& marker : m_markers) {
        if (marker.isVisible) {
            count++;
        }
    }
    return count;
} 