#include "drawing.h"
#include "types.h"
#include "memory/memory.h"
#include "Logger.h"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <Windows.h>
#include "objects/ObjectManager.h"

// Global instance
WorldToScreenManager g_WorldToScreenManager;

WorldToScreenManager::WorldToScreenManager() 
    : nextId(1), m_pDevice(nullptr), m_pLine(nullptr), m_pFont(nullptr) {
}

WorldToScreenManager::~WorldToScreenManager() {
    Cleanup();
}

bool WorldToScreenManager::Initialize(LPDIRECT3DDEVICE9 pDevice) {
    if (!pDevice) {
        LOG_ERROR("pDevice is null");
        return false;
    }
    
    m_pDevice = pDevice;
    
    // Create ID3DXLine for drawing lines
    HRESULT hr = D3DXCreateLine(m_pDevice, &m_pLine);
    if (FAILED(hr)) {
        LOG_ERROR("Failed to create ID3DXLine, HRESULT: 0x" + std::to_string(hr));
        return false;
    }
    
    // Create font for text rendering
    hr = D3DXCreateFont(m_pDevice, 14, 0, FW_NORMAL, 1, FALSE, DEFAULT_CHARSET,
                        OUT_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                        L"Arial", &m_pFont);
    if (FAILED(hr)) {
        LOG_ERROR("Failed to create ID3DXFont, HRESULT: 0x" + std::to_string(hr));
        // Continue without font - not critical
    }
    
    LOG_INFO("Initialized successfully with Direct3D 9");
    
    // Add player arrow by default
    if (showPlayerArrow) {
        AddPlayerArrow();
    }
    
    return true;
}

void WorldToScreenManager::Cleanup() {
    if (m_pLine) {
        m_pLine->Release();
        m_pLine = nullptr;
    }
    
    if (m_pFont) {
        m_pFont->Release();
        m_pFont = nullptr;
    }
    
    m_pDevice = nullptr;
    lines.clear();
    markers.clear();
}

void WorldToScreenManager::OnDeviceLost() {
    if (m_pLine) {
        m_pLine->OnLostDevice();
    }
    if (m_pFont) {
        m_pFont->OnLostDevice();
    }
}

void WorldToScreenManager::OnDeviceReset() {
    if (m_pLine) {
        m_pLine->OnResetDevice();
    }
    if (m_pFont) {
        m_pFont->OnResetDevice();
    }
}

