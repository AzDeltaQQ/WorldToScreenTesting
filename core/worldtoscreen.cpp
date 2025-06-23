#include "worldtoscreen.h"
#include <imgui.h>
#include <imgui_impl_dx9.h>
#include <imgui_impl_win32.h>
#include <algorithm>
#include <Windows.h>

namespace WorldToScreen {
    // Global state
    static std::vector<Arrow> g_arrows;
    static bool g_initialized = false;
    static bool g_showControlPanel = true;
    
    // Private helper to get the game's WorldFrame pointer
    CGWorldFrame* GetWorldFrame() {
        CGWorldFrame** worldFramePtr = reinterpret_cast<CGWorldFrame**>(GameOffsets::WORLD_FRAME_PTR);
        return *worldFramePtr;
    }
    
    // Private helper to get the WorldToScreen function
    WorldToScreenFn GetWorldToScreenFunction() {
        return reinterpret_cast<WorldToScreenFn>(GameOffsets::WORLD_TO_SCREEN_FUNC);
    }
    
    void Initialize() {
        if (g_initialized) return;
        
        // Add test arrows at better positions for visibility
        // These should be more visible in the world
        AddArrow(0.0f, 0.0f, 0.0f, "Origin", 25.0f, 0xFF0000FF);
        AddArrow(5.0f, 0.0f, 0.0f, "East 5", 20.0f, 0xFF00FF00);
        AddArrow(0.0f, 5.0f, 0.0f, "North 5", 20.0f, 0xFFFF0000);
        AddArrow(0.0f, 0.0f, 2.0f, "Up 2", 20.0f, 0xFFFFFF00);
        
        g_initialized = true;
    }
    
    void Shutdown() {
        g_arrows.clear();
        g_initialized = false;
    }
    
    void AddArrow(float worldX, float worldY, float worldZ, const std::string& label, float size, unsigned int color) {
        g_arrows.emplace_back(worldX, worldY, worldZ, label, size, color);
    }
    
    void ClearArrows() {
        g_arrows.clear();
    }
    
    bool CallWorldToScreen(const C3Vector& worldPos, C3Vector& screenPos, int* outcode) {
        CGWorldFrame* worldFrame = GetWorldFrame();
        WorldToScreenFn worldToScreenFn = GetWorldToScreenFunction();
        
        if (!worldFrame || !worldToScreenFn) {
            return false;
        }
        
        // Call the actual WoW WorldToScreen function
        return worldToScreenFn(worldFrame, &worldPos, &screenPos, outcode);
    }
    
    void UpdateArrows() {
        if (!g_initialized) return;
        
        // Get screen dimensions for converting normalized coordinates to pixels
        // Use ImGui's display size which should match the game window
        ImGuiIO& io = ImGui::GetIO();
        float screenWidth = io.DisplaySize.x;
        float screenHeight = io.DisplaySize.y;
        
        for (auto& arrow : g_arrows) {
            int outcode = 0;
            C3Vector normalizedPos;
            bool result = CallWorldToScreen(arrow.worldPos, normalizedPos, &outcode);
            
            if (result) {
                // Convert normalized coordinates (0-1) to actual screen pixels
                arrow.screenPos.x = normalizedPos.x * screenWidth;
                arrow.screenPos.y = normalizedPos.y * screenHeight;
                
                // Check if the converted coordinates are within reasonable screen bounds
                if (arrow.screenPos.x >= 0 && arrow.screenPos.x <= screenWidth &&
                    arrow.screenPos.y >= 0 && arrow.screenPos.y <= screenHeight) {
                    arrow.isVisible = true;
                } else {
                    arrow.isVisible = false;
                }
            } else {
                arrow.isVisible = false;
            }
        }
    }
    
    void RenderArrows() {
        if (!g_initialized) return;
        
        ImDrawList* drawList = ImGui::GetBackgroundDrawList();
        if (!drawList) return;
        
        for (const auto& arrow : g_arrows) {
            if (!arrow.isVisible) continue;
            
            const float x = arrow.screenPos.x;
            const float y = arrow.screenPos.y;
            const float size = arrow.size;
            const unsigned int color = arrow.color;
            
            // Draw triangle arrow pointing down
            ImVec2 p1(x, y - size * 0.5f);
            ImVec2 p2(x - size * 0.4f, y + size * 0.3f);
            ImVec2 p3(x + size * 0.4f, y + size * 0.3f);
            
            drawList->AddTriangleFilled(p1, p2, p3, color);
            drawList->AddTriangle(p1, p2, p3, 0xFF000000, 2.0f);
            
            // Draw label if present
            if (!arrow.label.empty()) {
                ImVec2 textPos(x - ImGui::CalcTextSize(arrow.label.c_str()).x * 0.5f, y + size * 0.4f);
                drawList->AddText(textPos, 0xFFFFFFFF, arrow.label.c_str());
            }
        }
    }
    
