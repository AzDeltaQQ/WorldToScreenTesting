#pragma once

#include "../../types.h"
#include "WorldToScreenCore.h"
#include "RenderEngine.h"
#include "../../objects/ObjectManager.h"
#include <vector>
#include <string>
#include <d3dx9.h>

class ObjectOverlay {
private:
    WorldToScreenCore* m_pWorldToScreen;
    RenderEngine* m_pRenderEngine;
    ObjectManager* m_pObjectManager;
    
public:
    ObjectOverlay();
    ~ObjectOverlay();
    
    // Initialization
    void Initialize(WorldToScreenCore* pWorldToScreen, RenderEngine* pRenderEngine, ObjectManager* pObjectManager);
    void Cleanup();
    
    // Rendering
    void RenderObjectNames();
    void RenderDistances();
    
    // Update and render
    void Update();
    void Render();
    
    // Settings (synchronized from GUI)
    bool showObjectNames = false;
    bool showDistances = false;
    bool showPlayerNames = true;
    bool showUnitNames = false;
    bool showGameObjectNames = false;
    
    // Separate distance filters (independent of name filters)
    bool showPlayerDistances = true;
    bool showUnitDistances = false;
    bool showGameObjectDistances = false;
    
    float maxDrawDistance = 50.0f;
    D3DCOLOR textColor = 0xFFFFFFFF;
    D3DCOLOR distanceColor = 0xFFCCCCCC;
    float textScale = 1.0f;
}; 