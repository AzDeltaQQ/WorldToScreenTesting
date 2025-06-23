#include "worldtoscreen.h"
#include "types.h"
#include <iostream>
#include <sstream>
#include <iomanip>

// Global instance
WorldToScreenManager g_WorldToScreenManager;

WorldToScreenManager::WorldToScreenManager() : nextId(1) {
}

void WorldToScreenManager::ExecuteLuaCode(const std::string& luaCode) {
    // Get the Lua state from WoW
    // WoW's Lua state is typically accessible through various methods
    // For now, we'll use a simple approach - you may need to adjust based on your WoW version
    
    typedef int(__cdecl* LuaDoStringType)(const char*);
    static LuaDoStringType LuaDoString = nullptr;
    
    if (!LuaDoString) {
        // Use the address from types.h
        LuaDoString = reinterpret_cast<LuaDoStringType>(LUADOSTRING_ADDR);
    }
    
    if (LuaDoString) {
        try {
            LuaDoString(luaCode.c_str());
        } catch (...) {
            // Handle any exceptions silently
        }
    }
}

int WorldToScreenManager::AddArrow(float worldX, float worldY, float worldZ, const std::string& label) {
    ArrowData arrow;
    arrow.worldX = worldX;
    arrow.worldY = worldY;
    arrow.worldZ = worldZ;
    arrow.screenX = 0;
    arrow.screenY = 0;
    arrow.isVisible = false;
    arrow.label = label.empty() ? ("Arrow" + std::to_string(nextId)) : label;
    arrow.id = nextId++;
    
    arrows.push_back(arrow);
    return arrow.id;
}

void WorldToScreenManager::RemoveArrow(int id) {
    arrows.erase(
        std::remove_if(arrows.begin(), arrows.end(),
            [id](const ArrowData& arrow) { return arrow.id == id; }),
        arrows.end()
    );
    
    // Send Lua command to hide the frame
    std::string luaCode = "local frame = _G['WorldToScreenArrow" + std::to_string(id) + "']; if frame then frame:Hide(); end";
    ExecuteLuaCode(luaCode);
}

void WorldToScreenManager::ClearAllArrows() {
    // Hide all existing frames
    for (const auto& arrow : arrows) {
        std::string luaCode = "local frame = _G['WorldToScreenArrow" + std::to_string(arrow.id) + "']; if frame then frame:Hide(); end";
        ExecuteLuaCode(luaCode);
    }
    
    arrows.clear();
    nextId = 1;
}

void WorldToScreenManager::Update() {
    // Get WorldFrame and WorldToScreen function
    CGWorldFrame* worldFrame = reinterpret_cast<CGWorldFrame*>(*reinterpret_cast<uintptr_t*>(WORLDFRAME_PTR));
    if (!worldFrame) return;
    
    WorldToScreenFn worldToScreenFn = reinterpret_cast<WorldToScreenFn>(WORLDTOSCREEN_ADDR);
    if (!worldToScreenFn) return;
    
    for (auto& arrow : arrows) {
        C3Vector worldPos(arrow.worldX, arrow.worldY, arrow.worldZ);
        C3Vector screenPos;
        int outcode = 0;
        
        // Call the real WorldToScreen function
        bool result = worldToScreenFn(worldFrame, &worldPos, &screenPos, &outcode);
        
        if (result) {
            // Convert normalized coordinates to screen pixels
            arrow.screenX = screenPos.x * 1920.0f; // Assuming 1920x1080, you may want to get actual screen size
            arrow.screenY = screenPos.y * 1080.0f;
            arrow.isVisible = true;
            
            // Send coordinates to Lua to create/update frame
            std::stringstream luaCode;
            luaCode << std::fixed << std::setprecision(2);
            
            luaCode << "local frameName = 'WorldToScreenArrow" << arrow.id << "'; ";
            luaCode << "local frame = _G[frameName]; ";
            luaCode << "if not frame then ";
            luaCode << "  frame = CreateFrame('Frame', frameName, UIParent); ";
            luaCode << "  frame:SetSize(20, 20); ";
            luaCode << "  local texture = frame:CreateTexture(nil, 'BACKGROUND'); ";
            luaCode << "  texture:SetAllPoints(); ";
            luaCode << "  texture:SetColorTexture(0, 1, 0, 0.8); "; // Green color
            luaCode << "  frame.texture = texture; ";
            luaCode << "  local text = frame:CreateFontString(nil, 'OVERLAY', 'GameFontNormal'); ";
            luaCode << "  text:SetPoint('BOTTOM', frame, 'TOP', 0, 2); ";
            luaCode << "  text:SetText('" << arrow.label << "'); ";
            luaCode << "  frame.text = text; ";
            luaCode << "end; ";
            luaCode << "frame:SetPoint('CENTER', UIParent, 'BOTTOMLEFT', " << arrow.screenX << ", " << (1080.0f - arrow.screenY) << "); ";
            luaCode << "frame:Show(); ";
            
            ExecuteLuaCode(luaCode.str());
        } else {
            arrow.isVisible = false;
            
            // Hide the frame when not visible
            std::string luaCode = "local frame = _G['WorldToScreenArrow" + std::to_string(arrow.id) + "']; if frame then frame:Hide(); end";
            ExecuteLuaCode(luaCode);
        }
    }
}

// Global functions for easy access
extern "C" {
    __declspec(dllexport) int AddWorldToScreenArrow(float x, float y, float z, const char* label) {
        return g_WorldToScreenManager.AddArrow(x, y, z, label ? label : "");
    }
    
    __declspec(dllexport) void RemoveWorldToScreenArrow(int id) {
        g_WorldToScreenManager.RemoveArrow(id);
    }
    
    __declspec(dllexport) void ClearWorldToScreenArrows() {
        g_WorldToScreenManager.ClearAllArrows();
    }
    
    __declspec(dllexport) void UpdateWorldToScreenArrows() {
        g_WorldToScreenManager.Update();
    }
} 