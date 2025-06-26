#pragma once

#include "CombatLogEntry.h"
#include "../logs/Logger.h"
#include <mutex>
#include <deque>
#include <memory>
#include <functional>
#include <atomic>
#include <cstdint>
#include <string>
#include <vector>
#include <unordered_set>
#include <chrono>
#include <unordered_map>
#include <queue>
#include <future>

// Forward declarations
struct CombatLogEntry;
struct WowCombatLogEntry;
class CombatSession;

// --- Spell Lookup System ---
using FnFetchLocalizedRow = int(__thiscall*)(
    void* pDbcContext,      // Pointer to the context object for Spell.dbc (passed in ECX)
    int recordId,           // The Spell ID
    void* pResultBuffer     // A pointer to a buffer to receive the record data
);

struct SpellInfo {
    std::string name;
    uint32_t schoolMask;
    bool success;
};

// --- WoW Memory Addresses for Spell Lookup ---
namespace WowSpellAddresses {
    constexpr uintptr_t FETCH_LOCALIZED_ROW = 0x004CFD20;
    constexpr uintptr_t SPELL_DB_CONTEXT_POINTER = 0x00AD49D0;
}

class CombatLogManager {
public:
    // Singleton pattern
    static CombatLogManager& GetInstance();
    static void Initialize();
    static void Shutdown();

    // Combat log capture control
    bool StartCapture();
    bool StopCapture();
    bool IsCapturing() const { return m_isCapturing.load(); }
    
    // Session management
    void StartNewSession();
    void EndCurrentSession();
    bool HasActiveSession() const;
    std::shared_ptr<CombatSession> GetCurrentSession();
    std::shared_ptr<CombatSession> GetSession(size_t index);
    size_t GetSessionCount() const;
    void ClearAllSessions();
    
    // Entry management
    void AddEntry(std::shared_ptr<CombatLogEntry> entry);
    void AddDamageEntry(const DamageLogEntry& entry);
    void AddHealEntry(const HealLogEntry& entry);
    void AddExperienceEntry(const ExperienceLogEntry& entry);
    void AddHonorEntry(const HonorLogEntry& entry);
    
    // Data access with filtering
    std::vector<std::shared_ptr<CombatLogEntry>> GetFilteredEntries(const CombatLogFilter& filter) const;
    std::vector<std::shared_ptr<CombatLogEntry>> GetRecentEntries(size_t count = 100) const;
    std::vector<std::shared_ptr<CombatLogEntry>> GetEntriesInTimeRange(
        std::chrono::steady_clock::time_point start,
        std::chrono::steady_clock::time_point end) const;
    
    // Statistics and analysis
    DamageStatistics CalculateDamageStats(const WGUID& entityGUID, const CombatLogFilter& filter = CombatLogFilter()) const;
    HealingStatistics CalculateHealingStats(const WGUID& entityGUID, const CombatLogFilter& filter = CombatLogFilter()) const;
    
    // Get all participants in current session
    std::vector<std::pair<WGUID, std::string>> GetSessionParticipants() const;
    
    // Export functionality
    bool ExportToCSV(const std::string& filename, const CombatLogFilter& filter = CombatLogFilter()) const;
    bool ExportToJSON(const std::string& filename, const CombatLogFilter& filter = CombatLogFilter()) const;
    
    // Settings
    struct Settings {
        size_t maxEntriesPerSession = 10000;
        size_t maxSessions = 50;
        bool autoStartOnCombat = true;
        bool autoEndOnCombatEnd = true;
        bool captureAllEvents = false;  // If false, only capture damage/heal/death events
        double combatTimeoutSeconds = 5.0;  // Time to wait before ending combat session
        bool enableRealTimeAnalysis = true;
        bool logToFile = false;
        std::string logFilePath = "combat_log.txt";
    };
    
    Settings& GetSettings() { return m_settings; }
    const Settings& GetSettings() const { return m_settings; }
    void ApplySettings(const Settings& settings);
    
    // Event callbacks for real-time processing
    using CombatEventCallback = std::function<void(std::shared_ptr<CombatLogEntry>)>;
    void RegisterEventCallback(const std::string& name, CombatEventCallback callback);
    void UnregisterEventCallback(const std::string& name);
    
    // Memory management
    void TrimOldEntries();
    size_t GetTotalEntryCount() const;
    size_t GetMemoryUsageBytes() const;
    
