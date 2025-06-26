#include "MovementTab.h"
#include "../../objects/ObjectManager.h"
#include "../../objects/WowPlayer.h"
#include "../../objects/WowUnit.h"
#include "../../objects/WowGameObject.h"
#include "../../movement/MovementController.h"
#include "../../types/types.h"
#include "../../logs/Logger.h"
#include "../../../dependencies/ImGui/imgui.h"
#include <sstream>
#include <iomanip>
#include <algorithm>

namespace GUI {

MovementTab::MovementTab()
    : m_objectManager(nullptr)
    , m_movementController(&MovementController::GetInstance())
    , m_isEnabled(true)
    , m_targetPosition{0.0f, 0.0f, 0.0f}
    , m_targetGuidBuffer{}
    , m_useCurrentPlayerPos(false)
    , m_enableLogging(true)
    , m_autoStop(true)
    , m_movementTimeout(10.0f)
    , m_showMovementState(true)
    , m_showAdvancedControls(false)
    , m_selectedQuickAction(-1)
{
    // Initialize GUID buffer
    memset(m_targetGuidBuffer, 0, sizeof(m_targetGuidBuffer));
    
    // Add some default quick actions
    AddQuickAction("Stormwind Bank", Vector3(-8842.09f, 628.65f, 94.24f));
    AddQuickAction("Goldshire Inn", Vector3(-9449.06f, 64.83f, 56.31f));
    AddQuickAction("Elwynn Forest", Vector3(-9447.8f, -1367.5f, 47.1f));
    
    LOG_INFO("MovementTab initialized");
}

void MovementTab::SetObjectManager(ObjectManager* objManager) {
    m_objectManager = objManager;
    
    // Initialize movement controller with object manager
    if (objManager) {
        MovementController::Initialize(objManager);
        LOG_INFO("MovementController auto-initialized with handlePlayerClickToMove!");
    }
}

void MovementTab::Update(float deltaTime) {
    if (!m_isEnabled) {
        return;
    }
    
    // Update movement controller
    if (m_movementController) {
        m_movementController->Update(deltaTime);
    }
    
    // Sync settings with movement controller
    if (m_movementController) {
        m_movementController->SetLoggingEnabled(m_enableLogging);
        m_movementController->SetAutoStopEnabled(m_autoStop);
        m_movementController->SetMovementTimeout(m_movementTimeout);
    }
}

void MovementTab::Render() {
    if (!m_isEnabled) {
        return;
    }
    
    if (ImGui::CollapsingHeader("Movement System", ImGuiTreeNodeFlags_DefaultOpen)) {
        
        // Status display
        if (m_movementController && m_movementController->IsInitialized()) {
            ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Status: Initialized");
        } else {
            ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "Status: Not Initialized");
        }
        
        ImGui::Separator();
        
        // Basic Controls
        RenderBasicControls();
        
        ImGui::Separator();
        
        // Advanced Controls (collapsible)
        if (ImGui::CollapsingHeader("Advanced Controls")) {
            RenderAdvancedControls();
        }
        
        // Movement State Display
        if (ImGui::CollapsingHeader("Movement State")) {
            RenderMovementState();
        }
        
        // Quick Actions
        if (ImGui::CollapsingHeader("Quick Actions")) {
            RenderQuickActions();
        }
        
        // Target Selection
        if (ImGui::CollapsingHeader("Target Selection")) {
            RenderTargetSelection();
        }
        
        // Settings
        if (ImGui::CollapsingHeader("Settings")) {
            RenderSettings();
        }
        
        // Testing Controls
        if (ImGui::CollapsingHeader("Testing Controls")) {
            RenderTestingControls();
        }
    }
}

void MovementTab::RenderBasicControls() {
    ImGui::Text("Basic Movement Controls");
    
    // Position input
    ImGui::Text("Target Position:");
    ImGui::InputFloat3("##TargetPos", m_targetPosition);
    
    // Current player position button
    ImGui::SameLine();
    if (ImGui::Button("Current Pos")) {
        Vector3 playerPos = GetPlayerPosition();
        m_targetPosition[0] = playerPos.x;
        m_targetPosition[1] = playerPos.y;
        m_targetPosition[2] = playerPos.z;
    }
    
    // Movement buttons
    if (ImGui::Button("Move to Position")) {
        ExecuteClickToMove();
    }
    
    ImGui::SameLine();
    if (ImGui::Button("Stop")) {
        ExecuteStop();
    }
    
    ImGui::SameLine();
    if (ImGui::Button("Face Position")) {
        ExecuteFacePosition();
    }
    
    // GUID input
    ImGui::Text("Target GUID:");
    ImGui::InputText("##TargetGUID", m_targetGuidBuffer, sizeof(m_targetGuidBuffer));
    
    // GUID-based actions
    if (ImGui::Button("Move to Object")) {
        ExecuteMoveToObject();
    }
    
    ImGui::SameLine();
    if (ImGui::Button("Interact")) {
        ExecuteInteractWithObject();
    }
    
    ImGui::SameLine();
    if (ImGui::Button("Attack")) {
        ExecuteAttackTarget();
    }
    
    ImGui::SameLine();
    if (ImGui::Button("Face Target")) {
        ExecuteFaceTarget();
    }
}

