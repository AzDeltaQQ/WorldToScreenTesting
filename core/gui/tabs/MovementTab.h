#pragma once

#include <memory>
#include <string>
#include <vector>
#include "../../types/types.h"
#include "../../movement/MovementController.h"

// Forward declarations
class ObjectManager;

namespace GUI {

class MovementTab {
private:
    ObjectManager* m_objectManager;
    MovementController* m_movementController;
    
    // UI State
    bool m_isEnabled;
    
    // Movement testing
    float m_targetPosition[3]; // X, Y, Z for manual input
    char m_targetGuidBuffer[32]; // Buffer for GUID input
    bool m_useCurrentPlayerPos;
    
    // Settings
    bool m_enableLogging;
    bool m_autoStop;
    float m_movementTimeout;
    
    // Status display
    bool m_showMovementState;
    bool m_showAdvancedControls;
    
    // Quick actions
    struct QuickAction {
        std::string name;
        Vector3 position;
        bool isValid;
    };
    std::vector<QuickAction> m_quickActions;
    int m_selectedQuickAction;
    
    // Helper methods
    void RenderBasicControls();
    void RenderAdvancedControls();
    void RenderMovementState();
    void RenderQuickActions();
    void RenderSettings();
    void RenderTargetSelection();
    void RenderTestingControls();
    
    // Action handlers
    void ExecuteClickToMove();
    void ExecuteMoveToObject();
    void ExecuteInteractWithObject();
    void ExecuteAttackTarget();
    void ExecuteStop();
    void ExecuteFaceTarget();
    void ExecuteFacePosition();
    void ExecuteRightClick();
    
    // Utility
    bool ParseGUID(const std::string& guidStr, uint64_t& outGuid);
    void AddQuickAction(const std::string& name, const Vector3& position);
    void SaveCurrentPositionAsQuickAction();
    Vector3 GetPlayerPosition();
    std::string FormatPosition(const Vector3& pos);
    std::string FormatMovementAction(uint32_t action);

public:
    MovementTab();
    ~MovementTab() = default;
    
    // Core tab interface
    void SetObjectManager(ObjectManager* objManager);
    void Render();
    void Update(float deltaTime);
    
    // Tab state
    bool IsEnabled() const { return m_isEnabled; }
    void SetEnabled(bool enabled) { m_isEnabled = enabled; }
    
    // Settings access
    void LoadSettings();
    void SaveSettings();
};
} 