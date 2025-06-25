#include "ObjectsTab.h"
#include "../../objects/ObjectManager.h"
#include "../../objects/WowObject.h"
#include "../../objects/WowUnit.h"
#include "../../objects/WowGameObject.h"
#include "../../objects/WowPlayer.h"
#include <imgui.h>
#include <algorithm>
#include <sstream>
#include <iomanip>

namespace GUI {

ObjectsTab::ObjectsTab()
    : m_objectManager(nullptr)
    , m_showPlayers(true)
    , m_showUnits(true)
    , m_showGameObjects(true)
    , m_showOther(false)
    , m_maxDistance(200.0f)
    , m_nameFilter("")
    , m_selectedObjectGuid()
    , m_totalObjectCount(0)
    , m_visibleObjectCount(0)
    , m_playerCount(0)
    , m_unitCount(0)
    , m_gameObjectCount(0)
    , m_refreshInterval(3.0f) // Refresh every 3 seconds instead of 1 second
    , m_timeSinceLastRefresh(0.0f)
    , m_needsRefresh(true)
{
}

void ObjectsTab::SetObjectManager(ObjectManager* objManager) {
    m_objectManager = objManager;
    m_needsRefresh = true;
}

void ObjectsTab::Update(float deltaTime) {
    // Only update if we have a valid object manager
    if (!m_objectManager) {
        return;
    }
    
    m_timeSinceLastRefresh += deltaTime;
    
    // Update cached statistics periodically to avoid interfering with UI
    if (m_timeSinceLastRefresh >= m_refreshInterval) {
        UpdateStats(); // Update cached statistics
        m_timeSinceLastRefresh = 0.0f;
        m_needsRefresh = false;
    }
}

void ObjectsTab::UpdateFilteredObjects() {
    // This is now called only when rendering, not in Update()
    // Just reset the counter - actual filtering happens during render
    m_visibleObjectCount = 0;
}

void ObjectsTab::UpdateStats() {
    if (!m_objectManager) {
        m_totalObjectCount = 0;
        m_playerCount = 0;
        m_unitCount = 0;
        m_gameObjectCount = 0;
        m_visibleObjectCount = 0;
        m_cachedFilteredObjects.clear();
        return;
    }
    
    auto allObjects = m_objectManager->GetAllObjects();
    m_totalObjectCount = static_cast<int>(allObjects.size());
    
    m_playerCount = 0;
    m_unitCount = 0;
    m_gameObjectCount = 0;
    m_visibleObjectCount = 0;
    
    // Clear and rebuild cached objects
    m_cachedFilteredObjects.clear();
    
    for (const auto& pair : allObjects) {
        auto obj = pair.second;
        if (!obj) continue;
        
        // Count all objects by type
        switch (obj->GetObjectType()) {
            case OBJECT_PLAYER:
                m_playerCount++;
                break;
            case OBJECT_UNIT:
                m_unitCount++;
                break;
            case OBJECT_GAMEOBJECT:
                m_gameObjectCount++;
                break;
        }
        
        // Apply filters
        if (!PassesTypeFilter(obj->GetObjectType())) {
            continue;
        }
        
        std::string objName = obj->GetName();
        if (!PassesNameFilter(objName)) {
            continue;
        }
        
        float distance = CalculateDistanceToPlayer(obj);
        if (!PassesDistanceFilter(distance)) {
            continue;
        }
        
        // Create cached object info
        CachedObjectInfo cachedInfo;
        cachedInfo.object = obj;
        cachedInfo.distance = distance;
        
        std::stringstream ss;
        ss << objName << " (" << FormatDistance(distance) << ")";
        cachedInfo.displayText = ss.str();
        
        m_cachedFilteredObjects.push_back(cachedInfo);
        m_visibleObjectCount++;
    }

    // Sort cached objects by distance (nearest first)
    std::sort(m_cachedFilteredObjects.begin(), m_cachedFilteredObjects.end(),
        [](const CachedObjectInfo& a, const CachedObjectInfo& b) {
            return a.distance < b.distance;
        });
}

void ObjectsTab::ForceStatsUpdate() {
    // Force update stats without affecting UI state
    UpdateStats();
}

void ObjectsTab::Render() {
    if (!m_objectManager) {
        ImGui::Text("ObjectManager not initialized");
        return;
    }
    
    // Render filter controls
    RenderFilterControls();
    
    ImGui::Separator();
    
    // Optimized split view: more compact proportions
    if (ImGui::BeginTable("ObjectsLayout", 2, ImGuiTableFlags_Resizable)) {
        ImGui::TableSetupColumn("Objects", ImGuiTableColumnFlags_WidthFixed, 280.0f);
        ImGui::TableSetupColumn("Details", ImGuiTableColumnFlags_WidthStretch);
        
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        RenderObjectList();
        
        ImGui::TableSetColumnIndex(1);
        RenderObjectDetails();
        
        ImGui::EndTable();
    }
    
    // Compact stats overlay at bottom - fixed height child so it stays visible
    ImGui::Separator();
    if (ImGui::BeginChild("StatsBar", ImVec2(0, 18), false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
        ImGui::Text("Total: %d | Players: %d | Units: %d | GameObjects: %d | Visible: %d", 
                    m_totalObjectCount, m_playerCount, m_unitCount, m_gameObjectCount, m_visibleObjectCount);
    }
    ImGui::EndChild();
}

void ObjectsTab::RenderFilterControls() {
    ImGui::Text("Object Filters:");
    
    // Type filters
    bool filtersChanged = false;
    filtersChanged |= ImGui::Checkbox("Players", &m_showPlayers);
    ImGui::SameLine();
    filtersChanged |= ImGui::Checkbox("Units", &m_showUnits);
    ImGui::SameLine();
    filtersChanged |= ImGui::Checkbox("GameObjects", &m_showGameObjects);
    ImGui::SameLine();
    filtersChanged |= ImGui::Checkbox("Other", &m_showOther);
    
    if (filtersChanged) {
        UpdateStats(); // Immediate update when filter changes
    }
    
    // Distance filter
    if (ImGui::SliderFloat("Max Distance", &m_maxDistance, 1.0f, 200.0f, "%.1f")) {
        UpdateStats(); // Immediate update when filter changes
    }
    
    // Name filter
    char nameBuffer[256];
    strncpy_s(nameBuffer, m_nameFilter.c_str(), sizeof(nameBuffer) - 1);
    nameBuffer[sizeof(nameBuffer) - 1] = '\0';
    
    if (ImGui::InputText("Name Filter", nameBuffer, sizeof(nameBuffer))) {
        m_nameFilter = std::string(nameBuffer);
        UpdateStats(); // Immediate update when filter changes
    }
    
    ImGui::SameLine();
    if (ImGui::Button("Clear")) {
        m_nameFilter.clear();
        UpdateStats(); // Immediate update when filter changes
    }
}

void ObjectsTab::RenderObjectList() {
    ImGui::Text("Objects:");
    
    if (!m_objectManager->IsInitialized()) {
        ImGui::Text("ObjectManager not initialized");
        return;
    }
    
    // Give the list a fixed, smaller height to make it scrollable sooner,
    // and add a border to make the region clear.
    // A height of 120px will show about 5-6 items.
    if (ImGui::BeginChild("ObjectList", ImVec2(0, 120.0f), true)) {
        
        for (const auto& cachedInfo : m_cachedFilteredObjects) {
            auto obj = cachedInfo.object;
            if (!obj) continue;
            
            // Create object entry using cached display text
            bool isSelected = (obj->GetGUID() == m_selectedObjectGuid);
            
            if (ImGui::Selectable(cachedInfo.displayText.c_str(), isSelected)) {
                SelectObject(obj->GetGUID());
            }
            
            if (ImGui::IsItemHovered()) {
                ImGui::BeginTooltip();
                ImGui::Text("GUID: %s", FormatGUID(obj->GetGUID64()).c_str());
                ImGui::Text("Type: %d", static_cast<int>(obj->GetObjectType()));
                auto pos = obj->GetPosition();
                ImGui::Text("Position: (%.2f, %.2f, %.2f)", pos.x, pos.y, pos.z);
                ImGui::Text("Distance: %.1f yards", cachedInfo.distance);
                ImGui::EndTooltip();
            }
        }
    }
    ImGui::EndChild();
    
    ImGui::Text("Showing: %d / %d objects", m_visibleObjectCount, m_totalObjectCount);
}

void ObjectsTab::RenderObjectDetails() {
    ImGui::Text("Object Details:");
    
    if (!m_selectedObjectGuid.IsValid()) {
        ImGui::TextDisabled("No object selected");
        return;
    }
    
    auto selectedObj = m_objectManager->GetObjectByGUID(m_selectedObjectGuid);
    if (!selectedObj) {
        ImGui::TextDisabled("Selected object not found");
        return;
    }
    
    if (ImGui::BeginChild("ObjectDetails")) {
        // Basic info in a more compact format
        ImGui::Text("Name: %s", selectedObj->GetName().c_str());
        ImGui::Text("GUID: %s", FormatGUID(selectedObj->GetGUID64()).c_str());
        ImGui::Text("Type: %d", static_cast<int>(selectedObj->GetObjectType()));
        
        auto pos = selectedObj->GetPosition();
        ImGui::Text("Pos: (%.1f, %.1f, %.1f)", pos.x, pos.y, pos.z);
        
        float distance = CalculateDistanceToPlayer(selectedObj);
        ImGui::Text("Distance: %s", FormatDistance(distance).c_str());
        
        // Type-specific details in compact format
        if (auto unit = std::dynamic_pointer_cast<WowUnit>(selectedObj)) {
            ImGui::Separator();
            ImGui::Text("Unit Info:");
            ImGui::Text("HP: %u/%u (%.0f%%)", 
                       unit->GetHealth(), unit->GetMaxHealth(), unit->GetHealthPercent());
            ImGui::Text("Level: %u | Combat: %s | Alive: %s", 
                       unit->GetLevel(), 
                       unit->IsInCombat() ? "Yes" : "No",
                       unit->IsAlive() ? "Yes" : "No");
        }
        
        if (auto gameObj = std::dynamic_pointer_cast<WowGameObject>(selectedObj)) {
            ImGui::Separator();
            ImGui::Text("GameObject Info:");
            // Add GameObject-specific details here
        }
    }
    ImGui::EndChild();
}

void ObjectsTab::SelectObject(const WGUID& guid) {
    m_selectedObjectGuid = guid;
}

bool ObjectsTab::PassesTypeFilter(WowObjectType type) const {
    switch (type) {
        case OBJECT_PLAYER:
            return m_showPlayers;
        case OBJECT_UNIT:
            return m_showUnits;
        case OBJECT_GAMEOBJECT:
            return m_showGameObjects;
        default:
            return m_showOther;
    }
}

bool ObjectsTab::PassesNameFilter(const std::string& objectName) const {
    if (m_nameFilter.empty()) {
        return true;
    }
    
    // Handle empty object names - allow them to pass if no specific filter is set
    if (objectName.empty()) {
        return true; // Changed: allow empty names to pass through
    }
    
    // Case-insensitive search
    std::string lowerName = objectName;
    std::string lowerFilter = m_nameFilter;
    std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);
    std::transform(lowerFilter.begin(), lowerFilter.end(), lowerFilter.begin(), ::tolower);
    
    return lowerName.find(lowerFilter) != std::string::npos;
}

bool ObjectsTab::PassesDistanceFilter(float distance) const {
    return distance <= m_maxDistance;
}

float ObjectsTab::CalculateDistanceToPlayer(std::shared_ptr<WowObject> obj) const {
    if (!obj || !m_objectManager) {
        return 999.0f;
    }
    
    auto localPlayer = m_objectManager->GetLocalPlayer();
    if (!localPlayer) {
        // If no local player, return a reasonable distance so objects still show
        // This allows viewing objects even when local player detection fails
        return 50.0f; // Changed from 999.0f to 50.0f
    }
    
    auto playerPos = localPlayer->GetPosition();
    auto objPos = obj->GetPosition();
    
    // Check for zero positions which indicate invalid data
    if (playerPos.IsZero() || objPos.IsZero()) {
        return 50.0f; // Return reasonable distance for invalid positions
    }
    
    float distance = playerPos.Distance(objPos);
    
    // Clamp unreasonable distances
    if (distance > 1000.0f || distance < 0.0f) {
        return 50.0f;
    }
    
    return distance;
}

std::string ObjectsTab::FormatDistance(float distance) const {
    if (distance < 1000.0f) {
        std::stringstream ss;
        ss << std::fixed << std::setprecision(1) << distance << " yd";
        return ss.str();
    } else {
        return "Far";
    }
}

std::string ObjectsTab::FormatGUID(uint64_t guid) const {
    std::stringstream ss;
    ss << "0x" << std::hex << std::uppercase << guid;
    return ss.str();
}

} // namespace GUI 