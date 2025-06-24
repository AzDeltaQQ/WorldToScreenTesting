#include "LineOfSightManager.h"
#include "../../objects/WowObject.h"
#include "../../memory/memory.h"
#include "../../logs/Logger.h"
#include <algorithm>
#include <chrono>

LineOfSightManager::LineOfSightManager()
    : m_traceLineFunc(nullptr)
    , m_lastUpdateTime(0.0f)
    , m_isInitialized(false)
{
    LOG_INFO("LineOfSightManager constructed");
}

bool LineOfSightManager::Initialize() {
    if (m_isInitialized) {
        return true;
    }

    LOG_INFO("Initializing LineOfSightManager...");
    
    // Initialize game function pointers
    if (!InitializeGameFunctions()) {
        LOG_ERROR("Failed to initialize LoS game functions");
        return false;
    }
    
    // Clear any existing cache
    m_losCache.clear();
    m_lastUpdateTime = 0.0f;
    
    m_isInitialized = true;
    LOG_INFO("LineOfSightManager initialized successfully");
    return true;
}

void LineOfSightManager::Shutdown() {
    if (!m_isInitialized) {
        return;
    }
    
    LOG_INFO("Shutting down LineOfSightManager...");
    
    ClearLoSCache();
    m_traceLineFunc = nullptr;
    m_isInitialized = false;
    
    LOG_INFO("LineOfSightManager shutdown complete");
}

bool LineOfSightManager::InitializeGameFunctions() {
    try {
        // Validate that the TraceLine function address is readable
        if (!Memory::IsValidAddress(TRACE_LINE_FUNCTION_ADDR)) {
            LOG_ERROR("TraceLine function address 0x7A3B70 is not valid");
            return false;
        }
        
        // Set up function pointer
        m_traceLineFunc = reinterpret_cast<TraceLineFn>(TRACE_LINE_FUNCTION_ADDR);
        
        // Verify hit result GUID address is valid
        if (!Memory::IsValidAddress(HIT_RESULT_GUID_ADDR)) {
            LOG_WARNING("Hit result GUID address 0xCD7768 may not be valid");
        }
        
        LOG_INFO("LoS game functions initialized successfully");
        return true;
        
    } catch (const std::exception& e) {
        LOG_ERROR("Exception during LoS function initialization: " + std::string(e.what()));
        return false;
    }
}

void LineOfSightManager::Update(float deltaTime, const Vector3& playerPos) {
    if (!m_isInitialized || !m_settings.enableLoSChecks) {
        return;
    }
    
    m_lastUpdateTime += deltaTime;
    
    // Only update based on our configured interval
    if (m_lastUpdateTime < m_settings.losUpdateInterval) {
        return;
    }
    
    // Reset timer
    m_lastUpdateTime = 0.0f;
    
    // Clean up old/invalid cache entries
    auto it = m_losCache.begin();
    while (it != m_losCache.end()) {
        if (!it->second.isValid) {
            it = m_losCache.erase(it);
        } else {
            ++it;
        }
    }
    
    // Disable cache update logging to prevent spam
    // (Cache size can be viewed in GUI instead)
}