    // Combat state detection
    bool IsInCombat() const { return m_isInCombat.load(); }
    std::chrono::steady_clock::time_point GetLastCombatTime() const { return m_lastCombatTime; }
    
    // Game integration hooks (to be called from game's combat log system)
    void OnCombatLogEvent(uint32_t eventType, const void* eventData, size_t dataSize);
    void OnSpellDamage(WGUID sourceGUID, WGUID targetGUID, uint32_t spellId, 
                      uint32_t damage, uint32_t overkill, uint32_t school, 
                      uint32_t resisted, uint32_t blocked, uint32_t absorbed, 
                      uint32_t hitFlags);
    void OnSpellHeal(WGUID sourceGUID, WGUID targetGUID, uint32_t spellId,
                    uint32_t healing, uint32_t overheal, uint32_t hitFlags);
    void OnMeleeDamage(WGUID sourceGUID, WGUID targetGUID, uint32_t damage,
                      uint32_t overkill, uint32_t blocked, uint32_t absorbed,
                      uint32_t hitFlags);
    void OnExperienceGain(uint32_t amount, bool isQuest, bool isGroupBonus);
    void OnHonorGain(WGUID victimGUID, uint32_t amount, uint32_t rank);
    
    // WoW Memory Reading Methods
    void StartWowMemoryReading();
    void StopWowMemoryReading();
    bool IsWowMemoryReadingActive() const { return m_wowReadingActive; }
    void TriggerManualRead(); // For testing purposes
    void UpdateCombatLogReading(); // Called from GUI Update loop for automatic reading
    
    // Spell lookup system (thread-safe with EndScene execution)
    SpellInfo GetSpellInfoById(uint32_t spellId);
    std::string GetSpellNameById(uint32_t spellId);
    void ProcessSpellLookupQueue(); // Called from EndScene - must be public

public:
    // Custom deleter for unique_ptr to access private destructor
    struct Deleter {
        void operator()(CombatLogManager* ptr) {
            delete ptr;
        }
    };

private:
    CombatLogManager();
    ~CombatLogManager();
    
    // Prevent copying
    CombatLogManager(const CombatLogManager&) = delete;
    CombatLogManager& operator=(const CombatLogManager&) = delete;
    
    // Internal methods
    void UpdateCombatState();
    void ProcessPendingEntries();
    void TriggerEventCallbacks(std::shared_ptr<CombatLogEntry> entry);
    std::string ResolveEntityName(const WGUID& guid) const;
    void UpdateSessionStatistics(std::shared_ptr<CombatLogEntry> entry);
    
    // WoW Memory Reading Methods
    void ReadWowCombatLogEntries();
    std::shared_ptr<CombatLogEntry> ConvertWowEntry(const WowCombatLogEntry& wowEntry);
    std::string GetEventTypeName(int32_t eventTypeIndex);
    
private:
    // Spell lookup queue for EndScene execution
    struct SpellLookupRequest {
        uint32_t spellId;
        std::shared_ptr<std::promise<SpellInfo>> promise;
    };
    
    std::queue<SpellLookupRequest> m_spellLookupQueue;
    std::mutex m_spellLookupMutex;
    std::unordered_map<uint32_t, SpellInfo> m_spellCache; // Cache successful lookups
    
    // Internal spell lookup (called from EndScene only)
    SpellInfo GetSpellInfoByIdInternal(uint32_t spellId);
    
    // Thread safety
    mutable std::recursive_mutex m_mutex;
    
    // Core data
    std::deque<std::shared_ptr<CombatSession>> m_sessions;
    std::shared_ptr<CombatSession> m_currentSession;
    
    // State management
    std::atomic<bool> m_isCapturing{false};
    std::atomic<bool> m_isInCombat{false};
    std::chrono::steady_clock::time_point m_lastCombatTime;
    
    // Settings and callbacks
    Settings m_settings;
    std::unordered_map<std::string, CombatEventCallback> m_eventCallbacks;
    
    // Performance tracking
    mutable std::atomic<size_t> m_totalEntryCount{0};
    
    // WoW Memory Reading State
    std::atomic<bool> m_wowReadingActive{false};
    uintptr_t m_lastProcessedNodeAddr{0};  // Address of last processed node (like Python)
    std::unordered_set<uintptr_t> m_processedNodeAddresses; // Track processed nodes to prevent duplicates
    
    // Instance management
    static std::unique_ptr<CombatLogManager, Deleter> s_instance;
    static std::mutex s_instanceMutex;
    static std::atomic<bool> s_isShuttingDown;
}; 