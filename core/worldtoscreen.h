#pragma once

#include "types.h"
#include <vector>
#include <string>

struct ArrowData {
    float worldX, worldY, worldZ;
    float screenX, screenY;
    bool isVisible;
    std::string label;
    int id;
};

class WorldToScreenManager {
private:
    std::vector<ArrowData> arrows;
    int nextId;
    
    // Lua execution function
    void ExecuteLuaCode(const std::string& luaCode);
    
public:
    WorldToScreenManager();
    
    // Arrow management
    int AddArrow(float worldX, float worldY, float worldZ, const std::string& label = "");
    void RemoveArrow(int id);
    void ClearAllArrows();
    
    // Update and render
    void Update();
    
    // Get arrow data for external use
    const std::vector<ArrowData>& GetArrows() const { return arrows; }
}; 