void MovementTab::RenderAdvancedControls() {
    ImGui::Text("Advanced Movement Controls");
    
    // Right-click simulation
    if (ImGui::Button("Right Click at Position")) {
        ExecuteRightClick();
    }
    
    // Use current player position toggle
    ImGui::Checkbox("Use Current Player Position", &m_useCurrentPlayerPos);
    
    if (m_useCurrentPlayerPos) {
        Vector3 playerPos = GetPlayerPosition();
        ImGui::Text("Player Position: (%.2f, %.2f, %.2f)", playerPos.x, playerPos.y, playerPos.z);
        
        // Distance to target
        Vector3 targetPos(m_targetPosition[0], m_targetPosition[1], m_targetPosition[2]);
        float distance = playerPos.Distance(targetPos);
        ImGui::Text("Distance to Target: %.2f yards", distance);
    }
}

void MovementTab::RenderMovementState() {
    if (!m_movementController) {
        ImGui::Text("Movement controller not available");
        return;
    }
    
    const auto& state = m_movementController->GetState();
    
    ImGui::Text("Is Moving: %s", state.isMoving ? "Yes" : "No");
    ImGui::Text("Has Destination: %s", state.hasDestination ? "Yes" : "No");
    ImGui::Text("Current Action: %s", FormatMovementAction(state.currentAction).c_str());
    
    if (state.targetGUID != 0) {
        ImGui::Text("Target GUID: 0x%llX", state.targetGUID);
    }
    
    if (state.hasDestination) {
        const auto& dest = state.currentDestination;
        ImGui::Text("Destination: %s", FormatPosition(dest).c_str());
    }
    
    // Movement timing info
    auto now = std::chrono::steady_clock::now();
    if (state.isMoving) {
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - state.movementStartTime).count();
        ImGui::Text("Movement Time: %lld seconds", elapsed);
        
        float timeoutRemaining = m_movementController->GetMovementTimeout() - elapsed;
        ImGui::Text("Timeout in: %.1f seconds", timeoutRemaining);
    }
}

void MovementTab::RenderQuickActions() {
    ImGui::Text("Quick Action Positions");
    
    // Quick action list
    for (size_t i = 0; i < m_quickActions.size(); ++i) {
        const auto& action = m_quickActions[i];
        
        bool isSelected = (m_selectedQuickAction == static_cast<int>(i));
        if (ImGui::Selectable(action.name.c_str(), isSelected)) {
            m_selectedQuickAction = static_cast<int>(i);
        }
        
        if (isSelected && action.isValid) {
            ImGui::Text("Position: %s", FormatPosition(action.position).c_str());
            
            if (ImGui::Button("Load Position")) {
                m_targetPosition[0] = action.position.x;
                m_targetPosition[1] = action.position.y;
                m_targetPosition[2] = action.position.z;
            }
            
            ImGui::SameLine();
            if (ImGui::Button("Move There")) {
                m_targetPosition[0] = action.position.x;
                m_targetPosition[1] = action.position.y;
                m_targetPosition[2] = action.position.z;
                ExecuteClickToMove();
            }
        }
    }
    
    // Add current position as quick action
    if (ImGui::Button("Save Current Position")) {
        SaveCurrentPositionAsQuickAction();
    }
}

