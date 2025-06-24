#include "ObjectOverlay.h"
#include "../../logs/Logger.h"
#include "../../objects/WowObject.h"
#include "../../objects/WowUnit.h"
#include "../../objects/WowPlayer.h"
#include "../../objects/WowGameObject.h"
#include <sstream>
#include <iomanip>

ObjectOverlay::ObjectOverlay() 
    : m_pWorldToScreen(nullptr), m_pRenderEngine(nullptr), m_pObjectManager(nullptr) {
}

ObjectOverlay::~ObjectOverlay() {
    Cleanup();
}

void ObjectOverlay::Initialize(WorldToScreenCore* pWorldToScreen, RenderEngine* pRenderEngine, ObjectManager* pObjectManager) {
    m_pWorldToScreen = pWorldToScreen;
    m_pRenderEngine = pRenderEngine;
    m_pObjectManager = pObjectManager;
}

void ObjectOverlay::Cleanup() {
    m_pWorldToScreen = nullptr;
    m_pRenderEngine = nullptr;
    m_pObjectManager = nullptr;
}

void ObjectOverlay::RenderObjectNames() {
    if (!showObjectNames || !m_pWorldToScreen || !m_pRenderEngine || !m_pObjectManager) {
        return;
    }
    
    if (!m_pObjectManager->IsInitialized()) {
        return;
    }
    
    // Get fresh player position directly from memory (not cached)
    C3Vector freshPlayerPos;
    if (!m_pWorldToScreen->GetPlayerPositionSafe(freshPlayerPos)) {
        return; // No valid player position
    }
    Vector3 playerPos(freshPlayerPos.x, freshPlayerPos.y, freshPlayerPos.z);
    
    // Get all objects and filter by type and distance
    auto allObjects = m_pObjectManager->GetAllObjects();
    
    for (const auto& pair : allObjects) {
        auto obj = pair.second;
        if (!obj || !obj->IsValid()) continue;
        
        // Check type filters
        WowObjectType objType = obj->GetObjectType();
        bool shouldShow = false;
        
        if (objType == OBJECT_PLAYER && showPlayerNames) shouldShow = true;
        else if (objType == OBJECT_UNIT && showUnitNames) shouldShow = true;
        else if (objType == OBJECT_GAMEOBJECT && showGameObjectNames) shouldShow = true;
        
        if (!shouldShow) continue;
        
        // Check distance using fresh player position
        Vector3 objPos = obj->GetPosition();
        float distance = playerPos.Distance(objPos);
        if (distance > maxDrawDistance) continue;
        
        // Convert to screen coordinates
        D3DXVECTOR3 worldPos(objPos.x, objPos.y, objPos.z);
        D3DXVECTOR2 screenPos;
        
        if (m_pWorldToScreen->WorldToScreen(worldPos, screenPos)) {
            // Get object name
            std::string objectName = obj->GetName();
            if (objectName.empty()) {
                objectName = "Unknown";
            }
            
            // Render object name
            D3DXVECTOR2 textPos = screenPos;
            textPos.y -= 10; // Position text above object
            m_pRenderEngine->DrawText(objectName, textPos, textColor, textScale);
        }
    }
}

void ObjectOverlay::RenderDistances() {
    if (!showDistances || !m_pWorldToScreen || !m_pRenderEngine || !m_pObjectManager) {
        return;
    }
    
    if (!m_pObjectManager->IsInitialized()) {
        return;
    }
    
    // Get fresh player position directly from memory (not cached)
    C3Vector freshPlayerPos;
    if (!m_pWorldToScreen->GetPlayerPositionSafe(freshPlayerPos)) {
        return; // No valid player position
    }
    Vector3 playerPos(freshPlayerPos.x, freshPlayerPos.y, freshPlayerPos.z);
    
    // Get all objects and render distances
    auto allObjects = m_pObjectManager->GetAllObjects();
    
    for (const auto& pair : allObjects) {
        auto obj = pair.second;
        if (!obj || !obj->IsValid()) continue;
        
        // Check type filters for distances (separate from name filters)
        WowObjectType objType = obj->GetObjectType();
        bool shouldShow = false;
        
        if (objType == OBJECT_PLAYER && showPlayerDistances) shouldShow = true;
        else if (objType == OBJECT_UNIT && showUnitDistances) shouldShow = true;
        else if (objType == OBJECT_GAMEOBJECT && showGameObjectDistances) shouldShow = true;
        
        if (!shouldShow) continue;
        
        // Check distance using fresh player position
        Vector3 objPos = obj->GetPosition();
        float distance = playerPos.Distance(objPos);
        if (distance > maxDrawDistance) continue;
        
        // Convert to screen coordinates
        D3DXVECTOR3 worldPos(objPos.x, objPos.y, objPos.z);
        D3DXVECTOR2 screenPos;
        
        if (m_pWorldToScreen->WorldToScreen(worldPos, screenPos)) {
            // Format distance text
            std::stringstream ss;
            ss << std::fixed << std::setprecision(1) << distance << " yd";
            
            // Render distance below object name (or at object if no name)
            D3DXVECTOR2 distancePos = screenPos;
            if (showObjectNames) {
                distancePos.y += 10; // Position below object name
            } else {
                distancePos.y -= 5; // Position above object
            }
            
            m_pRenderEngine->DrawText(ss.str(), distancePos, distanceColor, textScale);
        }
    }
}

void ObjectOverlay::Update() {
    // No specific update logic needed for now
    // Object positions are read fresh each render
}

void ObjectOverlay::Render() {
    if (!m_pWorldToScreen || !m_pRenderEngine || !m_pObjectManager) {
        return;
    }
    
    // Render object names first
    RenderObjectNames();
    
    // Render distances second (so they appear below names)
    RenderDistances();
} 