bool WorldToScreenManager::WorldToScreen(const D3DXVECTOR3& worldPos, D3DXVECTOR2& screenPos) {
    if (!m_pDevice) {
        return false;
    }
    
    try {
        // Get viewport for screen dimensions
        D3DVIEWPORT9 viewport;
        if (FAILED(m_pDevice->GetViewport(&viewport))) {
            static int failCount1 = 0;
            if (++failCount1 % 600 == 0) {
                LOG_DEBUG("WorldToScreen: GetViewport failed");
            }
            return false;
        }
        
        // Get WorldFrame pointer (it's a double pointer)
        CGWorldFrame** ppWorldFrame = reinterpret_cast<CGWorldFrame**>(WORLDFRAME_PTR);
        if (!ppWorldFrame || !*ppWorldFrame) {
            static int failCount2 = 0;
            if (++failCount2 % 600 == 0) {
                LOG_DEBUG("WorldToScreen: WorldFrame pointer is null");
            }
            return false;
        }
        
        CGWorldFrame* pWorldFrame = *ppWorldFrame;
        
        // Get camera position and matrices from WoW's data structures
        CCamera* pCamera = reinterpret_cast<CCamera*>(pWorldFrame->pActiveCamera);
        if (!pCamera) {
            static int failCount3 = 0;
            if (++failCount3 % 600 == 0) {
                LOG_DEBUG("WorldToScreen: Camera pointer is null");
            }
            return false;
        }
        
        // Read camera position
        D3DXVECTOR3 cameraPos(pCamera->position[0], pCamera->position[1], pCamera->position[2]);
        
        // Calculate relative position (world position relative to camera)
        D3DXVECTOR3 relativePos = worldPos - cameraPos;
        
        // Use WoW's view-projection matrix from WorldFrame+0x340
        D3DXMATRIX* pViewProjMatrix = reinterpret_cast<D3DXMATRIX*>(&pWorldFrame->viewProjectionMatrix[0]);
        
        // Transform the relative position using WoW's matrix
        D3DXVECTOR4 clipSpacePos;
        D3DXVECTOR3 homogeneousInput(relativePos.x, relativePos.y, relativePos.z);
        D3DXVec3Transform(&clipSpacePos, &homogeneousInput, pViewProjMatrix);
        
        // Check for valid W component (avoid division by zero)
        if (clipSpacePos.w <= 0.0001f) {
            static int failCount4 = 0;
            if (++failCount4 % 600 == 0) {
                LOG_DEBUG("WorldToScreen: Point behind camera (w=" + std::to_string(clipSpacePos.w) + ")");
            }
            return false;
        }
        
        // Perform perspective division to get normalized device coordinates (NDC)
        float ndcX = clipSpacePos.x / clipSpacePos.w;
        float ndcY = clipSpacePos.y / clipSpacePos.w;
        float ndcZ = clipSpacePos.z / clipSpacePos.w;
        
        // Check depth bounds (NDC Z should be between 0 and 1)
        if (ndcZ < 0.0f || ndcZ > 1.0f) {
            static int failCount5 = 0;
            if (++failCount5 % 600 == 0) {
                LOG_DEBUG("WorldToScreen: Point outside depth bounds (z=" + std::to_string(ndcZ) + ")");
            }
            return false;
        }
        
        // Convert NDC to screen coordinates
        // NDC range is [-1,1] for X and Y, convert to [0, viewport.Width] and [0, viewport.Height]
        screenPos.x = (ndcX + 1.0f) * 0.5f * viewport.Width;
        screenPos.y = (1.0f - ndcY) * 0.5f * viewport.Height; // Flip Y axis
        
        // Optional bounds check with margin (allow slightly off-screen objects)
        float margin = 100.0f;
        if (screenPos.x < -margin || screenPos.x > viewport.Width + margin ||
            screenPos.y < -margin || screenPos.y > viewport.Height + margin) {
            static int failCount6 = 0;
            if (++failCount6 % 600 == 0) {
                LOG_DEBUG("WorldToScreen: Point outside screen bounds (x=" + std::to_string(screenPos.x) + 
                         ", y=" + std::to_string(screenPos.y) + ", viewport=" + 
                         std::to_string(viewport.Width) + "x" + std::to_string(viewport.Height) + ")");
            }
            return false;
        }
        
        // Debug log occasionally
        static int debugCount = 0;
        if (++debugCount % 600 == 0) {
            LOG_DEBUG("Custom WorldToScreen success: world(" + std::to_string(worldPos.x) + "," + 
                     std::to_string(worldPos.y) + "," + std::to_string(worldPos.z) + 
                     ") -> screen(" + std::to_string(screenPos.x) + "," + 
                     std::to_string(screenPos.y) + ") ndc(" + std::to_string(ndcX) + "," + 
                     std::to_string(ndcY) + "," + std::to_string(ndcZ) + ")");
        }
        
        return true;
        
    }
    catch (...) {
        static int failCount7 = 0;
        if (++failCount7 % 600 == 0) {
            LOG_DEBUG("WorldToScreen: Exception caught");
        }
        return false;
    }
}

// Public WorldToScreen function for GUI use
bool WorldToScreenManager::WorldToScreen(float worldX, float worldY, float worldZ, float* screenX, float* screenY) {
    if (!screenX || !screenY) {
        return false;
    }
    
    D3DXVECTOR3 worldPos(worldX, worldY, worldZ);
    D3DXVECTOR2 screenPos;
    
    if (WorldToScreen(worldPos, screenPos)) {
        *screenX = screenPos.x;
        *screenY = screenPos.y;
        return true;
    }
    
    return false;
}

void WorldToScreenManager::DrawLine(const D3DXVECTOR2& start, const D3DXVECTOR2& end, D3DCOLOR color, float thickness) {
    if (!m_pLine) {
        return;
    }
        
    D3DXVECTOR2 points[2] = { start, end };
    
    m_pLine->SetWidth(thickness);
    m_pLine->Begin();
    m_pLine->Draw(points, 2, color);
    m_pLine->End();
}

