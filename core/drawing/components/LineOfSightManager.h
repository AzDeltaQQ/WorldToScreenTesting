#pragma once

#include "../types/types.h"
#include "../../types/types.h"
#include <unordered_map>
#include <vector>

// Forward declarations
class WowObject;

// Line of Sight result structure
struct LoSResult {
    bool isBlocked;
    float hitFraction;      // 0.0 to 1.0, where 1.0 means clear LoS
    Vector3 hitPoint;       // World coordinates where ray hit (if blocked)
    WGUID hitObjectGUID;    // GUID of what was hit (if available)
    bool isValid;           // Whether this result is current/valid
    
    LoSResult() : isBlocked(false), hitFraction(1.0f), hitPoint(), hitObjectGUID(), isValid(false) {}
};

// LoS check settings
struct LoSSettings {
    bool enableLoSChecks;
    bool showLoSLines;          // Draw lines from player to targets with LoS status
    bool useLoSForTargeting;    // Only show/highlight objects with clear LoS
    bool showBlockedTargets;    // Whether to show blocked targets (with different styling)
    
    // Visual settings
    float clearLoSColor[4];     // Color for objects with clear LoS (green)
    float blockedLoSColor[4];   // Color for objects with blocked LoS (red)
    float losLineWidth;         // Width of LoS indicator lines
    
    // Performance settings
    float losUpdateInterval;    // How often to update LoS checks (seconds)
    float maxLoSRange;          // Maximum range for LoS checks (yards)
    
    LoSSettings() {
        enableLoSChecks = true;   // Enable by default so LoS lines show without GUI
        showLoSLines = true;
        useLoSForTargeting = false;
        showBlockedTargets = true;
        
        // Green for clear LoS
        clearLoSColor[0] = 0.0f; clearLoSColor[1] = 1.0f; 
        clearLoSColor[2] = 0.0f; clearLoSColor[3] = 1.0f;
        
        // Red for blocked LoS
        blockedLoSColor[0] = 1.0f; blockedLoSColor[1] = 0.0f; 
        blockedLoSColor[2] = 0.0f; blockedLoSColor[3] = 1.0f;
        
        losLineWidth = 2.0f;
        losUpdateInterval = 2.0f;   // Every 2 seconds to reduce computational load
        maxLoSRange = 100.0f;       // 100 yards
    }
};

// Line of Sight Manager class
class LineOfSightManager {
private:
    // WoW 3.3.5a memory addresses and function pointers (from user's analysis)
    static constexpr uintptr_t TRACE_LINE_FUNCTION_ADDR = 0x7A3B70;
    static constexpr uintptr_t HIT_RESULT_GUID_ADDR = 0xCD7768;
    
    // LoS collision flags (from user's analysis)  
    static constexpr uint32_t LOS_FLAG_ALL_ENCOMPASSING = 0x100011;  // M2Collision + WMOCollision + EntityCollision (trees + buildings + entities, NO terrain)
    static constexpr uint32_t LOS_FLAG_SPECTATOR_CAMERA = 0x100171;  // Alternative comprehensive flag
    
    // Function pointer type for TraceLine
    typedef bool(__cdecl* TraceLineFn)(
        const Vector3* startPos,
        const Vector3* endPos,
        Vector3* outHitPoint,
        float* outHitFraction,
        uint32_t flags,
        void* pCallbackData
    );
    
    TraceLineFn m_traceLineFunc;
    
    // LoS cache and management
    std::unordered_map<WGUID, LoSResult, WGUIDHash> m_losCache;
    float m_lastUpdateTime;
    LoSSettings m_settings;
    bool m_isInitialized;
    
    // Internal helper methods
    bool InitializeGameFunctions();
    LoSResult PerformLoSCheck(const Vector3& startPos, const Vector3& endPos);
    WGUID ReadHitObjectGUID();
    bool ShouldUpdateLoSForObject(const WGUID& guid, float currentTime);
    
public:
    LineOfSightManager();
    ~LineOfSightManager() = default;
    
    // Initialization and cleanup
    bool Initialize();
    void Shutdown();
    bool IsInitialized() const { return m_isInitialized; }
    
    // Main update function
    void Update(float deltaTime, const Vector3& playerPos);
    
    // LoS check functions
    LoSResult CheckLineOfSight(const Vector3& startPos, const Vector3& endPos, bool useCache = true);
    LoSResult GetLoSResult(const WGUID& objectGUID) const;
    bool HasClearLineOfSight(const WGUID& objectGUID) const;
    bool HasClearLineOfSight(const Vector3& startPos, const Vector3& endPos);
    
    // Batch operations for performance
    void UpdateLoSForObjects(const std::vector<WowObject*>& objects, const Vector3& playerPos);
    void ClearLoSCache();
    void InvalidateLoSCache();
    
    // Settings management
    const LoSSettings& GetSettings() const { return m_settings; }
    void SetSettings(const LoSSettings& settings) { m_settings = settings; }
    
    // Visual indicator helpers
    bool ShouldDrawObject(const WGUID& objectGUID) const;
    uint32_t GetObjectColor(const WGUID& objectGUID, uint32_t defaultColor) const;
    bool ShouldDrawLoSLine(const WGUID& objectGUID) const;
    
    // Statistics and debugging
    size_t GetCacheSize() const { return m_losCache.size(); }
    float GetLastUpdateTime() const { return m_lastUpdateTime; }
    
    // Get LoS lines for rendering
    struct LoSLine {
        Vector3 startPos;
        Vector3 endPos;
        uint32_t color;
        float width;
        bool isBlocked;
        WGUID targetGUID;
    };
    
    std::vector<LoSLine> GetLoSLinesForRendering(const Vector3& playerPos) const;
}; 