#include "DrawingTab.h"
#include "../../objects/ObjectManager.h"
#include "../../objects/WowObject.h"
#include "../../objects/WowUnit.h"
#include "../../objects/WowGameObject.h"
#include "../../objects/WowPlayer.h"
#include "../../drawing.h"
#include <imgui.h>
#include <algorithm>
#include <sstream>
#include <iomanip>
#include "../../types.h"

namespace GUI {

DrawingTab::DrawingTab()
    : m_objectManager(nullptr)
    , m_showPlayerArrow(true)
    , m_showObjectNames(false)
    , m_showDistances(false)
    , m_showPlayerNames(true)
    , m_showUnitNames(false)
    , m_showGameObjectNames(false)
    , m_maxDrawDistance(50.0f)
    , m_onlyShowTargeted(false)
    , m_textScale(1.0f)
    , m_arrowSize(20)
{
    // Initialize colors
    m_playerArrowColor[0] = 1.0f; // Red
    m_playerArrowColor[1] = 0.0f; // Green
    m_playerArrowColor[2] = 0.0f; // Blue
    m_playerArrowColor[3] = 1.0f; // Alpha
    
    m_textColor[0] = 1.0f; // White
    m_textColor[1] = 1.0f;
    m_textColor[2] = 1.0f;
    m_textColor[3] = 1.0f;
    
    m_distanceColor[0] = 0.8f; // Light gray
    m_distanceColor[1] = 0.8f;
    m_distanceColor[2] = 0.8f;
    m_distanceColor[3] = 1.0f;
}

void DrawingTab::SetObjectManager(ObjectManager* objManager) {
    m_objectManager = objManager;
}

void DrawingTab::Update(float deltaTime) {
    // Update statistics periodically, not every frame
    static float statsUpdateTimer = 0.0f;
    statsUpdateTimer += deltaTime;
    
    if (statsUpdateTimer >= 1.0f) { // Update stats every 1 second
        UpdateStatistics();
        statsUpdateTimer = 0.0f;
    }
    
    // Synchronize tab settings with the drawing system
    extern WorldToScreenManager g_WorldToScreenManager;
    g_WorldToScreenManager.showPlayerArrow = m_showPlayerArrow;
    g_WorldToScreenManager.showObjectNames = m_showObjectNames;
    g_WorldToScreenManager.showDistances = m_showDistances;
    g_WorldToScreenManager.maxDrawDistance = m_maxDrawDistance;
}

void DrawingTab::Render() {
    if (!m_objectManager) {
        ImGui::Text("ObjectManager not initialized");
        return;
    }
    
    ImGui::Text("DirectX Drawing System Controls");
    ImGui::Separator();
    
    // Player Arrow Section
    if (ImGui::CollapsingHeader("Player Arrow", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox("Show Player Arrow", &m_showPlayerArrow);
        
        if (m_showPlayerArrow) {
            ImGui::SliderInt("Arrow Size", &m_arrowSize, 10, 50);
            ImGui::ColorEdit4("Arrow Color", m_playerArrowColor);
            
            ImGui::Text("Arrow shows your player position with 'YOU' text");
        }
    }
    
    ImGui::Separator();
    
    // Object Names Section
    if (ImGui::CollapsingHeader("Object Names")) {
        ImGui::Checkbox("Show Object Names", &m_showObjectNames);
        
        if (m_showObjectNames) {
            ImGui::Indent();
            ImGui::Checkbox("Player Names", &m_showPlayerNames);
            ImGui::Checkbox("Unit Names", &m_showUnitNames);
            ImGui::Checkbox("GameObject Names", &m_showGameObjectNames);
            ImGui::Unindent();
        }
    }
    
    ImGui::Separator();
    
    // Distance Display Section
    if (ImGui::CollapsingHeader("Distance Display")) {
        ImGui::Checkbox("Show Distances", &m_showDistances);
        
        if (m_showDistances) {
            ImGui::SliderFloat("Max Draw Distance", &m_maxDrawDistance, 10.0f, 200.0f, "%.1f yards");
            ImGui::Checkbox("Only Show Targeted Objects", &m_onlyShowTargeted);
        }
    }
    
    ImGui::Separator();
    
    // Style Settings Section
    if (ImGui::CollapsingHeader("Style Settings")) {
        ImGui::SliderFloat("Text Scale", &m_textScale, 0.5f, 2.0f, "%.1f");
        ImGui::ColorEdit4("Text Color", m_textColor);
        ImGui::ColorEdit4("Distance Color", m_distanceColor);
    }
    
    ImGui::Separator();
    
    // Status and Testing Section
    if (ImGui::CollapsingHeader("Status & Testing", ImGuiTreeNodeFlags_DefaultOpen)) {
        // Show current player position
        if (m_objectManager->IsInitialized()) {
            ImGui::Text("Player Position: (%.2f, %.2f, %.2f)", m_livePlayerPos.x, m_livePlayerPos.y, m_livePlayerPos.z);
            
            // Test DirectX drawing system status
            extern WorldToScreenManager g_WorldToScreenManager;
            const auto& markers = g_WorldToScreenManager.GetMarkers();
            const auto& lines = g_WorldToScreenManager.GetLines();
            
            ImGui::Text("DirectX Drawing System Status:");
            ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "✓ DirectX Rendering: Active");
            ImGui::Text("  Active Markers: %zu", markers.size());
            ImGui::Text("  Active Lines: %zu", lines.size());
            
            // Show detailed marker information
            if (!markers.empty()) {
                ImGui::Text("Marker Details:");
                for (size_t i = 0; i < markers.size() && i < 3; ++i) {
                    const auto& marker = markers[i];
                    ImGui::Text("  %s: (%.1f,%.1f,%.1f) -> %s", 
                        marker.label.c_str(),
                        marker.worldPos.x, marker.worldPos.y, marker.worldPos.z,
                        marker.isVisible ? "VISIBLE" : "HIDDEN");
                    if (marker.isVisible) {
                        ImGui::Text("    Screen: (%.1f, %.1f)", marker.screenPos.x, marker.screenPos.y);
                    }
                }
            }
            
            // Show detailed line information
            if (!lines.empty()) {
                ImGui::Text("Line Details:");
                for (size_t i = 0; i < lines.size() && i < 2; ++i) {
                    const auto& line = lines[i];
                    ImGui::Text("  %s: %s", line.label.c_str(), line.isVisible ? "VISIBLE" : "HIDDEN");
                }
            }
            
            // Show current target information from direct memory read
            if (m_liveTargetGUID.IsValid()) {
                auto target = m_objectManager->GetObjectByGUID(m_liveTargetGUID);
                if (target) {
                    ImGui::Text("Current Target: %s", target->GetName().c_str());
                    auto targetPos = target->GetPosition();
                    ImGui::Text("Target Position: (%.2f, %.2f, %.2f)", targetPos.x, targetPos.y, targetPos.z);
                    
                    float distance = m_livePlayerPos.Distance(C3Vector(targetPos.x, targetPos.y, targetPos.z));
                    ImGui::Text("Distance to Target: %.1f yards", distance);
                } else {
                    ImGui::Text("Current Target: GUID 0x%llX (not in object cache)", m_liveTargetGUID.ToUint64());
                }
            } else {
                ImGui::Text("No target selected");
            }
        } else {
            ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "ObjectManager not initialized");
        }
        
        ImGui::Separator();
        
        // Drawing statistics
        auto allObjects = m_objectManager->GetAllObjects();
        ImGui::Text("Total Objects: %zu", allObjects.size());
        
        // Show cached statistics (updated periodically)
        ImGui::Text("Objects in Draw Range: %d", m_cachedObjectsInRange);
        ImGui::Text("  Players: %d", m_cachedPlayersInRange);
        ImGui::Text("  Units: %d", m_cachedUnitsInRange);
        ImGui::Text("  GameObjects: %d", m_cachedGameObjectsInRange);
        
        ImGui::Separator();
        
        // Test buttons
        if (ImGui::Button("Test Player Arrow")) {
            // Force enable player arrow for testing
            m_showPlayerArrow = true;
            extern WorldToScreenManager g_WorldToScreenManager;
            g_WorldToScreenManager.showPlayerArrow = true;
        }
        
        ImGui::SameLine();
        
        if (ImGui::Button("Add Test Marker")) {
            // Add a test marker at player position
            extern WorldToScreenManager g_WorldToScreenManager;
            C3Vector playerPos = GetLocalPlayerPosition();
            D3DXVECTOR3 testPos(playerPos.x, playerPos.y, playerPos.z);
            g_WorldToScreenManager.AddMarker(testPos, 0xFF00FF00, 30.0f, "TEST");
        }
        
        ImGui::SameLine();
        
        if (ImGui::Button("Add Screen Test")) {
            // Add a test marker at a fixed world position for testing
            extern WorldToScreenManager g_WorldToScreenManager;
            C3Vector playerPos = GetLocalPlayerPosition();
            // Add marker 10 units in front of player
            D3DXVECTOR3 testPos(playerPos.x + 10.0f, playerPos.y, playerPos.z);
            g_WorldToScreenManager.AddMarker(testPos, 0xFFFFFF00, 40.0f, "FRONT");
        }
        
        ImGui::SameLine();
        
        if (ImGui::Button("Reset to Defaults")) {
            m_showPlayerArrow = true;
            m_showObjectNames = false;
            m_showDistances = false;
            m_showPlayerNames = true;
            m_showUnitNames = false;
            m_showGameObjectNames = false;
            m_maxDrawDistance = 50.0f;
            m_onlyShowTargeted = false;
            m_textScale = 1.0f;
            m_arrowSize = 20;
            
            // Reset colors
            m_playerArrowColor[0] = 1.0f; m_playerArrowColor[1] = 0.0f; 
            m_playerArrowColor[2] = 0.0f; m_playerArrowColor[3] = 1.0f;
            
            m_textColor[0] = 1.0f; m_textColor[1] = 1.0f; 
            m_textColor[2] = 1.0f; m_textColor[3] = 1.0f;
            
            m_distanceColor[0] = 0.8f; m_distanceColor[1] = 0.8f; 
            m_distanceColor[2] = 0.8f; m_distanceColor[3] = 1.0f;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Resets GUI settings only (colors, sizes, checkboxes).\nDoes NOT clear rendered markers/lines.");
        }
        
        // Cleanup buttons section
        ImGui::Separator();
        ImGui::Text("Cleanup Controls:");
        
        if (ImGui::Button("Clear All Markers")) {
            extern WorldToScreenManager g_WorldToScreenManager;
            g_WorldToScreenManager.ClearAllMarkers();
            
            // Re-add player arrow if it should be shown
            if (m_showPlayerArrow) {
                g_WorldToScreenManager.AddPlayerArrow();
            }
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Removes all markers (crosses) from screen.\nPlayer arrow will be re-added if enabled.");
        }
        
        ImGui::SameLine();
        
        if (ImGui::Button("Clear All Lines")) {
            extern WorldToScreenManager g_WorldToScreenManager;
            g_WorldToScreenManager.ClearAllLines();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Removes all lines from screen.\nIncludes player-to-target lines.");
        }
        
        ImGui::SameLine();
        
        if (ImGui::Button("Clear Everything")) {
            extern WorldToScreenManager g_WorldToScreenManager;
            g_WorldToScreenManager.ClearAllMarkers();
            g_WorldToScreenManager.ClearAllLines();
            
            // Re-add player arrow if it should be shown
            if (m_showPlayerArrow) {
                g_WorldToScreenManager.AddPlayerArrow();
            }
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Removes ALL markers and lines from screen.\nPlayer arrow will be re-added if enabled.");
        }
        
        ImGui::SameLine();
        
        if (ImGui::Button("Reset Settings + Clear All")) {
            // Reset all GUI settings to defaults
            m_showPlayerArrow = true;
            m_showObjectNames = false;
            m_showDistances = false;
            m_showPlayerNames = true;
            m_showUnitNames = false;
            m_showGameObjectNames = false;
            m_maxDrawDistance = 50.0f;
            m_onlyShowTargeted = false;
            m_textScale = 1.0f;
            m_arrowSize = 20;
            
            // Reset colors
            m_playerArrowColor[0] = 1.0f; m_playerArrowColor[1] = 0.0f; 
            m_playerArrowColor[2] = 0.0f; m_playerArrowColor[3] = 1.0f;
            
            m_textColor[0] = 1.0f; m_textColor[1] = 1.0f; 
            m_textColor[2] = 1.0f; m_textColor[3] = 1.0f;
            
            m_distanceColor[0] = 0.8f; m_distanceColor[1] = 0.8f; 
            m_distanceColor[2] = 0.8f; m_distanceColor[3] = 1.0f;
            
            // Clear all rendered objects
            extern WorldToScreenManager g_WorldToScreenManager;
            g_WorldToScreenManager.ClearAllMarkers();
            g_WorldToScreenManager.ClearAllLines();
            
            // Re-add player arrow since we want it by default
            g_WorldToScreenManager.AddPlayerArrow();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Complete reset: restores all GUI settings to defaults\nAND clears all markers/lines from screen.");
        }
    }
    