void WorldToScreenManager::DrawMarker(const D3DXVECTOR2& pos, D3DCOLOR color, float size) {
    if (!m_pLine) {
        return;
    }
        
    float halfSize = size * 0.5f;
    
    // Draw cross marker
    D3DXVECTOR2 horizontal[2] = {
        D3DXVECTOR2(pos.x - halfSize, pos.y),
        D3DXVECTOR2(pos.x + halfSize, pos.y)
    };
    
    D3DXVECTOR2 vertical[2] = {
        D3DXVECTOR2(pos.x, pos.y - halfSize),
        D3DXVECTOR2(pos.x, pos.y + halfSize)
    };
    
    m_pLine->SetWidth(2.0f);
    m_pLine->Begin();
    m_pLine->Draw(horizontal, 2, color);
    m_pLine->Draw(vertical, 2, color);
    m_pLine->End();
}

void WorldToScreenManager::DrawTriangleArrow(const D3DXVECTOR2& pos, D3DCOLOR color, float size) {
    if (!m_pLine) {
        return;
    }
    
    float halfSize = size * 0.5f;
    
    // Draw triangle pointing up
    D3DXVECTOR2 triangle[4] = {
        D3DXVECTOR2(pos.x, pos.y - halfSize),           // Top point
        D3DXVECTOR2(pos.x - halfSize, pos.y + halfSize), // Bottom left
        D3DXVECTOR2(pos.x + halfSize, pos.y + halfSize), // Bottom right
        D3DXVECTOR2(pos.x, pos.y - halfSize)            // Back to top (close triangle)
    };
    
    m_pLine->SetWidth(3.0f);
    m_pLine->Begin();
    m_pLine->Draw(triangle, 4, color);
    m_pLine->End();
}

void WorldToScreenManager::DrawText(const std::string& text, const D3DXVECTOR2& pos, D3DCOLOR color) {
    if (!m_pFont || text.empty()) {
        return;
    }
        
    RECT rect;
    rect.left = static_cast<LONG>(pos.x - 50);
    rect.top = static_cast<LONG>(pos.y - 20);
    rect.right = static_cast<LONG>(pos.x + 50);
    rect.bottom = static_cast<LONG>(pos.y + 20);
    
    // Convert string to wide string
    std::wstring wtext(text.begin(), text.end());
    
    m_pFont->DrawText(nullptr, wtext.c_str(), -1, &rect, DT_CENTER | DT_VCENTER, color);
}

int WorldToScreenManager::AddLine(const D3DXVECTOR3& start, const D3DXVECTOR3& end, D3DCOLOR color, float thickness, const std::string& label) {
    LineData line;
    line.start = start;
    line.end = end;
    line.color = color;
    line.thickness = thickness;
    line.isVisible = true;
    line.id = nextId++;
    line.label = label.empty() ? ("Line" + std::to_string(line.id)) : label;
    
    lines.push_back(line);
    return line.id;
}

void WorldToScreenManager::RemoveLine(int id) {
    lines.erase(
        std::remove_if(lines.begin(), lines.end(),
            [id](const LineData& line) { return line.id == id; }),
        lines.end()
    );
}

void WorldToScreenManager::ClearAllLines() {
    lines.clear();
}

int WorldToScreenManager::AddMarker(const D3DXVECTOR3& worldPos, D3DCOLOR color, float size, const std::string& label) {
    MarkerData marker;
    marker.worldPos = worldPos;
    marker.color = color;
    marker.size = size;
    marker.isVisible = true;
    marker.id = nextId++;
    marker.label = label.empty() ? ("Marker" + std::to_string(marker.id)) : label;
    
    markers.push_back(marker);
    return marker.id;
}

void WorldToScreenManager::RemoveMarker(int id) {
    markers.erase(
        std::remove_if(markers.begin(), markers.end(),
            [id](const MarkerData& marker) { return marker.id == id; }),
        markers.end()
    );
}

void WorldToScreenManager::ClearAllMarkers() {
    markers.clear();
}

int WorldToScreenManager::AddPlayerArrow(D3DCOLOR color, float size) {
    // Get player position
    C3Vector playerPos;
    if (!GetPlayerPositionSafe(playerPos)) {
        LOG_WARNING("Could not get player position for arrow");
        return -1;
    }
    
    // Use the same coordinate conversion as other parts of the system
    // Note: C3Vector might have different coordinate system than Vector3
    D3DXVECTOR3 playerD3DPos(playerPos.x, playerPos.y, playerPos.z);
    
    LOG_DEBUG("Adding player arrow at position (" + std::to_string(playerPos.x) + ", " + 
             std::to_string(playerPos.y) + ", " + std::to_string(playerPos.z) + ")");
    
    return AddMarker(playerD3DPos, color, size, "YOU");
}

