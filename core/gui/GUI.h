#pragma once

#include <memory>

// Forward declarations
class ObjectManager;

namespace GUI {
    class ObjectsTab;
    class DrawingTab;
    class CombatLogTab;

    // Main GUI manager class
    class GUIManager {
    private:
        static std::unique_ptr<GUIManager> s_instance;
        
        // Tab instances
        std::unique_ptr<ObjectsTab> m_objectsTab;
        std::unique_ptr<DrawingTab> m_drawingTab;
        std::unique_ptr<CombatLogTab> m_combatLogTab;
        
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