LoSResult LineOfSightManager::PerformLoSCheck(const Vector3& startPos, const Vector3& endPos) {
    LoSResult result;
    
    if (!m_isInitialized || !m_traceLineFunc) {
        LOG_WARNING("LoS check attempted but system not initialized");
        return result;
    }
    
    try {
        Vector3 hitPoint;
        float hitFraction = 1.0f;
        
        // Call the WoW TraceLine function with comprehensive collision flags
        bool traceResult = m_traceLineFunc(
            &startPos,
            &endPos,
            &hitPoint,
            &hitFraction,
            LOS_FLAG_ALL_ENCOMPASSING,  // Use the all-encompassing flag from user's analysis
            nullptr  // No callback data needed
        );
        
        // According to user's analysis:
        // - false (AL register is 0) = Line of sight is clear. The ray reached its destination without hitting anything.
        // - true (AL register is 1) = Line of sight is blocked. The ray was obstructed.
        // - hitFraction close to 1.0f also indicates clear LoS (ray reached target)
        
        result.isBlocked = traceResult;  // Direct mapping as per user's analysis
        result.hitFraction = hitFraction;
        result.hitPoint = hitPoint;
        result.isValid = true;
        
        // Additional check: if hitFraction is very close to 1.0, consider it clear even if traceResult says blocked
        if (traceResult && hitFraction >= 0.99f) {
            result.isBlocked = false;  // Override: close enough to target means clear LoS
            // Don't log this override to avoid spam
        }
        
        // If something was hit, try to read the GUID of what was hit
        if (result.isBlocked && Memory::IsValidAddress(HIT_RESULT_GUID_ADDR)) {
            try {
                uint64_t hitGuid64 = Memory::Read<uint64_t>(HIT_RESULT_GUID_ADDR);
                result.hitObjectGUID = WGUID(hitGuid64);
                
                // Disable GUID logging to prevent spam
                // LOG_DEBUG("LoS blocked by object GUID: 0x" + std::to_string(hitGuid64) + 
                //     ", hitFraction: " + std::to_string(hitFraction));
            } catch (const std::exception& e) {
                LOG_WARNING("Failed to read hit object GUID: " + std::string(e.what()));
            }
        }
        
        // Disable regular LoS logging to prevent spam - only log via manual test button
        // (Logging moved to manual test button in DrawingTab.cpp)
        
    } catch (const std::exception& e) {
        LOG_ERROR("Exception during LoS check: " + std::string(e.what()));
        result.isValid = false;
    }
    
    return result;
}

LoSResult LineOfSightManager::CheckLineOfSight(const Vector3& startPos, const Vector3& endPos, bool useCache) {
    if (!m_isInitialized) {
        return LoSResult();
    }
    
    // Calculate distance for range checking
    float distance = startPos.Distance(endPos);
    if (distance > m_settings.maxLoSRange) {
        LoSResult result;
        result.isBlocked = true;  // Consider out-of-range as blocked
        result.hitFraction = 0.0f;
        result.isValid = true;
        return result;
    }
    
    // For now, always perform fresh checks as we don't have object GUIDs in this context
    // In a real implementation, you'd want caching based on object GUIDs
    return PerformLoSCheck(startPos, endPos);
}

LoSResult LineOfSightManager::GetLoSResult(const WGUID& objectGUID) const {
    auto it = m_losCache.find(objectGUID);
    if (it != m_losCache.end() && it->second.isValid) {
        return it->second;
    }
    
    return LoSResult();  // Return invalid result if not cached
}

bool LineOfSightManager::HasClearLineOfSight(const WGUID& objectGUID) const {
    auto result = GetLoSResult(objectGUID);
    return result.isValid && !result.isBlocked;
}

bool LineOfSightManager::HasClearLineOfSight(const Vector3& startPos, const Vector3& endPos) {
    auto result = CheckLineOfSight(startPos, endPos, false);  // Don't use cache for direct checks
    return result.isValid && !result.isBlocked;
}

void LineOfSightManager::UpdateLoSForObjects(const std::vector<WowObject*>& objects, const Vector3& playerPos) {
    if (!m_isInitialized || !m_settings.enableLoSChecks || objects.empty()) {
        return;
    }
    
    // Disable batch update logging to prevent spam
    // (Object count can be viewed in GUI instead)
    
    for (auto* obj : objects) {
        if (!obj) continue;
        
        auto objGuid = obj->GetGUID();
        auto objPos = obj->GetPosition();
        Vector3 targetPos(objPos.x, objPos.y, objPos.z);
        
        // Skip if out of range
        float distance = playerPos.Distance(targetPos);
        if (distance > m_settings.maxLoSRange) {
            continue;
        }
        
        // Perform LoS check
        auto result = PerformLoSCheck(playerPos, targetPos);
        if (result.isValid) {
            m_losCache[objGuid] = result;
        }
    }
    
    // Disable final cache logging to prevent spam
    // (Cache size can be viewed in GUI instead)
}

void LineOfSightManager::ClearLoSCache() {
    m_losCache.clear();
    LOG_DEBUG("LoS cache cleared");
}

void LineOfSightManager::InvalidateLoSCache() {
    for (auto& pair : m_losCache) {
        pair.second.isValid = false;
    }
    LOG_DEBUG("LoS cache invalidated");
}

bool LineOfSightManager::ShouldDrawObject(const WGUID& objectGUID) const {
    if (!m_settings.enableLoSChecks) {
        return true;  // Draw everything if LoS is disabled
    }
    
    auto result = GetLoSResult(objectGUID);
    if (!result.isValid) {
        return true;  // Draw if we don't have LoS info yet
    }
    
    if (m_settings.useLoSForTargeting) {
        return !result.isBlocked;  // Only draw objects with clear LoS
    }
    
    if (!m_settings.showBlockedTargets) {
        return !result.isBlocked;  // Don't draw blocked objects
    }
    
    return true;  // Draw everything
}

