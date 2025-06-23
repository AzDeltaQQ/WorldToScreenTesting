#pragma once

#include "types.h"
#include <vector>
#include <string>

namespace WorldToScreen {
    // Arrow data structure
    struct Arrow {
        C3Vector worldPos;
        C3Vector screenPos;
        bool isVisible;
        float size;
        unsigned int color;
        std::string label;
        
        Arrow(float x, float y, float z, const std::string& lbl = "", float sz = 20.0f, unsigned int col = 0xFF00FF00)
            : worldPos(x, y, z), isVisible(false), size(sz), color(col), label(lbl) {}
    };
    
    // Initialize the arrow rendering system
    void Initialize();
    
    // Shutdown the arrow rendering system
    void Shutdown();
    
    // Add an arrow to be rendered
    void AddArrow(float worldX, float worldY, float worldZ, const std::string& label = "", float size = 20.0f, unsigned int color = 0xFF00FF00);
    
    // Remove all arrows
    void ClearArrows();
    
    // Update arrow positions (call from EndScene)
    void UpdateArrows();
    
    // Render arrows using ImGui draw list
    void RenderArrows();
    
    // Call the actual WoW WorldToScreen function
    bool CallWorldToScreen(const C3Vector& worldPos, C3Vector& screenPos, int* outcode = nullptr);
    
    // Render the simple control GUI
    void RenderGUI();
    
    // Get the WorldFrame pointer
    CGWorldFrame* GetWorldFrame();
    
    // Get the WorldToScreen function pointer
    WorldToScreenFn GetWorldToScreenFunction();
} 