    void RenderGUI() {
        if (!g_showControlPanel) return;
        
        ImGui::SetNextWindowSize(ImVec2(400, 300), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("WorldToScreen Test Panel", &g_showControlPanel)) {
            ImGui::Text("WorldToScreen Arrow Test");
            ImGui::Separator();
            
            ImGui::Text("Total Arrows: %d", (int)g_arrows.size());
            
            if (ImGui::Button("Clear All Arrows")) {
                ClearArrows();
            }
            
            ImGui::SameLine();
            if (ImGui::Button("Add Test Arrows")) {
                ClearArrows();
                Initialize();
            }
            
            ImGui::Separator();
            
            // Manual arrow addition
            static float addPos[3] = {0.0f, 0.0f, 0.0f};
            static char labelBuffer[64] = "Test Arrow";
            static float arrowSize = 20.0f;
            static float arrowColor[4] = {0.0f, 1.0f, 0.0f, 1.0f};
            
            ImGui::Text("Add New Arrow:");
            ImGui::InputFloat3("World Position", addPos);
            ImGui::InputText("Label", labelBuffer, sizeof(labelBuffer));
            ImGui::SliderFloat("Size", &arrowSize, 5.0f, 50.0f);
            ImGui::ColorEdit4("Color", arrowColor);
            
            if (ImGui::Button("Add Arrow")) {
                unsigned int color = ImGui::ColorConvertFloat4ToU32(ImVec4(arrowColor[0], arrowColor[1], arrowColor[2], arrowColor[3]));
                AddArrow(addPos[0], addPos[1], addPos[2], std::string(labelBuffer), arrowSize, color);
            }
            
            ImGui::Separator();
            
            // Arrow list
            if (ImGui::CollapsingHeader("Arrow List")) {
                for (size_t i = 0; i < g_arrows.size(); ++i) {
                    const auto& arrow = g_arrows[i];
                    ImGui::PushID((int)i);
                    
                    ImGui::Text("%.1f, %.1f, %.1f -> %.1f, %.1f (%s) - %s", 
                        arrow.worldPos.x, arrow.worldPos.y, arrow.worldPos.z,
                        arrow.screenPos.x, arrow.screenPos.y,
                        arrow.isVisible ? "Visible" : "Hidden",
                        arrow.label.empty() ? "No Label" : arrow.label.c_str());
                    
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Remove")) {
                        g_arrows.erase(g_arrows.begin() + i);
                        ImGui::PopID();
                        break;
                    }
                    
                    ImGui::PopID();
                }
            }
            
            ImGui::Separator();
            
            // Debug info
            if (ImGui::CollapsingHeader("Debug Info")) {
                CGWorldFrame* worldFrame = GetWorldFrame();
                ImGui::Text("WorldFrame Pointer: 0x%p", worldFrame);
                ImGui::Text("WorldToScreen Function: 0x%08X", GameOffsets::WORLD_TO_SCREEN_FUNC);
                
                if (worldFrame) {
                    ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "WorldFrame Valid: Yes");
                } else {
                    ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "WorldFrame Valid: No");
                }
                
                ImGui::Separator();
                ImGui::Text("Screen Resolution Info:");
                ImGui::Text("Expected arrows near your character's feet");
                ImGui::Text("(0,0,0) = world origin, may be far from player");
                
                if (ImGui::Button("Add Arrows Near Origin")) {
                    ClearArrows();
                    // Add arrows in a small area around origin
                    for (int x = -2; x <= 2; x++) {
                        for (int y = -2; y <= 2; y++) {
                            AddArrow((float)x, (float)y, 0.0f, 
                                   "(" + std::to_string(x) + "," + std::to_string(y) + ")", 
                                   15.0f, 0xFF00FFFF);
                        }
                    }
                }
            }
            
            ImGui::Separator();
            ImGui::Text("Coordinate Conversion Info:");
            ImGui::Text("WorldToScreen returns normalized coords (0-1)");
            ImGui::Text("We multiply by screen size for pixels");
            ImGuiIO& io = ImGui::GetIO();
            ImGui::Text("Screen Size: %.0f x %.0f", io.DisplaySize.x, io.DisplaySize.y);
            
            if (ImGui::Button("Add Center Screen Arrow")) {
                // Add an arrow that should appear in the center of screen
                // We need to find world coordinates that project to screen center
                AddArrow(0.0f, 0.0f, 0.0f, "Center Test", 30.0f, 0xFFFF00FF);
            }
        }
        ImGui::End();
    }
}

// Export functions for EndScene submission
extern "C" {
    void UpdateArrowsEndScene() {
        WorldToScreen::UpdateArrows();
    }
    
    void RenderArrowsEndScene() {
        WorldToScreen::RenderArrows();
        WorldToScreen::RenderGUI();
    }
} 