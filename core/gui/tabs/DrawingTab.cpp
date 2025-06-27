#include "DrawingTab.h"
#include "../../objects/ObjectManager.h"
#include "../../objects/WowObject.h"
#include "../../objects/WowUnit.h"
#include "../../objects/WowGameObject.h"
#include "../../objects/WowPlayer.h"
#include "../../drawing/drawing.h"
#include "../../logs/Logger.h"
#include <imgui.h>
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <cstring>
#include <unordered_map>
#include "../../types/types.h"

namespace GUI {

DrawingTab::DrawingTab()
    : m_objectManager(nullptr)
    , m_showPlayerArrow(true)
    , m_showObjectNames(false)
    , m_showDistances(false)
    , m_showPlayerNames(true)
    , m_showUnitNames(false)
    , m_showGameObjectNames(false)
    , m_showPlayerDistances(true)
    , m_showUnitDistances(false)
    , m_showGameObjectDistances(false)
    , m_maxDrawDistance(50.0f)
    , m_onlyShowTargeted(false)
    , m_showPlayerToTargetLine(true)
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
    
    m_lineColor[0] = 0.0f; // Green
    m_lineColor[1] = 1.0f;
    m_lineColor[2] = 0.0f;
    m_lineColor[3] = 1.0f;
}

void DrawingTab::SetObjectManager(ObjectManager* objManager) {
    m_objectManager = objManager;
    
    // Initialize the Line of Sight manager
    if (objManager && !m_losManager.IsInitialized()) {
        if (m_losManager.Initialize()) {
            LOG_INFO("LineOfSightManager initialized in DrawingTab");
        } else {
            LOG_WARNING("Failed to initialize LineOfSightManager in DrawingTab");
        }
    }
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
    
    // Convert float RGBA to D3DCOLOR and sync arrow settings
    BYTE r = (BYTE)(m_playerArrowColor[0] * 255.0f);
    BYTE g = (BYTE)(m_playerArrowColor[1] * 255.0f);
    BYTE b = (BYTE)(m_playerArrowColor[2] * 255.0f);
    BYTE a = (BYTE)(m_playerArrowColor[3] * 255.0f);
    g_WorldToScreenManager.playerArrowColor = D3DCOLOR_ARGB(a, r, g, b);
    g_WorldToScreenManager.playerArrowSize = (float)m_arrowSize;
    
    // Convert and sync text color settings
    r = (BYTE)(m_textColor[0] * 255.0f);
    g = (BYTE)(m_textColor[1] * 255.0f);
    b = (BYTE)(m_textColor[2] * 255.0f);
    a = (BYTE)(m_textColor[3] * 255.0f);
    g_WorldToScreenManager.textColor = D3DCOLOR_ARGB(a, r, g, b);
    
    // Convert and sync distance color settings
    r = (BYTE)(m_distanceColor[0] * 255.0f);
    g = (BYTE)(m_distanceColor[1] * 255.0f);
    b = (BYTE)(m_distanceColor[2] * 255.0f);
    a = (BYTE)(m_distanceColor[3] * 255.0f);
    g_WorldToScreenManager.distanceColor = D3DCOLOR_ARGB(a, r, g, b);
    
    // Convert and sync line color settings
    r = (BYTE)(m_lineColor[0] * 255.0f);
    g = (BYTE)(m_lineColor[1] * 255.0f);
    b = (BYTE)(m_lineColor[2] * 255.0f);
    a = (BYTE)(m_lineColor[3] * 255.0f);
    g_WorldToScreenManager.lineColor = D3DCOLOR_ARGB(a, r, g, b);
    
    // Sync other settings
    g_WorldToScreenManager.textScale = m_textScale;
    g_WorldToScreenManager.showPlayerNames = m_showPlayerNames;
    g_WorldToScreenManager.showUnitNames = m_showUnitNames;
    g_WorldToScreenManager.showGameObjectNames = m_showGameObjectNames;
    g_WorldToScreenManager.showPlayerDistances = m_showPlayerDistances;
    g_WorldToScreenManager.showUnitDistances = m_showUnitDistances;
    g_WorldToScreenManager.showGameObjectDistances = m_showGameObjectDistances;
    g_WorldToScreenManager.showPlayerToTargetLine = m_showPlayerToTargetLine;
    
    // Synchronize LoS settings with the main drawing system
    if (m_losManager.IsInitialized() && g_WorldToScreenManager.GetLoSManager().IsInitialized()) {
        auto currentSettings = m_losManager.GetSettings();
        g_WorldToScreenManager.GetLoSManager().SetSettings(currentSettings);
    }
    
    // Synchronize texture settings with the main drawing system
    auto currentTextureSettings = m_textureSettings;
    g_WorldToScreenManager.GetTextureManager().SetSettings(currentTextureSettings);
    
    // Update Line of Sight manager
    if (m_losManager.IsInitialized()) {
        Vector3 playerPos(m_livePlayerPos.x, m_livePlayerPos.y, m_livePlayerPos.z);
        m_losManager.Update(deltaTime, playerPos);
        
        // Update LoS for objects if enabled
        if (m_losManager.GetSettings().enableLoSChecks && m_objectManager) {
            auto allObjects = m_objectManager->GetAllObjects();
            std::vector<WowObject*> objectPtrs;
            objectPtrs.reserve(allObjects.size());
            
            for (auto& objPair : allObjects) {
                objectPtrs.push_back(objPair.second.get());
            }
            
            m_losManager.UpdateLoSForObjects(objectPtrs, playerPos);
        }
    }
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
    
    // Lines Section (NEW)
    if (ImGui::CollapsingHeader("Lines & Connections")) {
        ImGui::Text("Line rendering settings for player-to-target connections");
        
        ImGui::Checkbox("Show Player-to-Target Line", &m_showPlayerToTargetLine);
        ImGui::ColorEdit4("Line Color", m_lineColor);
        
        if (m_showPlayerToTargetLine) {
            ImGui::Text("Lines automatically connect player to current target");
        } else {
            ImGui::Text("Player-to-target lines are disabled");
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
            ImGui::Indent();
            
            ImGui::Text("Show distances for:");
            ImGui::Checkbox("Player Distances", &m_showPlayerDistances);
            ImGui::Checkbox("Unit Distances", &m_showUnitDistances);
            ImGui::Checkbox("GameObject Distances", &m_showGameObjectDistances);
            
            ImGui::Unindent();
        }
        
        ImGui::SliderFloat("Max Draw Distance", &m_maxDrawDistance, 10.0f, 200.0f, "%.1f yards");
        ImGui::Checkbox("Only Show Targeted Objects", &m_onlyShowTargeted);
    }
    
    ImGui::Separator();
    
    // Line of Sight Section
    if (ImGui::CollapsingHeader("Line of Sight")) {
        const auto& losSettings = m_losManager.GetSettings();
        LoSSettings newSettings = losSettings;
        
        ImGui::Checkbox("Enable Line of Sight Checks", &newSettings.enableLoSChecks);
        
        if (newSettings.enableLoSChecks) {
            ImGui::Indent();
            
            ImGui::Checkbox("Show LoS Lines", &newSettings.showLoSLines);
            ImGui::Checkbox("Use LoS for Targeting", &newSettings.useLoSForTargeting);
            ImGui::Checkbox("Show Blocked Targets", &newSettings.showBlockedTargets);
            
            ImGui::Separator();
            ImGui::Text("Visual Settings:");
            ImGui::ColorEdit4("Clear LoS Color", newSettings.clearLoSColor);
            ImGui::ColorEdit4("Blocked LoS Color", newSettings.blockedLoSColor);
            ImGui::SliderFloat("LoS Line Width", &newSettings.losLineWidth, 1.0f, 5.0f, "%.1f");
            
            ImGui::Separator();
            ImGui::Text("Performance Settings:");
            ImGui::SliderFloat("Update Interval", &newSettings.losUpdateInterval, 0.1f, 2.0f, "%.2f sec");
            ImGui::SliderFloat("Max LoS Range", &newSettings.maxLoSRange, 20.0f, 200.0f, "%.1f yards");
            
            ImGui::Unindent();
            
            // LoS Status Information
            ImGui::Separator();
            ImGui::Text("LoS Status:");
            ImGui::Text("Cache Size: %zu entries", m_losManager.GetCacheSize());
            ImGui::Text("System Status: %s", m_losManager.IsInitialized() ? "INITIALIZED" : "NOT INITIALIZED");
            
            if (ImGui::Button("Clear LoS Cache")) {
                m_losManager.ClearLoSCache();
            }
            
            ImGui::SameLine();
            
            // Live LoS Status Display
            ImGui::Text("Live LoS Status:");
            if (m_liveTargetGUID.IsValid()) {
                auto target = m_objectManager->GetObjectByGUID(m_liveTargetGUID);
                if (target) {
                    auto targetPos = target->GetPosition();
                    Vector3 targetVec(targetPos.x, targetPos.y, targetPos.z);
                    
                    // Use player position with height offset for eye level (approximate camera position)
                    Vector3 playerVec(m_livePlayerPos.x, m_livePlayerPos.y, m_livePlayerPos.z + 2.0f);  // +2.0 yards for eye height
                    
                    bool hasLoS = m_losManager.HasClearLineOfSight(playerVec, targetVec);
                    float distance = playerVec.Distance(targetVec);
                    
                    // Display LoS status with color coding
                    ImVec4 losColor = hasLoS ? ImVec4(0.0f, 1.0f, 0.0f, 1.0f) : ImVec4(1.0f, 0.0f, 0.0f, 1.0f);
                    ImGui::Text("Target: %s", target->GetName().c_str());
                    ImGui::Text("Distance: %.1f yards", distance);
                    ImGui::TextColored(losColor, "Line of Sight: %s", hasLoS ? "CLEAR" : "BLOCKED");
                    
                    // Show origin and target coordinates for debugging
                    ImGui::Text("Player Eye: (%.1f, %.1f, %.1f)", playerVec.x, playerVec.y, playerVec.z);
                    ImGui::Text("Target Pos: (%.1f, %.1f, %.1f)", targetVec.x, targetVec.y, targetVec.z);
                    
                    // Optional: Show LoS result details if available
                    auto losResult = m_losManager.GetLoSResult(m_liveTargetGUID);
                    if (losResult.isValid) {
                        ImGui::Text("Hit Fraction: %.3f", losResult.hitFraction);
                        if (losResult.isBlocked && losResult.hitObjectGUID.IsValid()) {
                            ImGui::Text("Blocked by: 0x%llX", losResult.hitObjectGUID.ToUint64());
                        }
                    }
                    
                    // Manual test button (for logging/debugging)
                    if (ImGui::Button("Log LoS Test")) {
                        LOG_INFO("Manual LoS Test: " + std::string(hasLoS ? "CLEAR" : "BLOCKED") + 
                            " (Distance: " + std::to_string(distance) + " yards)");
                    }
                    
                    // Add LoS line if enabled (DISABLED - main system handles this now)
                    if (false && m_losManager.GetSettings().showLoSLines) {
                        extern WorldToScreenManager g_WorldToScreenManager;
                        
                        // Remove any existing LoS line
                        if (m_losLineId != -1) {
                            g_WorldToScreenManager.RemoveLine(m_losLineId);
                            m_losLineId = -1;
                        }
                        
                        // Get player position using the same method as PlayerTracker for consistent positioning
                        C3Vector playerPosC3;
                        if (g_WorldToScreenManager.GetPlayerPositionSafe(playerPosC3)) {
                            // Add new LoS line with appropriate color
                            D3DXVECTOR3 startPos(playerPosC3.x, playerPosC3.y, playerPosC3.z + 2.0f);  // +2 yards above head
                            D3DXVECTOR3 endPos(targetVec.x, targetVec.y, targetVec.z);
                            
                            // Use green for clear LoS, red for blocked
                            D3DCOLOR lineColor = hasLoS ? 0xFF00FF00 : 0xFFFF0000;  // Green : Red
                            
                            m_losLineId = g_WorldToScreenManager.AddLine(startPos, endPos, lineColor, 3.0f, "LoS_Line");
                        }
                    } else {
                        // Clear LoS line if showing is disabled
                        if (m_losLineId != -1) {
                            extern WorldToScreenManager g_WorldToScreenManager;
                            g_WorldToScreenManager.RemoveLine(m_losLineId);
                            m_losLineId = -1;
                        }
                    }
                    
                } else {
                    ImGui::Text("Target GUID valid but object not found");
                }
            } else {
                ImGui::Text("No target selected");
                
                // Clear LoS line when no target
                if (m_losLineId != -1) {
                    extern WorldToScreenManager g_WorldToScreenManager;
                    g_WorldToScreenManager.RemoveLine(m_losLineId);
                    m_losLineId = -1;
                }
            }
        }
        
        // Update settings if they changed
        if (memcmp(&losSettings, &newSettings, sizeof(LoSSettings)) != 0) {
            m_losManager.SetSettings(newSettings);
            
            // Clear LoS line if LoS checks were disabled
            if (!newSettings.enableLoSChecks && m_losLineId != -1) {
                extern WorldToScreenManager g_WorldToScreenManager;
                g_WorldToScreenManager.RemoveLine(m_losLineId);
                m_losLineId = -1;
            }
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
    
    // Texture Search & Rendering Section
    if (ImGui::CollapsingHeader("Texture Search & Rendering")) {
        ImGui::Checkbox("Enable Texture Rendering", &m_textureSettings.enableTextureRendering);
        
        if (m_textureSettings.enableTextureRendering) {
            ImGui::Indent();
            
            // Display settings
            ImGui::Checkbox("Show Texture Labels", &m_textureSettings.showTextureLabels);
            ImGui::Checkbox("Only Show In Range", &m_textureSettings.onlyShowInRange);
            ImGui::Checkbox("Billboard Textures", &m_textureSettings.billboardTextures);
            
            if (m_textureSettings.onlyShowInRange) {
                ImGui::SliderFloat("Max Render Distance", &m_textureSettings.maxRenderDistance, 10.0f, 500.0f, "%.1f yards");
            }
            
            ImGui::SliderFloat("Default Texture Size", &m_textureSettings.defaultTextureSize, 16.0f, 128.0f, "%.1f px");
            ImGui::SliderFloat("Default Scale", &m_textureSettings.defaultTextureScale, 0.1f, 3.0f, "%.1f");
            
            ImGui::Separator();
            ImGui::Text("Search Filters:");
            
            // Search input
            static char searchBuffer[256] = "";
            if (ImGui::InputText("Search", searchBuffer, sizeof(searchBuffer))) {
                m_textureSettings.searchFilter = std::string(searchBuffer);
                // Update filtered list
                extern WorldToScreenManager g_WorldToScreenManager;
                m_filteredTextures = g_WorldToScreenManager.GetTextureManager().SearchTextures(m_textureSettings.searchFilter);
            }
            
            // Category filters
            ImGui::Checkbox("Interface Textures", &m_textureSettings.showInterfaceTextures);
            ImGui::SameLine();
            ImGui::Checkbox("Spell Textures", &m_textureSettings.showSpellTextures);
            ImGui::Checkbox("Item Textures", &m_textureSettings.showItemTextures);
            ImGui::SameLine();
            ImGui::Checkbox("Environment Textures", &m_textureSettings.showEnvironmentTextures);
            
            // Update filtered list when filters change
            extern WorldToScreenManager g_WorldToScreenManager;
            m_filteredTextures = g_WorldToScreenManager.GetTextureManager().SearchTextures(m_textureSettings.searchFilter);
            
            ImGui::Separator();
            ImGui::Text("Available Textures:");
            
            // Texture list
            if (ImGui::BeginListBox("##TextureList", ImVec2(-1, 150))) {
                for (const auto& texturePath : m_filteredTextures) {
                    bool isSelected = (m_selectedTexturePath == texturePath);
                    if (ImGui::Selectable(texturePath.c_str(), isSelected)) {
                        m_selectedTexturePath = texturePath;
                    }
                    if (isSelected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndListBox();
            }
            
            // Add texture button
            if (ImGui::Button("Add Texture at Player Position") && !m_selectedTexturePath.empty()) {
                C3Vector playerPos;
                if (g_WorldToScreenManager.GetPlayerPositionSafe(playerPos)) {
                    D3DXVECTOR3 pos(playerPos.x, playerPos.y, playerPos.z + 2.0f); // Slightly above player
                    
                    // Extract filename for label
                    std::string label = m_selectedTexturePath;
                    size_t lastSlash = label.find_last_of("\\");
                    if (lastSlash != std::string::npos) {
                        label = label.substr(lastSlash + 1);
                    }
                    
                    g_WorldToScreenManager.GetTextureManager().AddTextureAtPosition(
                        m_selectedTexturePath, pos, m_textureSettings.defaultTextureSize, 0xFFFFFFFF, label);
                }
            }
            
            ImGui::SameLine();
            
            if (ImGui::Button("Add Texture at Target Position") && !m_selectedTexturePath.empty() && m_liveTargetGUID.IsValid()) {
                auto target = m_objectManager->GetObjectByGUID(m_liveTargetGUID);
                if (target) {
                    auto targetPos = target->GetPosition();
                    D3DXVECTOR3 pos(targetPos.x, targetPos.y, targetPos.z + 2.0f); // Slightly above target
                    
                    // Extract filename for label
                    std::string label = m_selectedTexturePath;
                    size_t lastSlash = label.find_last_of("\\");
                    if (lastSlash != std::string::npos) {
                        label = label.substr(lastSlash + 1);
                    }
                    
                    g_WorldToScreenManager.GetTextureManager().AddTextureAtPosition(
                        m_selectedTexturePath, pos, m_textureSettings.defaultTextureSize, 0xFFFFFFFF, label);
                }
            }
            
            ImGui::Separator();
            
            // Active Texture Management
            const auto& activeTextures = g_WorldToScreenManager.GetTextureManager().GetRenderTextures();
            if (!activeTextures.empty()) {
                ImGui::Text("Active Textures:");
                
                if (ImGui::BeginTable("ActiveTextures", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
                    ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 30.0f);
                    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableSetupColumn("Scale", ImGuiTableColumnFlags_WidthFixed, 100.0f);
                    ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, 60.0f);
                    ImGui::TableHeadersRow();
                    
                    for (const auto& texture : activeTextures) {
                        ImGui::TableNextRow();
                        
                        ImGui::TableSetColumnIndex(0);
                        ImGui::Text("%d", texture.id);
                        
                        ImGui::TableSetColumnIndex(1);
                        ImGui::Text("%s", texture.label.c_str());
                        
                        ImGui::TableSetColumnIndex(2);
                        static std::unordered_map<int, float> scaleValues;
                        if (scaleValues.find(texture.id) == scaleValues.end()) {
                            scaleValues[texture.id] = texture.scale;
                        }
                        
                        ImGui::PushID(texture.id);
                        if (ImGui::SliderFloat("##scale", &scaleValues[texture.id], 0.01f, 2.0f, "%.2f")) {
                            g_WorldToScreenManager.GetTextureManager().UpdateTextureScale(texture.id, scaleValues[texture.id]);
                        }
                        ImGui::PopID();
                        
                        ImGui::TableSetColumnIndex(3);
                        ImGui::PushID(texture.id);
                        if (ImGui::Button("X")) {
                            g_WorldToScreenManager.GetTextureManager().RemoveTexture(texture.id);
                            scaleValues.erase(texture.id);
                        }
                        ImGui::PopID();
                    }
                    
                    ImGui::EndTable();
                }
                
                ImGui::Separator();
            }
            
            // Texture statistics
            ImGui::Text("Texture Statistics:");
            ImGui::Text("Rendered Textures: %zu", g_WorldToScreenManager.GetTextureManager().GetRenderTextureCount());
            ImGui::Text("Available Textures: %zu", g_WorldToScreenManager.GetTextureManager().GetCommonTexturePaths().size());
            
            if (ImGui::Button("Clear All Textures")) {
                g_WorldToScreenManager.GetTextureManager().ClearAllTextures();
            }
            
            ImGui::SameLine();
            
            if (ImGui::Button("Hot Reload Textures")) {
                if (g_WorldToScreenManager.GetTextureManager().HotReloadTexturesFromSource()) {
                    // Update the filtered texture list after hot reload
                    m_filteredTextures = g_WorldToScreenManager.GetTextureManager().SearchTextures(m_textureSettings.searchFilter);
                }
            }
            
            ImGui::Unindent();
        }
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
            C3Vector playerPos;
            if (g_WorldToScreenManager.GetPlayerPositionSafe(playerPos)) {
                D3DXVECTOR3 testPos(playerPos.x, playerPos.y, playerPos.z);
                g_WorldToScreenManager.AddMarker(testPos, 0xFF00FF00, 30.0f, "TEST");
            }
        }
        
        ImGui::SameLine();
        
        if (ImGui::Button("Add Screen Test")) {
            // Add a test marker at a fixed world position for testing
            extern WorldToScreenManager g_WorldToScreenManager;
            C3Vector playerPos;
            if (g_WorldToScreenManager.GetPlayerPositionSafe(playerPos)) {
                // Add marker 10 units in front of player
                D3DXVECTOR3 testPos(playerPos.x + 10.0f, playerPos.y, playerPos.z);
                g_WorldToScreenManager.AddMarker(testPos, 0xFFFFFF00, 40.0f, "FRONT");
            }
        }
        
        ImGui::SameLine();
        
        if (ImGui::Button("Reset to Defaults")) {
            m_showPlayerArrow = true;
            m_showObjectNames = false;
            m_showDistances = false;
            m_showPlayerNames = true;
            m_showUnitNames = false;
            m_showGameObjectNames = false;
            m_showPlayerDistances = true;
            m_showUnitDistances = false;
            m_showGameObjectDistances = false;
            m_maxDrawDistance = 50.0f;
            m_onlyShowTargeted = false;
            m_showPlayerToTargetLine = true;
            m_textScale = 1.0f;
            m_arrowSize = 20;
            
            // Reset colors
            m_playerArrowColor[0] = 1.0f; m_playerArrowColor[1] = 0.0f; 
            m_playerArrowColor[2] = 0.0f; m_playerArrowColor[3] = 1.0f;
            
            m_textColor[0] = 1.0f; m_textColor[1] = 1.0f; 
            m_textColor[2] = 1.0f; m_textColor[3] = 1.0f;
            
            m_distanceColor[0] = 0.8f; m_distanceColor[1] = 0.8f; 
            m_distanceColor[2] = 0.8f; m_distanceColor[3] = 1.0f;
            
            m_lineColor[0] = 0.0f; m_lineColor[1] = 1.0f; 
            m_lineColor[2] = 0.0f; m_lineColor[3] = 1.0f;
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
            m_showPlayerDistances = true;
            m_showUnitDistances = false;
            m_showGameObjectDistances = false;
            m_maxDrawDistance = 50.0f;
            m_onlyShowTargeted = false;
            m_showPlayerToTargetLine = true;
            m_textScale = 1.0f;
            m_arrowSize = 20;
            
            // Reset colors
            m_playerArrowColor[0] = 1.0f; m_playerArrowColor[1] = 0.0f; 
            m_playerArrowColor[2] = 0.0f; m_playerArrowColor[3] = 1.0f;
            
            m_textColor[0] = 1.0f; m_textColor[1] = 1.0f; 
            m_textColor[2] = 1.0f; m_textColor[3] = 1.0f;
            
            m_distanceColor[0] = 0.8f; m_distanceColor[1] = 0.8f; 
            m_distanceColor[2] = 0.8f; m_distanceColor[3] = 1.0f;
            
            m_lineColor[0] = 0.0f; m_lineColor[1] = 1.0f; 
            m_lineColor[2] = 0.0f; m_lineColor[3] = 1.0f;
            
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
    C3Vector livePlayerPos;
    extern WorldToScreenManager g_WorldToScreenManager;
    if (g_WorldToScreenManager.GetPlayerPositionSafe(livePlayerPos)) {
        m_livePlayerPos = livePlayerPos; // new member to display
    }
    
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