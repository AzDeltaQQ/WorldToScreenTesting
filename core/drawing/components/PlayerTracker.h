#pragma once

#include "../../types/types.h"
#include "WorldToScreenCore.h"
#include "LineManager.h"
#include "MarkerManager.h"
#include <d3dx9.h>

class PlayerTracker {
private:
    WorldToScreenCore* m_pWorldToScreen;
    LineManager* m_pLineManager;
    MarkerManager* m_pMarkerManager;
    
    // Player-to-target line tracking
    int m_targetLineId;
    
public:
    PlayerTracker();
    ~PlayerTracker();
    
    // Initialization
    void Initialize(WorldToScreenCore* pWorldToScreen, LineManager* pLineManager, MarkerManager* pMarkerManager);
    void Cleanup();
    
    // Player arrow management
    int AddPlayerArrow(D3DCOLOR color = 0xFFFF0000, float size = 20.0f);
    void UpdatePlayerArrow();
    
    // Player-to-target line management
    void UpdatePlayerToTargetLine();
    
    // Player-to-enemy line helpers
    int AddPlayerToEnemyLine(const D3DXVECTOR3& enemyPos, D3DCOLOR color = 0xFF00FF00);
    
    // Update all player-related elements
    void Update();
    
    // Settings
    bool showPlayerArrow = true;
    bool showPlayerToTargetLine = true;
    D3DCOLOR playerArrowColor = 0xFFFF0000; // Default red
    float playerArrowSize = 20.0f; // Default size
    D3DCOLOR lineColor = 0xFF00FF00; // Default green for lines
}; 