void MovementTab::RenderTargetSelection() {
    if (!m_objectManager) {
        ImGui::Text("Object manager not available");
        return;
    }
    
    ImGui::Text("Nearby Objects");
    
    // Get nearby objects
    auto localPlayer = m_objectManager->GetLocalPlayer();
    if (!localPlayer) {
        ImGui::Text("Local player not found");
        return;
    }
    
    Vector3 playerPos = GetPlayerPosition();
    
    // Display nearby units
    if (ImGui::TreeNode("Units")) {
        auto units = m_objectManager->GetAllUnits();
        for (const auto& unit : units) {
            if (!unit || unit == localPlayer) continue;
            
            Vector3 unitPos = unit->GetPosition();
            Vector3 unitPosC3(unitPos.x, unitPos.y, unitPos.z);
            float distance = playerPos.Distance(unitPosC3);
            
            if (distance <= 100.0f) { // Only show units within 100 yards
                std::string name = unit->GetName();
                if (name.empty()) name = "Unknown Unit";
                
                std::string label = name + " (GUID: 0x" + std::to_string(unit->GetGUID64()) + 
                                  ", Dist: " + std::to_string(distance) + ")";
                
                if (ImGui::Selectable(label.c_str())) {
                    // Set GUID in buffer
                    snprintf(m_targetGuidBuffer, sizeof(m_targetGuidBuffer), "%llX", unit->GetGUID64());
                }
            }
        }
        ImGui::TreePop();
    }
    
    // Display nearby game objects
    if (ImGui::TreeNode("Game Objects")) {
        auto gameObjects = m_objectManager->GetAllGameObjects();
        for (const auto& obj : gameObjects) {
            if (!obj) continue;
            
            Vector3 objPos = obj->GetPosition();
            Vector3 objPosC3(objPos.x, objPos.y, objPos.z);
            float distance = playerPos.Distance(objPosC3);
            
            if (distance <= 50.0f) { // Only show objects within 50 yards
                std::string name = obj->GetName();
                if (name.empty()) name = "Unknown Object";
                
                std::string label = name + " (GUID: 0x" + std::to_string(obj->GetGUID64()) + 
                                  ", Dist: " + std::to_string(distance) + ")";
                
                if (ImGui::Selectable(label.c_str())) {
                    // Set GUID in buffer
                    snprintf(m_targetGuidBuffer, sizeof(m_targetGuidBuffer), "%llX", obj->GetGUID64());
                }
            }
        }
        ImGui::TreePop();
    }
}

void MovementTab::RenderSettings() {
    ImGui::Text("Movement Settings");
    
    if (ImGui::Checkbox("Enable Logging", &m_enableLogging) && m_movementController) {
        m_movementController->SetLoggingEnabled(m_enableLogging);
    }
    
    if (ImGui::Checkbox("Auto Stop", &m_autoStop) && m_movementController) {
        m_movementController->SetAutoStopEnabled(m_autoStop);
    }
    
    if (ImGui::SliderFloat("Movement Timeout", &m_movementTimeout, 1.0f, 60.0f, "%.1f seconds") && m_movementController) {
        m_movementController->SetMovementTimeout(m_movementTimeout);
    }
    

    
    ImGui::Separator();
    
    if (ImGui::Button("Load Settings")) {
        LoadSettings();
    }
    
    ImGui::SameLine();
    if (ImGui::Button("Save Settings")) {
        SaveSettings();
    }
}

void MovementTab::RenderTestingControls() {
    ImGui::Text("Testing and Diagnostics");
    
    if (m_movementController) {
        if (ImGui::Button("Print Status")) {
            std::string status = m_movementController->GetStatusString();
            LOG_INFO("Movement Controller Status:\n" + status);
        }
        
        ImGui::SameLine();
        if (ImGui::Button("Reset State")) {
            m_movementController->Stop();
        }
    }
    
    // Manual CTM testing
    if (ImGui::TreeNode("Manual CTM Testing")) {
        static int testAction = 4; // MOVE_TO_POSITION
        static uint64_t testGuid = 0;
        
        ImGui::InputInt("Action Type", &testAction);
        ImGui::InputScalar("Test GUID", ImGuiDataType_U64, &testGuid, nullptr, nullptr, "%llX", ImGuiInputTextFlags_CharsHexadecimal);
        
        if (ImGui::Button("Execute Test")) {
            // This would require direct CTM memory access
            LOG_INFO("Manual CTM test requested - Action: " + std::to_string(testAction) + 
                    ", GUID: 0x" + std::to_string(testGuid));
        }
        
        ImGui::TreePop();
    }
}

void MovementTab::ExecuteClickToMove() {
    if (!m_movementController) {
        LOG_ERROR("Movement controller not available");
        return;
    }
    
    Vector3 targetPos(m_targetPosition[0], m_targetPosition[1], m_targetPosition[2]);
    
    if (m_movementController->ClickToMove(targetPos)) {
        LOG_INFO("Click-to-Move executed successfully");
    } else {
        LOG_ERROR("Failed to execute Click-to-Move");
    }
}

void MovementTab::ExecuteMoveToObject() {
    if (!m_movementController) {
        LOG_ERROR("Movement controller not available");
        return;
    }
    
    uint64_t targetGuid;
    if (!ParseGUID(std::string(m_targetGuidBuffer), targetGuid)) {
        LOG_ERROR("Invalid GUID format");
        return;
    }
    
    if (m_movementController->MoveToObject(targetGuid)) {
        LOG_INFO("Move to object executed successfully");
    } else {
        LOG_ERROR("Failed to execute move to object");
    }
}