void WorldToScreenManager::UpdatePlayerArrow() {
    if (!showPlayerArrow) {
        return;
    }
    
    // Find the player arrow marker (labeled "YOU")
    for (auto& marker : markers) {
        if (marker.label == "YOU") {
            // Update position
            C3Vector playerPos;
            if (GetPlayerPositionSafe(playerPos)) {
                marker.worldPos = D3DXVECTOR3(playerPos.x, playerPos.y, playerPos.z);
                
                // Debug log occasionally
                static int updateCounter = 0;
                if (++updateCounter % 600 == 0) {
                    LOG_DEBUG("Updated player arrow position to (" + std::to_string(playerPos.x) + ", " + 
                             std::to_string(playerPos.y) + ", " + std::to_string(playerPos.z) + ")");
                }
            }
            break;
        }
    }
}

int WorldToScreenManager::AddPlayerToEnemyLine(const D3DXVECTOR3& enemyPos, D3DCOLOR color) {
    // Get player position
    C3Vector playerPos;
    if (!GetPlayerPositionSafe(playerPos)) {
        return -1;
    }
    
    D3DXVECTOR3 playerD3DPos(playerPos.x, playerPos.y, playerPos.z);
    return AddLine(playerD3DPos, enemyPos, color, 2.0f, "PlayerToEnemy");
}



// helper to convert internal Vector3 (x=game Y) to game world coords for D3D
static D3DXVECTOR3 ToGameWorld(const Vector3& v) {
    return D3DXVECTOR3(v.y, v.x, v.z); // swap back X/Y
}

