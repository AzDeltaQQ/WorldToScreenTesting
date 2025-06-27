#pragma once

#include "../dependencies/ImGui/imgui.h"
#include "../dependencies/ImGui/backends/imgui_impl_dx11.h"
#include "../dependencies/ImGui/backends/imgui_impl_win32.h"

#include <memory>

// Forward declarations
class ObjectManager;

// Forward declarations for tabs
class CombatLogTab;
class DrawingTab;
class MovementTab;
class NavigationTab;

// Include headers for tabs that need to be instantiated
#include "tabs/CombatLogTab.h"
#include "tabs/DrawingTab.h"
#include "tabs/MovementTab.h"
#include "tabs/NavigationTab.h"

namespace GUI {
    class ObjectsTab;

    // Main GUI manager class
    class GUIManager {
    private:
        static std::unique_ptr<GUIManager> s_instance;
        
        // Tab instances
        std::unique_ptr<ObjectsTab> m_objectsTab;
        std::unique_ptr<DrawingTab> m_drawingTab;
        std::unique_ptr<CombatLogTab> m_combatLogTab;
        std::unique_ptr<MovementTab> m_movementTab;
        std::unique_ptr<NavigationTab> m_navigationTab;
        
        bool m_statusOverlayForceEnabled; // Independent overlay state - ONLY controlled by Settings tab
        ObjectManager* m_objectManager;

        // Status overlay helper methods
        void RenderStatusOverlay();
        bool IsCryotherapistActive() const;

    public:
        GUIManager();
        ~GUIManager();

        // Singleton management
        static GUIManager* GetInstance();
        static void Initialize();
        static void Shutdown();

        // Main interface
        void SetObjectManager(ObjectManager* objManager);
        void Update();
        void UpdateLogic();
        void Render(bool* p_open);

        // Status overlay control (independent of main GUI)
        void SetStatusOverlayEnabled(bool enabled) { m_statusOverlayForceEnabled = enabled; }
        bool IsStatusOverlayEnabled() const { return m_statusOverlayForceEnabled; }
        void ToggleStatusOverlay() { m_statusOverlayForceEnabled = !m_statusOverlayForceEnabled; }

        // Tab access
        ObjectsTab* GetObjectsTab() const { return m_objectsTab.get(); }
        DrawingTab* GetDrawingTab() const { return m_drawingTab.get(); }
        CombatLogTab* GetCombatLogTab() const { return m_combatLogTab.get(); }
        MovementTab* GetMovementTab() const { return m_movementTab.get(); }
        NavigationTab* GetNavigationTab() const { return m_navigationTab.get(); }
    };

    // Convenience functions
    void Initialize();
    void Shutdown();
    void Render(bool* p_open);
    
    // Status overlay convenience functions (independent of main GUI)
    void SetStatusOverlayEnabled(bool enabled);
    bool IsStatusOverlayEnabled();
    void ToggleStatusOverlay();
} 