void MovementTab::ExecuteInteractWithObject() {
    if (!m_movementController) {
        LOG_ERROR("Movement controller not available");
        return;
    }
    
    uint64_t targetGuid;
    if (!ParseGUID(std::string(m_targetGuidBuffer), targetGuid)) {
        LOG_ERROR("Invalid GUID format");
        return;
    }
    
    if (m_movementController->InteractWithObject(targetGuid)) {
        LOG_INFO("Interact with object executed successfully");
    } else {
        LOG_ERROR("Failed to execute interact with object");
    }
}

void MovementTab::ExecuteAttackTarget() {
    if (!m_movementController) {
        LOG_ERROR("Movement controller not available");
        return;
    }
    
    uint64_t targetGuid;
    if (!ParseGUID(std::string(m_targetGuidBuffer), targetGuid)) {
        LOG_ERROR("Invalid GUID format");
        return;
    }
    
    if (m_movementController->AttackTarget(targetGuid)) {
        LOG_INFO("Attack target executed successfully");
    } else {
        LOG_ERROR("Failed to execute attack target");
    }
}

void MovementTab::ExecuteStop() {
    if (!m_movementController) {
        LOG_ERROR("Movement controller not available");
        return;
    }
    
    m_movementController->Stop();
    LOG_INFO("Stop command executed");
}

void MovementTab::ExecuteFaceTarget() {
    if (!m_movementController) {
        LOG_ERROR("Movement controller not available");
        return;
    }
    
    uint64_t targetGuid;
    if (!ParseGUID(std::string(m_targetGuidBuffer), targetGuid)) {
        LOG_ERROR("Invalid GUID format");
        return;
    }
    
    m_movementController->FaceTarget(targetGuid);
    LOG_INFO("Face target executed");
}

void MovementTab::ExecuteFacePosition() {
    if (!m_movementController) {
        LOG_ERROR("Movement controller not available");
        return;
    }
    
    Vector3 targetPos(m_targetPosition[0], m_targetPosition[1], m_targetPosition[2]);
    m_movementController->FacePosition(targetPos);
    LOG_INFO("Face position executed");
}

void MovementTab::ExecuteRightClick() {
    if (!m_movementController) {
        LOG_ERROR("Movement controller not available");
        return;
    }
    
    Vector3 targetPos(m_targetPosition[0], m_targetPosition[1], m_targetPosition[2]);
    m_movementController->RightClickAt(targetPos);
    LOG_INFO("Right click executed");
}

bool MovementTab::ParseGUID(const std::string& guidStr, uint64_t& outGuid) {
    if (guidStr.empty()) {
        return false;
    }
    
    try {
        // Try parsing as hexadecimal
        outGuid = std::stoull(guidStr, nullptr, 16);
        return true;
    } catch (...) {
        try {
            // Try parsing as decimal
            outGuid = std::stoull(guidStr, nullptr, 10);
            return true;
        } catch (...) {
            return false;
        }
    }
}

void MovementTab::AddQuickAction(const std::string& name, const Vector3& position) {
    QuickAction action;
    action.name = name;
    action.position = position;
    action.isValid = true;
    m_quickActions.push_back(action);
}

void MovementTab::SaveCurrentPositionAsQuickAction() {
    Vector3 playerPos = GetPlayerPosition();
    if (!playerPos.IsZero()) {
        std::string name = "Saved Position " + std::to_string(m_quickActions.size() + 1);
        AddQuickAction(name, playerPos);
        LOG_INFO("Saved current position as quick action: " + name);
    } else {
        LOG_ERROR("Failed to get current player position");
    }
}

Vector3 MovementTab::GetPlayerPosition() {
    if (!m_objectManager) {
        return Vector3();
    }
    
    auto localPlayer = m_objectManager->GetLocalPlayer();
    if (!localPlayer) {
        return Vector3();
    }
    
    Vector3 pos = localPlayer->GetPosition();
    return Vector3(pos.x, pos.y, pos.z);
}

std::string MovementTab::FormatPosition(const Vector3& pos) {
    std::stringstream ss;
    ss << std::fixed << std::setprecision(2);
    ss << "(" << pos.x << ", " << pos.y << ", " << pos.z << ")";
    return ss.str();
}

std::string MovementTab::FormatMovementAction(uint32_t action) {
    switch (action) {
        case 1: return "Face Target";
        case 2: return "Face Destination";
        case 3: return "Stop";
        case 4: return "Move to Position";
        case 5: return "Attack Position";
        case 6: return "Interact Object";
        case 7: return "Attack GUID";
        case 8: return "Loot Object";
        case 9: return "Move to Object";
        case 10: return "Skin Object";
        default: return "Unknown (" + std::to_string(action) + ")";
    }
}

void MovementTab::LoadSettings() {
    // TODO: Implement settings loading from file or registry
    LOG_INFO("Loading movement settings (not implemented)");
}

void MovementTab::SaveSettings() {
    // TODO: Implement settings saving to file or registry
    LOG_INFO("Saving movement settings (not implemented)");
}

} // namespace GUI