    ImGui::Separator();
    
    // Instructions
    if (ImGui::CollapsingHeader("Instructions")) {
        ImGui::TextWrapped("This tab controls the DirectX drawing system:");
        ImGui::Bullet(); ImGui::TextWrapped("Player Arrow: Red triangle with 'YOU' text showing your position");
        ImGui::Bullet(); ImGui::TextWrapped("Object Names: Display names of objects in the world");
        ImGui::Bullet(); ImGui::TextWrapped("Distance Display: Show distance to objects");
        ImGui::Bullet(); ImGui::TextWrapped("Use the settings above to customize what is drawn");
        ImGui::Bullet(); ImGui::TextWrapped("The drawing happens using DirectX overlay rendering");
        
        ImGui::Separator();
        ImGui::Text("Note: The red triangle arrow is the intended player indicator!");
    }
}

// Add global offset
extern "C" const uintptr_t CURRENT_TARGET_GUID_ADDR = GameOffsets::CURRENT_TARGET_GUID_ADDR;

void DrawingTab::UpdateStatistics() {
    // Reset cached statistics
    m_cachedObjectsInRange = 0;
    m_cachedPlayersInRange = 0;
    m_cachedUnitsInRange = 0;
    m_cachedGameObjectsInRange = 0;
    
    // Always fetch latest player position for UI
    C3Vector livePlayerPos = GetLocalPlayerPosition();
    m_livePlayerPos = livePlayerPos; // new member to display
    
    // Read current target guid directly
    uint64_t tgtGuid64 = Memory::Read<uint64_t>(CURRENT_TARGET_GUID_ADDR);
    m_liveTargetGUID = WGUID(tgtGuid64);
    
    if (!m_objectManager || !m_objectManager->IsInitialized()) {
        return;
    }
    
    auto player = m_objectManager->GetLocalPlayer();
    if (!player) {
        return;
    }
    
    auto playerPos = player->GetPosition();
    auto allObjects = m_objectManager->GetAllObjects();
    
    for (const auto& pair : allObjects) {
        auto obj = pair.second;
        if (!obj) continue;
        
        float distance = playerPos.Distance(obj->GetPosition());
        if (distance <= m_maxDrawDistance) {
            m_cachedObjectsInRange++;
            
            switch (obj->GetObjectType()) {
                case OBJECT_PLAYER:
                    m_cachedPlayersInRange++;
                    break;
                case OBJECT_UNIT:
                    m_cachedUnitsInRange++;
                    break;
                case OBJECT_GAMEOBJECT:
                    m_cachedGameObjectsInRange++;
                    break;
            }
        }
    }
}

} 