uint32_t LineOfSightManager::GetObjectColor(const WGUID& objectGUID, uint32_t defaultColor) const {
    if (!m_settings.enableLoSChecks) {
        return defaultColor;
    }
    
    auto result = GetLoSResult(objectGUID);
    if (!result.isValid) {
        return defaultColor;
    }
    
    if (result.isBlocked) {
        // Convert float RGBA to D3DCOLOR for blocked objects
        BYTE r = (BYTE)(m_settings.blockedLoSColor[0] * 255.0f);
        BYTE g = (BYTE)(m_settings.blockedLoSColor[1] * 255.0f);
        BYTE b = (BYTE)(m_settings.blockedLoSColor[2] * 255.0f);
        BYTE a = (BYTE)(m_settings.blockedLoSColor[3] * 255.0f);
        return (a << 24) | (r << 16) | (g << 8) | b;
    } else {
        // Convert float RGBA to D3DCOLOR for clear LoS objects
        BYTE r = (BYTE)(m_settings.clearLoSColor[0] * 255.0f);
        BYTE g = (BYTE)(m_settings.clearLoSColor[1] * 255.0f);
        BYTE b = (BYTE)(m_settings.clearLoSColor[2] * 255.0f);
        BYTE a = (BYTE)(m_settings.clearLoSColor[3] * 255.0f);
        return (a << 24) | (r << 16) | (g << 8) | b;
    }
}

bool LineOfSightManager::ShouldDrawLoSLine(const WGUID& objectGUID) const {
    if (!m_settings.enableLoSChecks || !m_settings.showLoSLines) {
        return false;
    }
    
    auto result = GetLoSResult(objectGUID);
    return result.isValid;  // Draw LoS line if we have LoS data
}

std::vector<LineOfSightManager::LoSLine> LineOfSightManager::GetLoSLinesForRendering(const Vector3& playerPos) const {
    std::vector<LoSLine> lines;
    
    if (!m_settings.enableLoSChecks || !m_settings.showLoSLines) {
        return lines;
    }
    
    for (const auto& pair : m_losCache) {
        const auto& guid = pair.first;
        const auto& result = pair.second;
        
        if (!result.isValid) {
            continue;
        }
        
        LoSLine line;
        line.startPos = playerPos;
        line.targetGUID = guid;
        line.isBlocked = result.isBlocked;
        line.width = m_settings.losLineWidth;
        
        if (result.isBlocked) {
            // Line goes to hit point if blocked
            line.endPos = result.hitPoint;
            
            // Red color for blocked LoS
            BYTE r = (BYTE)(m_settings.blockedLoSColor[0] * 255.0f);
            BYTE g = (BYTE)(m_settings.blockedLoSColor[1] * 255.0f);
            BYTE b = (BYTE)(m_settings.blockedLoSColor[2] * 255.0f);
            BYTE a = (BYTE)(m_settings.blockedLoSColor[3] * 255.0f);
            line.color = (a << 24) | (r << 16) | (g << 8) | b;
        } else {
            // For clear LoS, we need to get the actual target position
            // Skip this line for now since we don't have target position in cache
            // The LoS lines will be handled differently in the main rendering loop
            continue;
        }
        
        lines.push_back(line);
    }
    
    return lines;
}

WGUID LineOfSightManager::ReadHitObjectGUID() {
    if (!Memory::IsValidAddress(HIT_RESULT_GUID_ADDR)) {
        return WGUID();
    }
    
    try {
        uint64_t guid64 = Memory::Read<uint64_t>(HIT_RESULT_GUID_ADDR);
        return WGUID(guid64);
    } catch (const std::exception& e) {
        LOG_WARNING("Failed to read hit object GUID: " + std::string(e.what()));
        return WGUID();
    }
}

bool LineOfSightManager::ShouldUpdateLoSForObject(const WGUID& guid, float currentTime) {
    // For now, always update if we don't have cached data
    // In a more sophisticated implementation, you could track per-object update times
    auto it = m_losCache.find(guid);
    return it == m_losCache.end() || !it->second.isValid;
} 