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
    typedef int(__cdecl* LuaDoStringType)(const char*);
    static LuaDoStringType LuaDoString = nullptr;
    static DWORD lastLuaCall = 0;
    
    // Throttle Lua calls to prevent spam
    DWORD currentTime = GetTickCount();
    if (currentTime - lastLuaCall < 50) { // Max 20 calls per second
        return;
    }
    lastLuaCall = currentTime;
    
    if (!LuaDoString) {
        LuaDoString = reinterpret_cast<LuaDoStringType>(LUADOSTRING_ADDR);
        OutputDebugStringA("[Lua] LuaDoString function initialized");
    }
    
    if (!LuaDoString || IsBadCodePtr(reinterpret_cast<FARPROC>(LuaDoString))) {
        return;
    }
    
    // Use structured exception handling for safety
    __try {
        LuaDoString(luaCode.c_str());
    }
    __except(EXCEPTION_EXECUTE_HANDLER) {
        // Lua call failed - skip silently and try again next time
        static int errorCount = 0;
        if (errorCount++ < 5) { // Only log first 5 errors
            OutputDebugStringA("[Lua] Access violation during Lua execution");
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
    // Throttle updates to prevent excessive calls
    static DWORD lastUpdateTime = 0;
    DWORD currentTime = GetTickCount();
    if (currentTime - lastUpdateTime < 100) { // Update max once per 100ms
        return;
    }
    lastUpdateTime = currentTime;
    
    // Don't clear arrows - just reuse the same one to avoid excessive Lua calls
    if (arrows.empty()) {
        // Add single arrow at origin first time
        AddArrow(0.0f, 0.0f, 0.0f, "Player");
    }
    
    OutputDebugStringA("[WorldToScreen] Getting local player position...");
    
    // Get local player position
    C3Vector playerPos = GetLocalPlayerPosition();
    
    char debugMsg[256];
    sprintf_s(debugMsg, "[WorldToScreen] Got player position: (%.2f, %.2f, %.2f)", playerPos.x, playerPos.y, playerPos.z);
    OutputDebugStringA(debugMsg);
    
    if (!IsValidPlayerPosition(playerPos)) {
        OutputDebugStringA("[WorldToScreen] Player position is not valid, skipping arrow");
        return; // No valid player position, don't show arrow
    }
    
    OutputDebugStringA("[WorldToScreen] Player position is valid, updating arrow");
    
    // Update the existing arrow position instead of creating new ones
    if (!arrows.empty()) {
        arrows[0].worldX = playerPos.x;
        arrows[0].worldY = playerPos.y;
        arrows[0].worldZ = playerPos.z;
    }
    
    // Add safety checks to prevent access violations
    try {
        // Check if the WorldFrame pointer address is valid
        if (IsBadReadPtr(reinterpret_cast<void*>(WORLDFRAME_PTR), sizeof(uintptr_t))) {
            return;
        }
        
        // Get WorldFrame pointer
        uintptr_t worldFramePtrAddr = *reinterpret_cast<uintptr_t*>(WORLDFRAME_PTR);
        if (!worldFramePtrAddr || IsBadReadPtr(reinterpret_cast<void*>(worldFramePtrAddr), sizeof(CGWorldFrame))) {
            return;
        }
        
        CGWorldFrame* worldFrame = reinterpret_cast<CGWorldFrame*>(worldFramePtrAddr);
        
        // Check if WorldToScreen function address is valid
        if (IsBadCodePtr(reinterpret_cast<FARPROC>(WORLDTOSCREEN_ADDR))) {
            return;
        }
        
        WorldToScreenFn worldToScreenFn = reinterpret_cast<WorldToScreenFn>(WORLDTOSCREEN_ADDR);
        
        sprintf_s(debugMsg, "[WorldToScreen] Processing %d arrows", static_cast<int>(arrows.size()));
        OutputDebugStringA(debugMsg);
        
        for (auto& arrow : arrows) {
            C3Vector worldPos(arrow.worldX, arrow.worldY, arrow.worldZ);
            C3Vector screenPos;
            int outcode = 0;
            
            sprintf_s(debugMsg, "[WorldToScreen] Converting world pos (%.2f, %.2f, %.2f) to screen", 
                     worldPos.x, worldPos.y, worldPos.z);
            OutputDebugStringA(debugMsg);
            
            // Call the real WorldToScreen function with exception handling
            bool result = false;
            try {
                result = worldToScreenFn(worldFrame, &worldPos, &screenPos, &outcode);
            }
            catch (...) {
                OutputDebugStringA("[WorldToScreen] Exception caught during WorldToScreen call");
                // Handle access violation or other exceptions
                result = false;
            }
            
            sprintf_s(debugMsg, "[WorldToScreen] WorldToScreen result: %s, outcode: %d", 
                     result ? "SUCCESS" : "FAILED", outcode);
            OutputDebugStringA(debugMsg);
            
            if (result) {
                sprintf_s(debugMsg, "[WorldToScreen] Screen pos: (%.2f, %.2f)", screenPos.x, screenPos.y);
                OutputDebugStringA(debugMsg);
                // Convert normalized coordinates to screen pixels
                arrow.screenX = screenPos.x * 1920.0f; // Assuming 1920x1080, you may want to get actual screen size
                arrow.screenY = screenPos.y * 1080.0f;
                arrow.isVisible = true;
                
                // Send coordinates to Lua to create/update frame
                std::stringstream luaCode;
                luaCode << std::fixed << std::setprecision(2);
                
                luaCode << "local frameName = 'WorldToScreenPlayerArrow'; "; // Fixed name for single arrow
                luaCode << "local frame = _G[frameName]; ";
                luaCode << "if not frame then ";
                luaCode << "  frame = CreateFrame('Frame', frameName, UIParent); ";
                luaCode << "  frame:SetSize(30, 30); "; // Slightly larger for player arrow
                luaCode << "  local texture = frame:CreateTexture(nil, 'BACKGROUND'); ";
                luaCode << "  texture:SetAllPoints(); ";
                luaCode << "  texture:SetColorTexture(1, 0, 0, 0.9); "; // Red color for player
                luaCode << "  frame.texture = texture; ";
                luaCode << "  local text = frame:CreateFontString(nil, 'OVERLAY', 'GameFontNormal'); ";
                luaCode << "  text:SetPoint('BOTTOM', frame, 'TOP', 0, 2); ";
                luaCode << "  text:SetText('YOU'); ";
                luaCode << "  text:SetTextColor(1, 1, 0, 1); "; // Yellow text
                luaCode << "  frame.text = text; ";
                luaCode << "end; ";
                luaCode << "frame:SetPoint('CENTER', UIParent, 'BOTTOMLEFT', " << arrow.screenX << ", " << arrow.screenY << "); ";
                luaCode << "frame:Show(); ";
                
                sprintf_s(debugMsg, "[WorldToScreen] Final screen coords: (%.0f, %.0f)", arrow.screenX, arrow.screenY);
                OutputDebugStringA(debugMsg);
                
                OutputDebugStringA("[WorldToScreen] Executing Lua code to show arrow");
                ExecuteLuaCode(luaCode.str());
            } else {
                arrow.isVisible = false;
                
                OutputDebugStringA("[WorldToScreen] WorldToScreen failed, hiding arrow");
                
                // Hide the frame when not visible
                std::string luaCode = "local frame = _G['WorldToScreenPlayerArrow']; if frame then frame:Hide(); end";
                ExecuteLuaCode(luaCode);
            }
        }
    }
    catch (...) {
        // Handle any other exceptions silently to prevent crashes
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