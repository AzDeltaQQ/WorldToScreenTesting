#include "GUI.h"
#include "tabs/ObjectsTab.h"
#include "tabs/DrawingTab.h"
#include "tabs/CombatLogTab.h"
#include "tabs/MovementTab.h"
#include "tabs/NavigationTab.h"
#include "../objects/ObjectManager.h"
#include <imgui.h>
#include <memory>
#include <windows.h>

namespace GUI {

// Static member definition
std::unique_ptr<GUIManager> GUIManager::s_instance = nullptr;

GUIManager::GUIManager()
    : m_statusOverlayForceEnabled(false)  // Disable status overlay by default for this project
    , m_objectManager(nullptr)
{
    // Initialize tabs
    m_objectsTab = std::make_unique<ObjectsTab>();
    m_drawingTab = std::make_unique<DrawingTab>();
    m_combatLogTab = std::make_unique<CombatLogTab>();
    m_movementTab = std::make_unique<MovementTab>();
    m_navigationTab = std::make_unique<NavigationTab>();
}

GUIManager::~GUIManager() = default;

GUIManager* GUIManager::GetInstance() {
    if (!s_instance) {
        s_instance = std::make_unique<GUIManager>();
    }
    return s_instance.get();
}

void GUIManager::Initialize() {
    GetInstance(); // Ensure instance is created
}

void GUIManager::Shutdown() {
    s_instance.reset();
}

void GUIManager::SetObjectManager(ObjectManager* objManager) {
    m_objectManager = objManager;
    
    // Pass the ObjectManager to all tabs that need it
    if (m_objectsTab) {
        m_objectsTab->SetObjectManager(objManager);
    }
    if (m_drawingTab) {
        m_drawingTab->SetObjectManager(objManager);
    }
    if (m_combatLogTab) {
        m_combatLogTab->SetObjectManager(objManager);
    }
    if (m_movementTab) {
        m_movementTab->SetObjectManager(objManager);
    }
}

void GUIManager::Update() {
    // Calculate delta time
    static auto lastTime = ImGui::GetTime();
    auto currentTime = ImGui::GetTime();
    float deltaTime = static_cast<float>(currentTime - lastTime);
    lastTime = currentTime;
    
    // Update all tabs with controlled timing
    if (m_objectsTab) {
        m_objectsTab->Update(deltaTime);
    }
    if (m_drawingTab) {
        m_drawingTab->Update(deltaTime);
    }
    if (m_combatLogTab) {
        m_combatLogTab->Update(deltaTime);
    }
    if (m_movementTab) {
        m_movementTab->Update(deltaTime);
    }
    if (m_navigationTab) {
        m_navigationTab->Update();
    }
}

void GUIManager::UpdateLogic() {
    // Completely disable automatic updates to prevent UI interference
    // All data will be updated on-demand during rendering
    // This prevents any interference with ImGui's internal state management
}

bool GUIManager::IsCryotherapistActive() const {
    // For this project, just check if object manager is initialized
    return m_objectManager && m_objectManager->IsInitialized();
}

void GUIManager::RenderStatusOverlay() {
    // Status overlay disabled for this project
    if (!m_statusOverlayForceEnabled) {
        return;
    }

    // Create a small, non-intrusive overlay
    ImGuiWindowFlags window_flags = 
        ImGuiWindowFlags_NoDecoration | 
        ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoSavedSettings | 
        ImGuiWindowFlags_NoFocusOnAppearing | 
        ImGuiWindowFlags_NoNav |
        ImGuiWindowFlags_NoMove;

    // Position the overlay in the top-left corner
    const float DISTANCE = 10.0f;
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImVec2 work_pos = viewport->WorkPos;
    ImVec2 window_pos = ImVec2(work_pos.x + DISTANCE, work_pos.y + DISTANCE);
    ImGui::SetNextWindowPos(window_pos, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.85f); // Semi-transparent background

    if (ImGui::Begin("Core Status", nullptr, window_flags)) {
        bool isActive = IsCryotherapistActive();
        ImVec4 statusColor = isActive ? ImVec4(0.0f, 1.0f, 0.0f, 1.0f) : ImVec4(1.0f, 0.0f, 0.0f, 1.0f);
        const char* statusText = isActive ? "Active" : "Disabled";
        
        ImGui::Text("Object Manager: ");
        ImGui::SameLine();
        ImGui::TextColored(statusColor, "%s", statusText);
        
        // Show object count if available
        if (m_objectManager && m_objectManager->IsInitialized()) {
            auto allObjects = m_objectManager->GetAllObjects();
            ImGui::Text("Objects: %zu", allObjects.size());
        }
        
        ImGui::Separator();
        ImGui::Text("F1: Toggle GUI");
        ImGui::Text("F2: Toggle Overlay");
    }
    ImGui::End();
}

void GUIManager::Render(bool* p_open) {
    // The keybind processing is now done in hook.cpp's WndProc.
    
    // Always render the status overlay if enabled
    RenderStatusOverlay();
    
    // Only render main GUI if the visibility flag is true
    if (!p_open || !*p_open) {
        return;
    }

    // Main window - more compact size to reduce wasted space
    ImGui::SetNextWindowSize(ImVec2(650, 380), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(100, 100), ImGuiCond_FirstUseEver);
    
    // Apply global styling with tighter spacing
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6.0f, 6.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f, 3.0f));
    
    // Window flags that explicitly allow dragging and resizing
    ImGuiWindowFlags window_flags = ImGuiWindowFlags_MenuBar;
    
    if (ImGui::Begin("Object Manager", p_open, window_flags)) {
        
        // Menu bar
        if (ImGui::BeginMenuBar()) {
            if (ImGui::BeginMenu("View")) {
                ImGui::MenuItem("Hide Main Window", "F1", nullptr);
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Help")) {
                ImGui::MenuItem("About", nullptr, nullptr);
                ImGui::EndMenu();
            }
            ImGui::EndMenuBar();
        }

        if (ImGui::BeginTabBar("MainTabs", ImGuiTabBarFlags_None)) {
            
            // Objects Tab
            if (ImGui::BeginTabItem("Objects")) {
                if (m_objectsTab) {
                    m_objectsTab->Render();
                }
                ImGui::EndTabItem();
            }
            
            // Drawing Tab
            if (ImGui::BeginTabItem("Drawing")) {
                if (m_drawingTab) {
                    m_drawingTab->Render();
                }
                ImGui::EndTabItem();
            }
            
            // Combat Log Tab
            if (ImGui::BeginTabItem("Combat Log")) {
                if (m_combatLogTab) {
                    m_combatLogTab->Render();
                }
                ImGui::EndTabItem();
            }
            
            // Movement Tab
            if (ImGui::BeginTabItem("Movement")) {
                if (m_movementTab) {
                    m_movementTab->Render();
                }
                ImGui::EndTabItem();
            }
            
            // Navigation Tab
            if (ImGui::BeginTabItem("Navigation")) {
                if (m_navigationTab) {
                    m_navigationTab->Render();
                }
                ImGui::EndTabItem();
            }
            
            ImGui::EndTabBar();
        }
        
        // Status bar at the bottom
        ImGui::Separator();
        
        // ObjectManager status
        if (m_objectManager && m_objectManager->IsInitialized()) {
            auto allObjects = m_objectManager->GetAllObjects();
            ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "ObjectManager: Online (%zu objects)", allObjects.size());
        } else {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "ObjectManager: Offline");
        }
        
        ImGui::SameLine();
        ImGui::Text(" | ");
        ImGui::SameLine();
        
        // Performance info
        ImGui::Text("FPS: %.1f (%.2f ms)", ImGui::GetIO().Framerate, 1000.0f / ImGui::GetIO().Framerate);
        
    }
    ImGui::End();
    
    // Pop styling
    ImGui::PopStyleVar(4);
}

// Convenience function implementations
void Initialize() {
    GUIManager::Initialize();
}

void Shutdown() {
    GUIManager::Shutdown();
}

// Updated Render convenience function
void Render(bool* p_open) {
    GUIManager::GetInstance()->Render(p_open);
}

void SetStatusOverlayEnabled(bool enabled) {
    GUIManager::GetInstance()->SetStatusOverlayEnabled(enabled);
}

} // namespace GUI 