void WorldToScreenManager::Update() {
    static int updateCount = 0;
    updateCount++;
    
    // Handle dynamic player-to-target line
    static int targetLineId = -1;
    auto objMgr = ObjectManager::GetInstance();
    if (objMgr && objMgr->IsInitialized()) {
        auto player = objMgr->GetLocalPlayer();
        if (player) {
            uint64_t tgtGuid64 = Memory::Read<uint64_t>(GameOffsets::CURRENT_TARGET_GUID_ADDR);
            WGUID tgtGuid;
            if (tgtGuid64 != 0) {
                tgtGuid = WGUID(tgtGuid64);
            } else {
                tgtGuid = player->GetTargetGUID(); // fallback to cached value
            }
            if (tgtGuid.IsValid()) {
                auto targetObj = objMgr->GetObjectByGUID(tgtGuid);
                if (targetObj) {
                    // Get player position using C3Vector (correct coordinate system)
                    C3Vector playerPosC3;
                    if (GetPlayerPositionSafe(playerPosC3)) {
                        // Get target position using Vector3 (now same coordinate system) 
                        Vector3 tgtPosV = targetObj->GetPosition();
                        
                        // Convert to D3DXVECTOR3: Both are now in the same coordinate system
                        D3DXVECTOR3 pPos(playerPosC3.x, playerPosC3.y, playerPosC3.z);
                        D3DXVECTOR3 tPos(tgtPosV.x, tgtPosV.y, tgtPosV.z);
                        
                        // Debug log position updates occasionally
                        static int posUpdateCounter = 0;
                        if (++posUpdateCounter % 300 == 0) {
                            float distance = sqrt(pow(pPos.x - tPos.x, 2) + pow(pPos.y - tPos.y, 2) + pow(pPos.z - tPos.z, 2));
                            LOG_DEBUG("PlayerToTarget positions: Player(" + std::to_string(pPos.x) + "," + std::to_string(pPos.y) + "," + std::to_string(pPos.z) + 
                                     ") Target(" + std::to_string(tPos.x) + "," + std::to_string(tPos.y) + "," + std::to_string(tPos.z) + 
                                     ") Distance=" + std::to_string(distance) + " yards");
                        }
                        
                        // Remove old line each frame to keep simple
                        if (targetLineId != -1) RemoveLine(targetLineId);
                        targetLineId = AddLine(pPos, tPos, 0xFFFF0000, 2.0f, "PlayerToTarget");
                    }
                }
            } else {
                // No target
                if (targetLineId != -1) { RemoveLine(targetLineId); targetLineId = -1; }
            }
        }
    }

    // Update player arrow position
    UpdatePlayerArrow();
    
    // Update marker screen positions using DirectX
    int visibleMarkers = 0;
    for (auto& marker : markers) {
        D3DXVECTOR2 screenPos;
        marker.isVisible = WorldToScreen(marker.worldPos, screenPos);
        if (marker.isVisible) {
            marker.screenPos = screenPos;
            visibleMarkers++;
        } else {
            // Debug log why WorldToScreen failed occasionally
            static int debugCounter = 0;
            if (++debugCounter % 600 == 0) { // Log every 10 seconds
                LOG_DEBUG("WorldToScreen failed for marker " + marker.label + 
                         " at pos (" + std::to_string(marker.worldPos.x) + ", " + 
                         std::to_string(marker.worldPos.y) + ", " + 
                         std::to_string(marker.worldPos.z) + ")");
            }
        }
    }
    
    // Update line visibility
    for (auto& line : lines) {
        D3DXVECTOR2 startScreen, endScreen;
        bool startVisible = WorldToScreen(line.start, startScreen);
        bool endVisible = WorldToScreen(line.end, endScreen);
        // Make lines more permissive - show if at least one endpoint is visible
        line.isVisible = startVisible || endVisible;
        
        // Store screen positions even if only partially visible
        if (startVisible) {
            // Store valid start screen position for later use
        }
        if (endVisible) {
            // Store valid end screen position for later use
        }
        
        // Debug log line visibility issues occasionally
        static int lineDebugCounter = 0;
        if (++lineDebugCounter % 300 == 0) { // Log every 5 seconds
            LOG_DEBUG("Line " + line.label + " visibility: start=" + std::to_string(startVisible) + 
                     " end=" + std::to_string(endVisible) + " overall=" + std::to_string(line.isVisible) +
                     " startPos(" + std::to_string(line.start.x) + "," + std::to_string(line.start.y) + "," + std::to_string(line.start.z) + ")" +
                     " endPos(" + std::to_string(line.end.x) + "," + std::to_string(line.end.y) + "," + std::to_string(line.end.z) + ")");
        }
    }
    
    // Debug output every 600 updates (about once per 10 seconds at 60 FPS)
    if (updateCount % 600 == 0) {
        LOG_DEBUG("Update #" + std::to_string(updateCount) + ": " + 
                 std::to_string(markers.size()) + " markers total, " + 
                 std::to_string(visibleMarkers) + " visible");
    }
}

void WorldToScreenManager::Render() {
    if (!m_pDevice || !m_pLine) {
        return;
    }
    
    // Save render states
    DWORD oldAlphaBlend, oldSrcBlend, oldDestBlend;
    m_pDevice->GetRenderState(D3DRS_ALPHABLENDENABLE, &oldAlphaBlend);
    m_pDevice->GetRenderState(D3DRS_SRCBLEND, &oldSrcBlend);
    m_pDevice->GetRenderState(D3DRS_DESTBLEND, &oldDestBlend);
    
    // Enable alpha blending
    m_pDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    m_pDevice->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    m_pDevice->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    
    int renderedLines = 0;
    int renderedMarkers = 0;
    
    // Render lines
    for (const auto& line : lines) {
        if (!line.isVisible) continue;
        
        D3DXVECTOR2 startScreen, endScreen;
        bool startVisible = WorldToScreen(line.start, startScreen);
        bool endVisible = WorldToScreen(line.end, endScreen);
        
        // If both endpoints are visible, draw normally
        if (startVisible && endVisible) {
            DrawLine(startScreen, endScreen, line.color, line.thickness);
            renderedLines++;
        }
        // If only one endpoint is visible, we can still draw a partial line
        // by clamping the off-screen endpoint to screen edges
        else if (startVisible || endVisible) {
            // Get viewport for clamping
            D3DVIEWPORT9 viewport;
            if (SUCCEEDED(m_pDevice->GetViewport(&viewport))) {
                // Clamp off-screen points to screen edges
                if (!startVisible) {
                    startScreen.x = std::max(0.0f, std::min((float)viewport.Width, startScreen.x));
                    startScreen.y = std::max(0.0f, std::min((float)viewport.Height, startScreen.y));
                }
                if (!endVisible) {
                    endScreen.x = std::max(0.0f, std::min((float)viewport.Width, endScreen.x));
                    endScreen.y = std::max(0.0f, std::min((float)viewport.Height, endScreen.y));
                }
                
                DrawLine(startScreen, endScreen, line.color, line.thickness);
                renderedLines++;
            }
        }
    }
    
    // Render markers
    for (const auto& marker : markers) {
        if (marker.isVisible) {
            // Draw triangle arrow for player marker, cross for others
            if (marker.label == "YOU") {
                DrawTriangleArrow(marker.screenPos, marker.color, marker.size);
            } else {
                DrawMarker(marker.screenPos, marker.color, marker.size);
            }
            renderedMarkers++;
            
            // Draw label if available
            if (!marker.label.empty()) {
                D3DXVECTOR2 textPos = marker.screenPos;
                textPos.y -= marker.size + 5; // Position text above marker
                DrawText(marker.label, textPos, 0xFFFFFFFF);
            }
        }
    }
    
    // Restore render states
    m_pDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, oldAlphaBlend);
    m_pDevice->SetRenderState(D3DRS_SRCBLEND, oldSrcBlend);
    m_pDevice->SetRenderState(D3DRS_DESTBLEND, oldDestBlend);
}

bool WorldToScreenManager::IsValidPlayerPosition(const C3Vector& pos) {
    // Basic validation - check if position is not zero and within reasonable bounds
    return (pos.x != 0.0f || pos.y != 0.0f || pos.z != 0.0f) &&
           abs(pos.x) < 100000.0f && abs(pos.y) < 100000.0f && abs(pos.z) < 100000.0f;
}

bool WorldToScreenManager::GetPlayerPositionSafe(C3Vector& playerPos) {
    // Simple validation without exception handling to avoid object unwinding issues
    playerPos = GetLocalPlayerPosition();
    return IsValidPlayerPosition(playerPos);
}

// Global functions for easy access
extern "C" {
    __declspec(dllexport) bool InitializeWorldToScreen(LPDIRECT3DDEVICE9 pDevice) {
        return g_WorldToScreenManager.Initialize(pDevice);
    }
    
    __declspec(dllexport) void CleanupWorldToScreen() {
        g_WorldToScreenManager.Cleanup();
    }
    
    __declspec(dllexport) void OnWorldToScreenDeviceLost() {
        g_WorldToScreenManager.OnDeviceLost();
    }
    
    __declspec(dllexport) void OnWorldToScreenDeviceReset() {
        g_WorldToScreenManager.OnDeviceReset();
    }
    
    __declspec(dllexport) int AddWorldToScreenLine(float startX, float startY, float startZ, 
                                                   float endX, float endY, float endZ, 
                                                   unsigned long color, float thickness) {
        D3DXVECTOR3 start(startX, startY, startZ);
        D3DXVECTOR3 end(endX, endY, endZ);
        return g_WorldToScreenManager.AddLine(start, end, color, thickness);
    }
    
    __declspec(dllexport) int AddPlayerToEnemyLine(float enemyX, float enemyY, float enemyZ, unsigned long color) {
        D3DXVECTOR3 enemyPos(enemyX, enemyY, enemyZ);
        return g_WorldToScreenManager.AddPlayerToEnemyLine(enemyPos, color);
    }
    
    __declspec(dllexport) void RemoveWorldToScreenLine(int id) {
        g_WorldToScreenManager.RemoveLine(id);
    }
    
    __declspec(dllexport) void ClearAllWorldToScreenLines() {
        g_WorldToScreenManager.ClearAllLines();
    }
    
    __declspec(dllexport) int AddWorldToScreenMarker(float x, float y, float z, unsigned long color, float size) {
        D3DXVECTOR3 pos(x, y, z);
        return g_WorldToScreenManager.AddMarker(pos, color, size);
    }
    
    __declspec(dllexport) void RemoveWorldToScreenMarker(int id) {
        g_WorldToScreenManager.RemoveMarker(id);
    }
    
    __declspec(dllexport) void UpdateWorldToScreen() {
        g_WorldToScreenManager.Update();
    }
    
    __declspec(dllexport) void RenderWorldToScreen() {
        g_WorldToScreenManager.Render();
    }
} 