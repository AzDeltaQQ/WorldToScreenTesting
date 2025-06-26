#include "CombatLogManager.h"
#include "CombatLogAnalyzer.h"
#include "../objects/ObjectManager.h"
#include "../memory/memory.h"
#include "../logs/Logger.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <iomanip>
#include <cstring>  // For memset
#include <Windows.h> // For GetTickCount()

// Static member definitions
std::unique_ptr<CombatLogManager, CombatLogManager::Deleter> CombatLogManager::s_instance = nullptr;
std::mutex CombatLogManager::s_instanceMutex;
std::atomic<bool> CombatLogManager::s_isShuttingDown(false);

CombatLogManager& CombatLogManager::GetInstance() {
    std::lock_guard<std::mutex> lock(s_instanceMutex);
    if (!s_instance && !s_isShuttingDown.load()) {
        s_instance = std::unique_ptr<CombatLogManager, CombatLogManager::Deleter>(new CombatLogManager(), CombatLogManager::Deleter());
    }
    return *s_instance;
}

void CombatLogManager::Initialize() {
    auto& instance = GetInstance();
    LOG_INFO("CombatLogManager initialized");
}

void CombatLogManager::Shutdown() {
    std::lock_guard<std::mutex> lock(s_instanceMutex);
    s_isShuttingDown.store(true);
    if (s_instance) {
        s_instance->StopCapture();
        s_instance.reset();
    }
    LOG_INFO("CombatLogManager shutdown");
}

CombatLogManager::CombatLogManager() {
    m_lastCombatTime = std::chrono::steady_clock::now();
}

CombatLogManager::~CombatLogManager() {
    StopCapture();
}

bool CombatLogManager::StartCapture() {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    
    if (m_isCapturing.load()) {
        return true; // Already capturing
    }
    
    m_isCapturing.store(true);
    
    // Only create a new session if we don't have any active session
    if (!HasActiveSession()) {
        StartNewSession();
    }
    
    // Automatically start WoW memory reading when capture starts
    StartWowMemoryReading();
    
    LOG_INFO("Combat log capture started");
    return true;
}

bool CombatLogManager::StopCapture() {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    
    if (!m_isCapturing.load()) {
        return true; // Already stopped
    }
    
    m_isCapturing.store(false);
    
    if (m_settings.autoEndOnCombatEnd && HasActiveSession()) {
        EndCurrentSession();
    }
    
    // Stop WoW memory reading when capture stops
    StopWowMemoryReading();
    
    LOG_INFO("Combat log capture stopped");
    return true;
}

void CombatLogManager::StartNewSession() {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    
    // End current session if active
    if (m_currentSession && m_currentSession->isActive) {
        EndCurrentSession();
    }
    
    // Create new session
    m_currentSession = std::make_shared<CombatSession>();
    m_currentSession->startTime = std::chrono::steady_clock::now();
    m_currentSession->isActive = true;
    
    // Manage session count
    if (m_sessions.size() >= m_settings.maxSessions) {
        m_sessions.pop_front(); // Remove oldest session
    }
    
    m_sessions.push_back(m_currentSession);
    
    LOG_INFO("New combat session started");
}

void CombatLogManager::EndCurrentSession() {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    
    if (m_currentSession && m_currentSession->isActive) {
        m_currentSession->isActive = false;
        m_currentSession->endTime = std::chrono::steady_clock::now();
        
        LOG_INFO("Combat session ended. Duration: " + 
                CombatLogAnalyzer::FormatDuration(
                    std::chrono::duration<double>(m_currentSession->endTime - m_currentSession->startTime)));
    }
}

bool CombatLogManager::HasActiveSession() const {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    return m_currentSession && m_currentSession->isActive;
}

std::shared_ptr<CombatSession> CombatLogManager::GetCurrentSession() {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    return m_currentSession;
}

std::shared_ptr<CombatSession> CombatLogManager::GetSession(size_t index) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (index < m_sessions.size()) {
        return m_sessions[index];
    }
    return nullptr;
}

size_t CombatLogManager::GetSessionCount() const {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    return m_sessions.size();
}

void CombatLogManager::ClearAllSessions() {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_sessions.clear();
    m_currentSession.reset();
    m_totalEntryCount.store(0);
    LOG_INFO("All combat sessions cleared");
}

void CombatLogManager::AddEntry(std::shared_ptr<CombatLogEntry> entry) {
    if (!entry || !m_isCapturing.load()) {
        LOG_DEBUG("AddEntry: Entry is null or not capturing");
        return;
    }
    
    LOG_DEBUG("AddEntry: Adding entry - type: " + std::to_string(static_cast<int>(entry->eventType)) + 
             ", src: '" + entry->sourceName + "', dest: '" + entry->targetName + "'");
    
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    
    // Ensure we have an active session
    if (!HasActiveSession()) {
        if (m_settings.autoStartOnCombat) {
            StartNewSession();
        } else {
            return;
        }
    }
    
    // Resolve entity names
    if (entry->sourceName.empty()) {
        entry->sourceName = ResolveEntityName(entry->sourceGUID);
    }
    if (entry->targetName.empty()) {
        entry->targetName = ResolveEntityName(entry->targetGUID);
    }
    
    // Add to current session
    m_currentSession->entries.push_back(entry);
    m_totalEntryCount.fetch_add(1);
    
    // Update session metadata
    UpdateSessionStatistics(entry);
    
    // Update combat state
    UpdateCombatState();
    
    // Trigger callbacks
    TriggerEventCallbacks(entry);
    
    // Manage session size
    if (m_currentSession->entries.size() > m_settings.maxEntriesPerSession) {
        TrimOldEntries();
    }
    
    // Log to file if enabled
    if (m_settings.logToFile) {
        // TODO: Implement file logging
    }
}

void CombatLogManager::AddDamageEntry(const DamageLogEntry& entry) {
    auto entryPtr = std::make_shared<DamageLogEntry>(entry);
    AddEntry(entryPtr);
}

void CombatLogManager::AddHealEntry(const HealLogEntry& entry) {
    auto entryPtr = std::make_shared<HealLogEntry>(entry);
    AddEntry(entryPtr);
}

void CombatLogManager::AddExperienceEntry(const ExperienceLogEntry& entry) {
    auto entryPtr = std::make_shared<ExperienceLogEntry>(entry);
    AddEntry(entryPtr);
}

void CombatLogManager::AddHonorEntry(const HonorLogEntry& entry) {
    auto entryPtr = std::make_shared<HonorLogEntry>(entry);
    AddEntry(entryPtr);
}

std::vector<std::shared_ptr<CombatLogEntry>> CombatLogManager::GetFilteredEntries(const CombatLogFilter& filter) const {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    
    std::vector<std::shared_ptr<CombatLogEntry>> result;
    
    if (!m_currentSession) {
        return result;
    }
    
    for (const auto& entry : m_currentSession->entries) {
        if (!entry) continue;
        
        // Event type filter
        if (!filter.allowedEventTypes.empty() && 
            filter.allowedEventTypes.find(entry->eventType) == filter.allowedEventTypes.end()) {
            continue;
        }
        
        // Time filter
        if (filter.useTimeFilter) {
            if (entry->timestamp < filter.startTime || entry->timestamp > filter.endTime) {
                continue;
            }
        }
        
        // Amount filter
        if (entry->amount < filter.minAmount || entry->amount > filter.maxAmount) {
            continue;
        }
        
        // Source/target filters
        if (!filter.allowedSources.empty() && 
            filter.allowedSources.find(entry->sourceGUID) == filter.allowedSources.end()) {
            continue;
        }
        
        if (!filter.allowedTargets.empty() && 
            filter.allowedTargets.find(entry->targetGUID) == filter.allowedTargets.end()) {
            continue;
        }
        
        // Spell filter
        if (!filter.allowedSpells.empty() && entry->spellId != 0 &&
            filter.allowedSpells.find(entry->spellId) == filter.allowedSpells.end()) {
            continue;
        }
        
        // Hit type filters
        bool hasCrit = static_cast<uint32_t>(entry->hitFlags) & static_cast<uint32_t>(HitFlags::CRITICAL);
        bool hasMiss = static_cast<uint32_t>(entry->hitFlags) & static_cast<uint32_t>(HitFlags::MISS);
        bool hasResist = static_cast<uint32_t>(entry->hitFlags) & static_cast<uint32_t>(HitFlags::RESIST);
        
        if (hasCrit && !filter.showCrits) continue;
        if (hasMiss && !filter.showMisses) continue;
        if (hasResist && !filter.showResists) continue;
        if (!hasCrit && !hasMiss && !hasResist && !filter.showNormalHits) continue;
        
        result.push_back(entry);
    }
    
    return result;
}

std::vector<std::shared_ptr<CombatLogEntry>> CombatLogManager::GetRecentEntries(size_t count) const {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    
    std::vector<std::shared_ptr<CombatLogEntry>> result;
    
    if (!m_currentSession) {
        return result;
    }
    
    size_t startIndex = 0;
    if (m_currentSession->entries.size() > count) {
        startIndex = m_currentSession->entries.size() - count;
    }
    
    for (size_t i = startIndex; i < m_currentSession->entries.size(); ++i) {
        result.push_back(m_currentSession->entries[i]);
    }
    
    return result;
}

DamageStatistics CombatLogManager::CalculateDamageStats(const WGUID& entityGUID, const CombatLogFilter& filter) const {
    auto entries = GetFilteredEntries(filter);
    auto breakdown = CombatLogAnalyzer::AnalyzeDamage(entries, entityGUID);
    
    // Convert DamageBreakdown to DamageStatistics
    DamageStatistics stats;
    stats.totalDamage = breakdown.totalDamage;
    stats.totalHits = breakdown.totalHits;
    stats.totalCrits = breakdown.criticalHits;
    stats.totalMisses = breakdown.totalMisses;
    stats.totalDodges = breakdown.totalDodges;
    stats.totalParries = breakdown.totalParries;
    stats.totalBlocks = breakdown.totalBlocks;
    stats.totalAbsorbed = breakdown.totalAbsorbed;
    stats.totalResisted = breakdown.totalResisted;
    stats.totalOverkill = breakdown.totalOverkill;
    stats.averageDamage = breakdown.averageDamage;
    stats.critRate = breakdown.critRate;
    stats.dps = breakdown.dps;
    // Convert std::map to std::unordered_map
    for (const auto& pair : breakdown.damageBySpell) {
        stats.damageBySpell[pair.first] = pair.second;
    }
    for (const auto& pair : breakdown.damageBySchool) {
        stats.damageBySchool[pair.first] = pair.second;
    }
    
    return stats;
}

HealingStatistics CombatLogManager::CalculateHealingStats(const WGUID& entityGUID, const CombatLogFilter& filter) const {
    auto entries = GetFilteredEntries(filter);
    auto breakdown = CombatLogAnalyzer::AnalyzeHealing(entries, entityGUID);
    
    // Convert HealingBreakdown to HealingStatistics
    HealingStatistics stats;
    stats.totalHealing = breakdown.totalHealing;
    stats.totalHits = breakdown.totalHits;
    stats.totalCrits = breakdown.criticalHits;
    stats.totalOverheal = breakdown.totalOverheal;
    stats.averageHeal = breakdown.averageHeal;
    stats.critRate = breakdown.critRate;
    stats.hps = breakdown.hps;
    stats.overhealPercent = breakdown.overhealPercent;
    // Convert std::map to std::unordered_map
    for (const auto& pair : breakdown.healingBySpell) {
        stats.healingBySpell[pair.first] = pair.second;
    }
    
    return stats;
}

std::vector<std::pair<WGUID, std::string>> CombatLogManager::GetSessionParticipants() const {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    
    std::vector<std::pair<WGUID, std::string>> result;
    
    if (!m_currentSession) {
        return result;
    }
    
    for (const auto& pair : m_currentSession->participantNames) {
        result.emplace_back(pair.first, pair.second);
    }
    
    return result;
}

bool CombatLogManager::ExportToCSV(const std::string& filename, const CombatLogFilter& filter) const {
    auto entries = GetFilteredEntries(filter);
    
    std::ofstream file(filename);
    if (!file.is_open()) {
        LOG_ERROR("Failed to open file for CSV export: " + filename);
        return false;
    }
    
    // Write header
    file << "Timestamp,EventType,SourceGUID,SourceName,TargetGUID,TargetName,SpellID,SpellName,Amount,OverAmount,Absorbed,Resisted,Blocked,HitFlags\n";
    
    // Write entries
    for (const auto& entry : entries) {
        file << CombatLogAnalyzer::ToCsvRow(*entry) << "\n";
    }
    
    file.close();
    LOG_INFO("Exported " + std::to_string(entries.size()) + " entries to CSV: " + filename);
    return true;
}

bool CombatLogManager::ExportToJSON(const std::string& filename, const CombatLogFilter& filter) const {
    auto entries = GetFilteredEntries(filter);
    
    std::ofstream file(filename);
    if (!file.is_open()) {
        LOG_ERROR("Failed to open file for JSON export: " + filename);
        return false;
    }
    
    // Write JSON array
    file << "[\n";
    for (size_t i = 0; i < entries.size(); ++i) {
        file << "  " << CombatLogAnalyzer::ToJsonObject(*entries[i]);
        if (i < entries.size() - 1) {
            file << ",";
        }
        file << "\n";
    }
    file << "]\n";
    
    file.close();
    LOG_INFO("Exported " + std::to_string(entries.size()) + " entries to JSON: " + filename);
    return true;
}

void CombatLogManager::ApplySettings(const Settings& settings) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_settings = settings;
}

void CombatLogManager::RegisterEventCallback(const std::string& name, CombatEventCallback callback) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_eventCallbacks[name] = callback;
}

void CombatLogManager::UnregisterEventCallback(const std::string& name) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_eventCallbacks.erase(name);
}

void CombatLogManager::TrimOldEntries() {
    if (!m_currentSession) return;
    
    size_t entriesToRemove = m_currentSession->entries.size() - m_settings.maxEntriesPerSession + 1000; // Keep some buffer
    
    if (entriesToRemove > 0) {
        m_currentSession->entries.erase(
            m_currentSession->entries.begin(),
            m_currentSession->entries.begin() + entriesToRemove
        );
    }
}

size_t CombatLogManager::GetTotalEntryCount() const {
    return m_totalEntryCount.load();
}

void CombatLogManager::UpdateCombatState() {
    auto now = std::chrono::steady_clock::now();
    
    // Check if we should end combat due to timeout
    if (m_isInCombat.load()) {
        auto timeSinceLastCombat = std::chrono::duration<double>(now - m_lastCombatTime).count();
        if (timeSinceLastCombat > m_settings.combatTimeoutSeconds) {
            m_isInCombat.store(false);
            if (m_settings.autoEndOnCombatEnd && HasActiveSession()) {
                EndCurrentSession();
            }
        }
    }
    
    m_lastCombatTime = now;
    m_isInCombat.store(true);
}

void CombatLogManager::TriggerEventCallbacks(std::shared_ptr<CombatLogEntry> entry) {
    for (const auto& pair : m_eventCallbacks) {
        try {
            pair.second(entry);
        } catch (const std::exception& e) {
            LOG_ERROR("Error in combat log callback '" + pair.first + "': " + e.what());
        }
    }
}

std::string CombatLogManager::ResolveEntityName(const WGUID& guid) const {
    // Try to get name from ObjectManager
    if (auto objManager = ObjectManager::GetInstance()) {
        if (auto obj = objManager->GetObjectByGUID(guid)) {
            std::string name = obj->GetName();
            if (!name.empty()) {
                return name;
            }
        }
    }
    
    // Fallback to GUID string
    return "Unknown-" + std::to_string(guid.ToUint64());
}

void CombatLogManager::UpdateSessionStatistics(std::shared_ptr<CombatLogEntry> entry) {
    if (!m_currentSession || !entry) return;
    
    // Update participant list
    if (!entry->sourceName.empty()) {
        m_currentSession->participantNames[entry->sourceGUID] = entry->sourceName;
    }
    if (!entry->targetName.empty()) {
        m_currentSession->participantNames[entry->targetGUID] = entry->targetName;
    }
    
    // Update faction classifications (simplified)
    // TODO: Implement proper faction detection
}

// Game integration methods (these would be called from the actual combat log hooks)
void CombatLogManager::OnSpellDamage(WGUID sourceGUID, WGUID targetGUID, uint32_t spellId, 
                                   uint32_t damage, uint32_t overkill, uint32_t school, 
                                   uint32_t resisted, uint32_t blocked, uint32_t absorbed, 
                                   uint32_t hitFlags) {
    if (!m_isCapturing.load()) return;
    
    DamageLogEntry entry;
    entry.eventType = CombatEventType::SPELL_DAMAGE;
    entry.sourceGUID = sourceGUID;
    entry.targetGUID = targetGUID;
    entry.spellId = spellId;
    entry.amount = damage;
    entry.overAmount = overkill;
    entry.resisted = resisted;
    entry.blocked = blocked;
    entry.absorbed = absorbed;
    entry.hitFlags = static_cast<HitFlags>(hitFlags);
    entry.spellSchool = static_cast<SpellSchool>(school);
    entry.isMelee = false;
    entry.mitigated = resisted + blocked + absorbed;
    
    AddDamageEntry(entry);
}

void CombatLogManager::OnSpellHeal(WGUID sourceGUID, WGUID targetGUID, uint32_t spellId,
                                 uint32_t healing, uint32_t overheal, uint32_t hitFlags) {
    if (!m_isCapturing.load()) return;
    
    HealLogEntry entry;
    entry.eventType = CombatEventType::SPELL_HEAL;
    entry.sourceGUID = sourceGUID;
    entry.targetGUID = targetGUID;
    entry.spellId = spellId;
    entry.amount = healing;
    entry.overAmount = overheal;
    entry.hitFlags = static_cast<HitFlags>(hitFlags);
    entry.isCritical = (hitFlags & static_cast<uint32_t>(HitFlags::CRITICAL)) != 0;
    
    AddHealEntry(entry);
}

void CombatLogManager::OnMeleeDamage(WGUID sourceGUID, WGUID targetGUID, uint32_t damage,
                                   uint32_t overkill, uint32_t blocked, uint32_t absorbed,
                                   uint32_t hitFlags) {
    if (!m_isCapturing.load()) return;
    
    DamageLogEntry entry;
    entry.eventType = CombatEventType::MELEE_DAMAGE;
    entry.sourceGUID = sourceGUID;
    entry.targetGUID = targetGUID;
    entry.amount = damage;
    entry.overAmount = overkill;
    entry.blocked = blocked;
    entry.absorbed = absorbed;
    entry.hitFlags = static_cast<HitFlags>(hitFlags);
    entry.isMelee = true;
    entry.mitigated = blocked + absorbed;
    
    AddDamageEntry(entry);
}

void CombatLogManager::OnExperienceGain(uint32_t amount, bool isQuest, bool isGroupBonus) {
    if (!m_isCapturing.load()) return;
    
    ExperienceLogEntry entry;
    entry.amount = amount;
    entry.isQuest = isQuest;
    entry.isGroupBonus = isGroupBonus;
    
    // Get local player GUID
    if (auto objManager = ObjectManager::GetInstance()) {
        entry.targetGUID = objManager->GetLocalPlayerGuid();
        entry.targetName = "You";
    }
    
    AddExperienceEntry(entry);
}

void CombatLogManager::OnHonorGain(WGUID victimGUID, uint32_t amount, uint32_t rank) {
    if (!m_isCapturing.load()) return;
    
    HonorLogEntry entry;
    entry.sourceGUID = victimGUID;
    entry.amount = amount;
    entry.pvpRank = rank;
    
    // Get local player GUID
    if (auto objManager = ObjectManager::GetInstance()) {
        entry.targetGUID = objManager->GetLocalPlayerGuid();
        entry.targetName = "You";
    }
    
    AddHonorEntry(entry);
}

// WoW Memory Reading Implementation
void CombatLogManager::StartWowMemoryReading() {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    
    if (m_wowReadingActive.load()) {
        return; // Already active
    }
    
    m_wowReadingActive.store(true);
    
    LOG_INFO("Started WoW combat log memory reading");
    
    // Log the memory addresses we're going to use
    LOG_INFO("WoW Combat Log Addresses:");
    LOG_INFO("  g_CombatLogEntryListHead: 0x" + 
             std::to_string(WowCombatLogAddresses::g_CombatLogEntryListHead));
    LOG_INFO("  g_CombatLogEntryListTail: 0x" + 
             std::to_string(WowCombatLogAddresses::g_CombatLogEntryListTail));
    LOG_INFO("  g_CombatLogEventNameTable: 0x" + 
             std::to_string(WowCombatLogAddresses::g_CombatLogEventNameTable));
    
    // Initialize with current tail position (like Python implementation)
    LOG_INFO("Initializing with tail position...");
    try {
        // Try both approaches - direct addresses and manager-based
        LOG_INFO("Testing direct addresses:");
        uintptr_t tailPtrAddr = WowCombatLogAddresses::g_CombatLogEntryListTail;
        uintptr_t headPtrAddr = WowCombatLogAddresses::g_CombatLogEntryListHead;
        
        if (Memory::IsValidAddress(tailPtrAddr)) {
            uintptr_t tailNode = Memory::ReadPointer(tailPtrAddr);
            LOG_INFO("  Direct tail (0x" + std::to_string(tailPtrAddr) + "): 0x" + std::to_string(tailNode));
        }
        
        if (Memory::IsValidAddress(headPtrAddr)) {
            uintptr_t headNode = Memory::ReadPointer(headPtrAddr);
            LOG_INFO("  Direct head (0x" + std::to_string(headPtrAddr) + "): 0x" + std::to_string(headNode));
        }
        
        // Try manager-based approach
        LOG_INFO("Testing manager-based addresses:");
        uintptr_t managerAddr = WowCombatLogAddresses::g_CombatLogListManager;
        if (Memory::IsValidAddress(managerAddr)) {
            LOG_INFO("  Manager address is valid: 0x" + std::to_string(managerAddr));
            
            uintptr_t managerHeadAddr = managerAddr + WowCombatLogAddresses::g_CombatLogListHeadOffset;
            uintptr_t managerTailAddr = managerAddr + WowCombatLogAddresses::g_CombatLogListTailOffset;
            
            if (Memory::IsValidAddress(managerHeadAddr)) {
                uintptr_t managerHead = Memory::ReadPointer(managerHeadAddr);
                LOG_INFO("  Manager head (0x" + std::to_string(managerHeadAddr) + "): 0x" + std::to_string(managerHead));
            }
            
            if (Memory::IsValidAddress(managerTailAddr)) {
                uintptr_t managerTail = Memory::ReadPointer(managerTailAddr);
                LOG_INFO("  Manager tail (0x" + std::to_string(managerTailAddr) + "): 0x" + std::to_string(managerTail));
            }
        }
        
        // Try reading some raw memory around these addresses to see what's there
        LOG_INFO("Memory dump around manager area:");
        for (int i = -4; i <= 12; i += 4) {
            uintptr_t addr = managerAddr + i;
            if (Memory::IsValidAddress(addr)) {
                uintptr_t value = Memory::ReadPointer(addr);
                LOG_INFO("  [" + std::to_string(i) + "] 0x" + std::to_string(addr) + " = 0x" + std::to_string(value));
            }
        }
        
        // Initialize with 0 to force reading from the beginning
        // Don't set m_lastProcessedNodeAddr to current tail - that would skip everything!
        m_lastProcessedNodeAddr = 0;
        
        // Clear processed nodes set to start fresh
        m_processedNodeAddresses.clear();
        LOG_INFO("  Cleared processed nodes set and initialized lastProcessedNodeAddr to 0");
        
    } catch (const std::exception& e) {
        LOG_ERROR("  Exception during initialization: " + std::string(e.what()));
        m_lastProcessedNodeAddr = 0;
    }
}

void CombatLogManager::StopWowMemoryReading() {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    
    if (!m_wowReadingActive.load()) {
        return; // Already stopped
    }
    
    m_wowReadingActive.store(false);
    m_lastProcessedNodeAddr = 0;
    
    // Clear processed nodes set
    m_processedNodeAddresses.clear();
    
    LOG_INFO("Stopped WoW combat log memory reading");
}

void CombatLogManager::TriggerManualRead() {
    LOG_INFO("TriggerManualRead: Manual read triggered");
    
    // First, let's check current state and try to read any existing node
    LOG_INFO("TriggerManualRead: Current state analysis");
    try {
        uintptr_t headPtrAddr = WowCombatLogAddresses::g_CombatLogEntryListHead;
        uintptr_t tailPtrAddr = WowCombatLogAddresses::g_CombatLogEntryListTail;
        
        uintptr_t headNode = Memory::ReadPointer(headPtrAddr);
        uintptr_t tailNode = Memory::ReadPointer(tailPtrAddr);
        
        LOG_INFO("  Current head: 0x" + std::to_string(headNode));
        LOG_INFO("  Current tail: 0x" + std::to_string(tailNode));
        LOG_INFO("  Last processed: 0x" + std::to_string(m_lastProcessedNodeAddr));
        
        // If we have a tail node, try to read its structure
        if (tailNode != 0 && Memory::IsValidAddress(tailNode)) {
            LOG_INFO("  Attempting to read tail node structure...");
            WowCombatLogEntry tailEntry;
            if (Memory::ReadBytes(tailNode, &tailEntry, sizeof(WowCombatLogEntry))) {
                LOG_INFO("  Tail node data:");
                LOG_INFO("    pNext: 0x" + std::to_string(reinterpret_cast<uintptr_t>(tailEntry.pNext)));
                LOG_INFO("    pPrev: 0x" + std::to_string(reinterpret_cast<uintptr_t>(tailEntry.pPrev)));
                LOG_INFO("    timestamp: " + std::to_string(tailEntry.timestamp));
                LOG_INFO("    eventTypeIndex: " + std::to_string(tailEntry.eventTypeIndex));
                LOG_INFO("    srcGuid: 0x" + std::to_string(tailEntry.srcGuid));
                LOG_INFO("    destGuid: 0x" + std::to_string(tailEntry.destGuid));
                LOG_INFO("    spellId: " + std::to_string(tailEntry.spellId));
                
                // Check if this looks like valid data
                if (tailEntry.eventTypeIndex > 100 || tailEntry.eventTypeIndex < 0) {
                    LOG_WARNING("  Tail node contains invalid data (eventTypeIndex out of bounds), probably no real combat log entries");
                    LOG_INFO("TriggerManualRead: No valid combat log entries found, nothing to process");
                    return; // Exit early, don't try to process garbage data
                }
            } else {
                LOG_WARNING("  Failed to read tail node structure");
                return;
            }
        } else {
            LOG_INFO("  No valid tail node, combat log is empty");
            return;
        }
        
        // Check if head and tail are different (indicating multiple entries)
        if (headNode != 0 && headNode != tailNode) {
            LOG_INFO("  Head and tail are different - multiple entries exist");
        } else if (headNode == 0 && tailNode != 0) {
            LOG_WARNING("  Head is null but tail exists - unusual state");
        } else if (headNode == tailNode && headNode != 0) {
            LOG_INFO("  Head equals tail - single entry in list");
        } else {
            LOG_INFO("  Both head and tail are null - empty list");
        }
        
    } catch (const std::exception& e) {
        LOG_ERROR("TriggerManualRead: Exception during state analysis: " + std::string(e.what()));
    }
    
    // Ensure we have a session to add entries to
    if (!HasActiveSession()) {
        LOG_INFO("TriggerManualRead: No active session, starting new session");
        StartNewSession();
    }
    
    // Temporarily enable reading for manual trigger
    bool wasActive = m_wowReadingActive.load();
    bool wasCapturing = m_isCapturing.load();
    
    if (!wasActive) {
        LOG_INFO("TriggerManualRead: Temporarily enabling WoW reading for manual read");
        m_wowReadingActive.store(true);
    }
    
    if (!wasCapturing) {
        LOG_INFO("TriggerManualRead: Temporarily enabling capture for manual read");
        m_isCapturing.store(true);
    }
    
    try {
        ReadWowCombatLogEntries();
    } catch (const std::exception& e) {
        LOG_ERROR("TriggerManualRead: Exception during read: " + std::string(e.what()));
    }
    
    // Restore previous states
    if (!wasActive) {
        m_wowReadingActive.store(false);
        LOG_INFO("TriggerManualRead: Restored WoW reading state");
    }
    
    if (!wasCapturing) {
        m_isCapturing.store(false);
        LOG_INFO("TriggerManualRead: Restored capture state");
    }
}

void CombatLogManager::UpdateCombatLogReading() {
    // This is called from the GUI Update loop to trigger automatic reading
    // Add extra safety checks since this runs frequently
    
    if (!m_wowReadingActive.load()) {
        return; // Extra safety check
    }
    
    if (!m_isCapturing.load()) {
        return; // Extra safety check
    }
    
    // Add a simple rate limiting mechanism
    static auto lastReadTime = std::chrono::steady_clock::now();
    auto now = std::chrono::steady_clock::now();
    auto timeSinceLastRead = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastReadTime);
    
    // Don't read more than once every 50ms (20 times per second max)
    if (timeSinceLastRead.count() < 50) {
        return;
    }
    
    lastReadTime = now;
    
    // Periodically clean up old processed nodes to prevent memory buildup
    static auto lastCleanupTime = std::chrono::steady_clock::now();
    auto timeSinceLastCleanup = std::chrono::duration_cast<std::chrono::minutes>(now - lastCleanupTime);
    
    if (timeSinceLastCleanup.count() >= 5) { // Clean up every 5 minutes
        if (m_processedNodeAddresses.size() > 1000) { // Only if we have many entries
            LOG_INFO("UpdateCombatLogReading: Cleaning up old processed nodes (" + 
                     std::to_string(m_processedNodeAddresses.size()) + " entries)");
            m_processedNodeAddresses.clear();
            m_lastProcessedNodeAddr = 0; // Reset to re-read from beginning
        }
        lastCleanupTime = now;
    }
    
    try {
        ReadWowCombatLogEntries();
    } catch (const std::exception& e) {
        LOG_ERROR("UpdateCombatLogReading: Exception caught: " + std::string(e.what()));
        // Don't rethrow - just log and continue
    } catch (...) {
        LOG_ERROR("UpdateCombatLogReading: Unknown exception caught");
        // Don't rethrow - just log and continue
    }
}

void CombatLogManager::ReadWowCombatLogEntries() {
    if (!m_wowReadingActive.load()) {
        LOG_DEBUG("ReadWowCombatLogEntries: WoW reading not active");
        return;
    }
    
    if (!m_isCapturing.load()) {
        LOG_DEBUG("ReadWowCombatLogEntries: Capture not active");
        return;
    }
    
    // Starting read cycle
    
    int processedCount = 0;
    const int maxProcessPerTick = 200; // Safety limit
    
    try {
        // Get current head and tail pointers (like Python implementation)
        uintptr_t headPtrAddr = WowCombatLogAddresses::g_CombatLogEntryListHead;
        uintptr_t tailPtrAddr = WowCombatLogAddresses::g_CombatLogEntryListTail;
        
        if (!Memory::IsValidAddress(headPtrAddr) || !Memory::IsValidAddress(tailPtrAddr)) {
            LOG_WARNING("ReadWowCombatLogEntries: Head or tail pointer addresses are invalid");
            return;
        }
        
        uintptr_t currentHeadNodeAddr = Memory::ReadPointer(headPtrAddr);
        uintptr_t targetTailNodeAddr = Memory::ReadPointer(tailPtrAddr);
        
        // Head/Tail/LastRead debug info removed for cleaner logs
        
        // Check if tail is null (empty list)
        if (targetTailNodeAddr == 0) {
            // Tail pointer is null, list might be empty
            return;
        }
        
        // Determine starting point - we want to read ALL entries, not just new ones
        uintptr_t currentNodeAddr = 0;
        
        if (currentHeadNodeAddr != 0) {
            // Normal case: start from head and read all entries
            currentNodeAddr = currentHeadNodeAddr;
            // Starting from head
        } else if (targetTailNodeAddr != 0) {
            // Special case: head is null but tail exists, traverse backwards from tail
            // Head is null but tail exists, will traverse backwards from tail
            
            // Additional safety check for the tail address
            if (targetTailNodeAddr < 0x10000) {
                LOG_ERROR("ReadWowCombatLogEntries: Tail address 0x" + std::to_string(targetTailNodeAddr) + " is too low, skipping");
                return;
            }
            
            // Use memory validation instead of arbitrary address limits
            if (!Memory::IsValidAddress(targetTailNodeAddr)) {
                LOG_ERROR("ReadWowCombatLogEntries: Tail address 0x" + std::to_string(targetTailNodeAddr) + " failed memory validation, skipping");
                return;
            }
            
            // Tail address passed validation, proceeding with backwards traversal
            
            // Try to read the tail node first to validate it's accessible
            WowCombatLogEntry testEntry;
            if (!Memory::ReadBytes(targetTailNodeAddr, &testEntry, sizeof(WowCombatLogEntry))) {
                LOG_ERROR("ReadWowCombatLogEntries: Cannot read tail node at 0x" + std::to_string(targetTailNodeAddr) + ", memory may be inaccessible");
                return;
            }
            
            // Validate the tail node data looks reasonable
            if (testEntry.eventTypeIndex < 0 || testEntry.eventTypeIndex > 100) {
                // Throttle this warning - only log it occasionally to avoid spam
                static std::chrono::steady_clock::time_point lastWarningTime;
                auto now = std::chrono::steady_clock::now();
                auto timeSinceLastWarning = std::chrono::duration_cast<std::chrono::seconds>(now - lastWarningTime);
                
                if (timeSinceLastWarning.count() >= 10) { // Only log every 10 seconds
                    LOG_WARNING("ReadWowCombatLogEntries: Tail node has invalid eventTypeIndex " + std::to_string(testEntry.eventTypeIndex) + ", skipping backwards traversal");
                    lastWarningTime = now;
                }
                return;
            }
            
            // First, let's traverse backwards to find the actual head
            std::vector<uintptr_t> nodeAddresses;
            uintptr_t traverseAddr = targetTailNodeAddr;
            int maxTraverse = 1000; // Safety limit
            
            while (traverseAddr != 0 && maxTraverse-- > 0) {
                // Validate address before adding
                if (traverseAddr < 0x10000) {
                    LOG_WARNING("ReadWowCombatLogEntries: Invalid traverse address 0x" + std::to_string(traverseAddr) + ", stopping traversal");
                    break;
                }
                
                if (!Memory::IsValidAddress(traverseAddr)) {
                    // Throttle this warning - it can spam heavily during invalid memory states
                    static std::chrono::steady_clock::time_point lastAddressWarningTime;
                    auto now = std::chrono::steady_clock::now();
                    auto timeSinceLastWarning = std::chrono::duration_cast<std::chrono::seconds>(now - lastAddressWarningTime);
                    
                    if (timeSinceLastWarning.count() >= 5) { // Only log every 5 seconds for this frequent warning
                        LOG_WARNING("ReadWowCombatLogEntries: Address 0x" + std::to_string(traverseAddr) + " is not valid, stopping traversal");
                        lastAddressWarningTime = now;
                    }
                    break;
                }
                
                nodeAddresses.push_back(traverseAddr);
                
                // Try to read pPrev pointer (offset 0x04) safely
                try {
                    uintptr_t prevAddr = Memory::ReadPointer(traverseAddr + 0x04);
                    if (prevAddr == 0 || prevAddr == traverseAddr) {
                        break; // Found the head or detected loop
                    }
                    
                    // Additional safety check for prev address
                    if (prevAddr < 0x10000) {
                        LOG_WARNING("ReadWowCombatLogEntries: Invalid pPrev address 0x" + std::to_string(prevAddr) + ", stopping traversal");
                        break;
                    }
                    
                    traverseAddr = prevAddr;
                } catch (const std::exception& e) {
                    LOG_ERROR("ReadWowCombatLogEntries: Exception reading pPrev at 0x" + std::to_string(traverseAddr) + ": " + e.what());
                    break;
                }
            }
            
            // Now process in forward order (from head to tail), but only NEW entries
            if (!nodeAddresses.empty()) {
                // Only log if we actually process new entries
                
                // Process nodes in reverse order (head first), but skip already processed ones
                for (int i = nodeAddresses.size() - 1; i >= 0; i--) {
                    uintptr_t nodeAddr = nodeAddresses[i];
                    
                    // Skip if we've already processed this node
                    if (m_processedNodeAddresses.find(nodeAddr) != m_processedNodeAddresses.end()) {
                        continue;
                    }
                    
                    try {
                        // Safety check for backwards traversal
                        if (nodeAddr < 0x10000) {
                            LOG_ERROR("ReadWowCombatLogEntries: Invalid backwards node address 0x" + std::to_string(nodeAddr));
                            continue;
                        }
                        
                        WowCombatLogEntry wowEntry;
                        // Initialize the structure to zero to avoid garbage data
                        memset(&wowEntry, 0, sizeof(wowEntry));
                        
                        if (!Memory::ReadBytes(nodeAddr, &wowEntry, sizeof(WowCombatLogEntry))) {
                            LOG_WARNING("ReadWowCombatLogEntries: Failed to read node at 0x" + std::to_string(nodeAddr));
                            continue;
                        }
                        
                                        // Validate data before processing
                if (wowEntry.eventTypeIndex > 100 || wowEntry.eventTypeIndex < 0) {
                    // Throttle this warning to avoid spam
                    static std::chrono::steady_clock::time_point lastInvalidWarningTime;
                    auto now = std::chrono::steady_clock::now();
                    auto timeSinceLastWarning = std::chrono::duration_cast<std::chrono::seconds>(now - lastInvalidWarningTime);
                    
                    if (timeSinceLastWarning.count() >= 10) { // Only log every 10 seconds
                        LOG_WARNING("ReadWowCombatLogEntries: Invalid eventTypeIndex " + std::to_string(wowEntry.eventTypeIndex) + 
                                   " at node 0x" + std::to_string(nodeAddr) + ", skipping");
                        lastInvalidWarningTime = now;
                    }
                    
                    // Mark this node as processed so we don't hit it again
                    m_processedNodeAddresses.insert(nodeAddr);
                    continue;
                }
                        
                        // Only log when processing new nodes
                        
                        auto convertedEntry = ConvertWowEntry(wowEntry);
                        if (convertedEntry) {
                            AddEntry(convertedEntry);
                            processedCount++;
                            
                            // Mark this node as processed
                            m_processedNodeAddresses.insert(nodeAddr);
                        }
                        
                    } catch (const std::exception& e) {
                        LOG_ERROR("ReadWowCombatLogEntries: Exception processing node 0x" + std::to_string(nodeAddr) + ": " + e.what());
                    }
                }
                
                m_lastProcessedNodeAddr = targetTailNodeAddr;
                
                // Only log if we actually processed new entries
                if (processedCount > 0) {
                    LOG_INFO("ReadWowCombatLogEntries: Processed " + std::to_string(processedCount) + " NEW entries via backwards traversal");
                }
                return;
            } else {
                LOG_WARNING("ReadWowCombatLogEntries: No valid nodes found in backwards traversal");
                return;
            }
        } else {
            LOG_DEBUG("ReadWowCombatLogEntries: Both head and tail are null, no entries to process");
            return;
        }
        
        if (currentNodeAddr == 0) {
            LOG_DEBUG("ReadWowCombatLogEntries: Start node is null, nothing to process");
            return;
        }
        
        // Process entries from head to tail (normal forward traversal)
        
        while (currentNodeAddr != 0 && 
               Memory::IsValidAddress(currentNodeAddr) && 
               processedCount < maxProcessPerTick) {
            
            // Skip if we've already processed this node
            if (m_processedNodeAddresses.find(currentNodeAddr) != m_processedNodeAddresses.end()) {
                // Move to next node
                uintptr_t nextNodeAddr = Memory::ReadPointer(currentNodeAddr);
                currentNodeAddr = nextNodeAddr;
                continue;
            }
            
            // Processing new node
            
            try {
                // Safety check: ensure the address looks reasonable
                if (currentNodeAddr < 0x10000) {
                    LOG_ERROR("ReadWowCombatLogEntries: Invalid node address 0x" + std::to_string(currentNodeAddr));
                    break;
                }
                
                // Read the entire node structure from memory
                WowCombatLogEntry wowEntry;
                size_t nodeSize = sizeof(WowCombatLogEntry);
                
                // Initialize the structure to zero to avoid garbage data
                memset(&wowEntry, 0, sizeof(wowEntry));
                
                if (!Memory::ReadBytes(currentNodeAddr, &wowEntry, nodeSize)) {
                    LOG_WARNING("ReadWowCombatLogEntries: Failed to read node data at 0x" + 
                               std::to_string(currentNodeAddr));
                    break;
                }
                
                // Node data logging removed for cleaner output
                
                // Validate data before processing
                if (wowEntry.eventTypeIndex > 100 || wowEntry.eventTypeIndex < 0) {
                    // Throttle this warning to avoid spam
                    static std::chrono::steady_clock::time_point lastForwardInvalidWarningTime;
                    auto now = std::chrono::steady_clock::now();
                    auto timeSinceLastWarning = std::chrono::duration_cast<std::chrono::seconds>(now - lastForwardInvalidWarningTime);
                    
                    if (timeSinceLastWarning.count() >= 10) { // Only log every 10 seconds
                        LOG_WARNING("ReadWowCombatLogEntries: Invalid eventTypeIndex " + std::to_string(wowEntry.eventTypeIndex) + 
                                   " at node 0x" + std::to_string(currentNodeAddr) + ", skipping garbage data");
                        lastForwardInvalidWarningTime = now;
                    }
                    
                    // Mark this node as processed so we don't hit it again
                    m_processedNodeAddresses.insert(currentNodeAddr);
                    
                    // Move to next node instead of stopping
                    uintptr_t nextNodeAddr = Memory::ReadPointer(currentNodeAddr);
                    currentNodeAddr = nextNodeAddr;
                    continue;
                }
                
                // Additional validation checks
                if (wowEntry.timestamp == 0) {
                    LOG_WARNING("ReadWowCombatLogEntries: Timestamp is 0 at node 0x" + std::to_string(currentNodeAddr) + ", likely garbage data");
                    
                    // Mark this node as processed so we don't hit it again
                    m_processedNodeAddresses.insert(currentNodeAddr);
                    
                    // Move to next node instead of stopping
                    uintptr_t nextNodeAddr = Memory::ReadPointer(currentNodeAddr);
                    currentNodeAddr = nextNodeAddr;
                    continue;
                }
                
                // Check if pointers look reasonable (not null and within reasonable range)
                if (wowEntry.pNext != nullptr) {
                    uintptr_t nextAddr = reinterpret_cast<uintptr_t>(wowEntry.pNext);
                    if (nextAddr < 0x10000 || nextAddr > 0x7FFFFFFF) {
                        LOG_WARNING("ReadWowCombatLogEntries: Invalid pNext pointer 0x" + std::to_string(nextAddr));
                        wowEntry.pNext = nullptr; // Sanitize
                    }
                }
                
                if (wowEntry.pPrev != nullptr) {
                    uintptr_t prevAddr = reinterpret_cast<uintptr_t>(wowEntry.pPrev);
                    if (prevAddr < 0x10000 || prevAddr > 0x7FFFFFFF) {
                        LOG_WARNING("ReadWowCombatLogEntries: Invalid pPrev pointer 0x" + std::to_string(prevAddr));
                        wowEntry.pPrev = nullptr; // Sanitize
                    }
                }
                
                // Convert to our internal format
                auto convertedEntry = ConvertWowEntry(wowEntry);
                if (convertedEntry) {
                    // Successfully converted and added entry
                    AddEntry(convertedEntry);
                    processedCount++;
                    
                    // Mark this node as processed
                    m_processedNodeAddresses.insert(currentNodeAddr);
                } else {
                    LOG_WARNING("ReadWowCombatLogEntries: Failed to convert entry");
                }
                
                // Update last processed node
                m_lastProcessedNodeAddr = currentNodeAddr;
                
                // Check if we just processed the target tail
                if (currentNodeAddr == targetTailNodeAddr) {
                    // Reached target tail, breaking
                    break;
                }
                
                // Move to next node
                uintptr_t nextNodeAddr = reinterpret_cast<uintptr_t>(wowEntry.pNext);
                // Moving to next node
                
                // Sanity check for infinite loops
                if (nextNodeAddr == currentNodeAddr) {
                    LOG_ERROR("ReadWowCombatLogEntries: Detected loop, breaking");
                    m_lastProcessedNodeAddr = 0; // Reset on error
                    break;
                }
                
                currentNodeAddr = nextNodeAddr;
                
            } catch (const std::exception& e) {
                LOG_ERROR("ReadWowCombatLogEntries: Exception processing node: " + std::string(e.what()));
                m_lastProcessedNodeAddr = 0; // Reset on error
                break;
            }
        }
        
        if (processedCount > 0) {
            LOG_INFO("ReadWowCombatLogEntries: Processed " + std::to_string(processedCount) + " new entries");
        } else {
            LOG_DEBUG("ReadWowCombatLogEntries: No new entries to process");
        }
        
        if (processedCount >= maxProcessPerTick) {
            LOG_WARNING("ReadWowCombatLogEntries: Hit processing limit, some events might be delayed");
        }
        
    } catch (const std::exception& e) {
        LOG_ERROR("Error in ReadWowCombatLogEntries: " + std::string(e.what()));
        m_lastProcessedNodeAddr = 0; // Reset on error
    }
}

std::shared_ptr<CombatLogEntry> CombatLogManager::ConvertWowEntry(const WowCombatLogEntry& wowEntry) {
    try {
        LOG_DEBUG("ConvertWowEntry: Starting conversion");
        
        auto entry = std::make_shared<CombatLogEntry>();
        
        // Convert timestamp (WoW uses GetTickCount(), we use steady_clock)
        auto now = std::chrono::steady_clock::now();
        auto currentTick = GetTickCount();
        auto timeDiff = std::chrono::milliseconds(currentTick - wowEntry.timestamp);
        entry->timestamp = now - timeDiff;
        
        LOG_DEBUG("ConvertWowEntry: Timestamp converted, tick diff: " + std::to_string(timeDiff.count()) + "ms");
        
        // Convert event type
        std::string eventTypeName = GetEventTypeName(wowEntry.eventTypeIndex);
        LOG_DEBUG("ConvertWowEntry: Event type name: '" + eventTypeName + "'");
        
        if (eventTypeName == "SWING_DAMAGE" || eventTypeName == "RANGE_DAMAGE") {
            entry->eventType = CombatEventType::MELEE_DAMAGE;
        } else if (eventTypeName == "SPELL_DAMAGE") {
            entry->eventType = CombatEventType::SPELL_DAMAGE;
        } else if (eventTypeName == "SPELL_HEAL" || eventTypeName == "SPELL_PERIODIC_HEAL") {
            entry->eventType = CombatEventType::SPELL_HEAL;
        } else if (eventTypeName == "SPELL_MISSED") {
            entry->eventType = CombatEventType::SPELL_MISS;
        } else if (eventTypeName == "SPELL_CAST_SUCCESS") {
            entry->eventType = CombatEventType::SPELL_CAST_SUCCESS;
        } else if (eventTypeName == "SPELL_CAST_START") {
            entry->eventType = CombatEventType::SPELL_CAST_START;
        } else if (eventTypeName == "SPELL_CAST_FAILED") {
            entry->eventType = CombatEventType::SPELL_CAST_FAILED;
        } else {
            entry->eventType = CombatEventType::UNKNOWN;
        }
        
        LOG_DEBUG("ConvertWowEntry: Event type mapped to: " + std::to_string(static_cast<int>(entry->eventType)));
        
        // Convert GUIDs
        entry->sourceGUID = WGUID(wowEntry.srcGuid);
        entry->targetGUID = WGUID(wowEntry.destGuid);
        
        LOG_DEBUG("ConvertWowEntry: GUIDs converted - src: 0x" + std::to_string(wowEntry.srcGuid) + 
                 ", dest: 0x" + std::to_string(wowEntry.destGuid));
        
        // Convert source name - try memory pointer first, then ObjectManager as fallback
        if (wowEntry.srcGuid != 0) {
            entry->sourceName = "Unknown";
            
            // Try reading from memory pointer first (more reliable for combat log)
            if (wowEntry.srcName && Memory::IsValidAddress(reinterpret_cast<uintptr_t>(wowEntry.srcName))) {
                try {
                    std::string memoryName = Memory::ReadString(reinterpret_cast<uintptr_t>(wowEntry.srcName));
                    // Validate the string isn't garbage
                    if (!memoryName.empty() && memoryName.length() <= 50 && 
                        memoryName.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789 -'") == std::string::npos) {
                        entry->sourceName = memoryName;
                        LOG_DEBUG("ConvertWowEntry: Source name from memory: '" + entry->sourceName + "'");
                    } else {
                        LOG_DEBUG("ConvertWowEntry: Source name from memory appears to be garbage: '" + memoryName + "'");
                    }
                } catch (...) {
                    LOG_DEBUG("ConvertWowEntry: Exception reading source name from memory");
                }
            }
            
            // If memory read failed, try ObjectManager as fallback
            if (entry->sourceName == "Unknown") {
                std::string objManagerName = ResolveEntityName(entry->sourceGUID);
                if (objManagerName.find("Unknown-") != 0) {
                    entry->sourceName = objManagerName;
                    LOG_DEBUG("ConvertWowEntry: Source name from ObjectManager: '" + entry->sourceName + "'");
                }
            }
            
            if (entry->sourceName != "Unknown") {
                LOG_DEBUG("ConvertWowEntry: Source name resolved: '" + entry->sourceName + "'");
            } else {
                LOG_DEBUG("ConvertWowEntry: Could not resolve source name for GUID 0x" + std::to_string(wowEntry.srcGuid));
            }
        } else {
            entry->sourceName = ""; // No source for this event
            LOG_DEBUG("ConvertWowEntry: No source for this event (srcGuid is 0)");
        }
        
        if (wowEntry.destGuid != 0) {
            entry->targetName = "Unknown";
            
            // Try reading from memory pointer first (more reliable for combat log)
            if (wowEntry.destName && Memory::IsValidAddress(reinterpret_cast<uintptr_t>(wowEntry.destName))) {
                try {
                    std::string memoryName = Memory::ReadString(reinterpret_cast<uintptr_t>(wowEntry.destName));
                    // Validate the string isn't garbage
                    if (!memoryName.empty() && memoryName.length() <= 50 && 
                        memoryName.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789 -'") == std::string::npos) {
                        entry->targetName = memoryName;
                        LOG_DEBUG("ConvertWowEntry: Target name from memory: '" + entry->targetName + "'");
                    } else {
                        LOG_DEBUG("ConvertWowEntry: Target name from memory appears to be garbage: '" + memoryName + "'");
                    }
                } catch (...) {
                    LOG_DEBUG("ConvertWowEntry: Exception reading target name from memory");
                }
            }
            
            // If memory read failed, try ObjectManager as fallback
            if (entry->targetName == "Unknown") {
                std::string objManagerName = ResolveEntityName(entry->targetGUID);
                if (objManagerName.find("Unknown-") != 0) {
                    entry->targetName = objManagerName;
                    LOG_DEBUG("ConvertWowEntry: Target name from ObjectManager: '" + entry->targetName + "'");
                }
            }
            
            if (entry->targetName != "Unknown") {
                LOG_DEBUG("ConvertWowEntry: Target name resolved: '" + entry->targetName + "'");
            } else {
                LOG_DEBUG("ConvertWowEntry: Could not resolve target name for GUID 0x" + std::to_string(wowEntry.destGuid));
            }
        } else {
            // No target GUID - check if this is a self-cast (source exists but target is 0)
            if (wowEntry.srcGuid != 0 && !entry->sourceName.empty() && entry->sourceName != "Unknown") {
                // This is likely a self-cast, use source name as target
                entry->targetName = entry->sourceName;
                LOG_DEBUG("ConvertWowEntry: Self-cast detected, using source name as target: '" + entry->targetName + "'");
            } else {
                entry->targetName = ""; // No target for this event
                LOG_DEBUG("ConvertWowEntry: No target for this event (destGuid is 0)");
            }
        }
        
        // ===== CORRECTED SPELL & SCHOOL LOGIC =====
        // CRITICAL: Initialize to safe values - NEVER read spellId/spellSchool unconditionally!
        entry->spellId = 0;
        entry->spellSchoolMask = 0;
        entry->spellSchool = SPELL_SCHOOL_NORMAL; // Default
        
        // Add debug logging for raw values - using CORRECTED offsets from RE analysis
        LOG_DEBUG("ConvertWowEntry: Raw values from memory - spellId(+0x44): " + std::to_string(wowEntry.spellId) + 
                 ", spellSchool(+0x4C): 0x" + std::to_string(wowEntry.spellSchool) + 
                 ", subEventFlags: 0x" + std::to_string(wowEntry.subEventFlags));
        LOG_DEBUG("ConvertWowEntry: NOTE - spellId(+0x44) is only valid when subEventFlags & 0x01 is set!");
        
        // 1. Check for MAIN spell info block (spellId, spellSchool)
        // Based on testing, spell ID at +0x44 appears to be valid even without 0x01 flag!
        uint32_t rawSpellId = wowEntry.spellId;
        uint32_t rawSpellSchool = wowEntry.spellSchool;
        
        if (wowEntry.subEventFlags & 0x01) {
            // Flag indicates spell info is definitely valid
            entry->spellId = rawSpellId;
            entry->spellSchoolMask = rawSpellSchool;
            LOG_INFO("ConvertWowEntry: *** MAIN SPELL INFO VALID (0x01 flag set) ***");
            LOG_INFO("  ✅ Spell ID from +0x44: " + std::to_string(entry->spellId));
            LOG_INFO("  ✅ Spell School from +0x4C: 0x" + std::to_string(entry->spellSchoolMask) + " (" + GetSchoolMaskName(entry->spellSchoolMask) + ")");
        } else {
            // Flag not set, but check if spell ID looks valid anyway (1-100000 range)
            if (rawSpellId > 0 && rawSpellId < 100000) {
                entry->spellId = rawSpellId;
                LOG_INFO("ConvertWowEntry: *** SPELL ID APPEARS VALID despite 0x01 flag not set ***");
                LOG_INFO("  ✅ Using Spell ID from +0x44: " + std::to_string(entry->spellId) + " (looks valid!)");
                
                // For spell school, only use if it looks like a valid bitmask (1-127)
                if (rawSpellSchool > 0 && rawSpellSchool <= 0x7F) {
                    entry->spellSchoolMask = rawSpellSchool;
                    LOG_INFO("  ✅ Using Spell School from +0x4C: 0x" + std::to_string(entry->spellSchoolMask) + " (" + GetSchoolMaskName(entry->spellSchoolMask) + ")");
                } else {
                    LOG_INFO("  ⚠️ Spell School from +0x4C looks invalid: 0x" + std::to_string(rawSpellSchool) + " (not using)");
                }
            } else {
                LOG_INFO("ConvertWowEntry: *** MAIN SPELL INFO NOT VALID (0x01 flag not set and spell ID looks invalid) ***");
                LOG_INFO("  ❌ Raw spellId: " + std::to_string(rawSpellId) + " (out of valid range)");
                LOG_INFO("  ❌ Raw spellSchool: 0x" + std::to_string(rawSpellSchool) + " (not using)");
            }
        }
        
        // 2. Check for EXTRA spell info block (procs, aura dose events) - when subEventFlags & 0x02
        if (wowEntry.subEventFlags & 0x02) {
            uint32_t extraSpellId = static_cast<uint32_t>(wowEntry.payload.genericParams.param7);
            if (extraSpellId > 0 && entry->spellId == 0) {
                entry->spellId = extraSpellId;
                LOG_INFO("ConvertWowEntry: ✅ Using extra spell ID from param7: " + std::to_string(extraSpellId));
            }
        }
        
        // 3. For damage/heal events, look for spell ID in the damage calculation block
        if (wowEntry.subEventFlags & 0x10) {
            LOG_INFO("ConvertWowEntry: 🔍 SPELL ID DEBUG - Damage calculation block active, checking all parameters:");
            LOG_INFO("  param1: " + std::to_string(wowEntry.payload.genericParams.param1) + " (could be spell ID?)");
            LOG_INFO("  param2: " + std::to_string(wowEntry.payload.genericParams.param2) + " (damage amount)");
            LOG_INFO("  param3: " + std::to_string(wowEntry.payload.genericParams.param3) + " (overkill or spell ID?)");
            LOG_INFO("  param4: " + std::to_string(wowEntry.payload.genericParams.param4) + " (school mask)");
            LOG_INFO("  param5: " + std::to_string(wowEntry.payload.genericParams.param5) + " (blocked or spell ID?)");
            LOG_INFO("  param6: " + std::to_string(wowEntry.payload.genericParams.param6) + " (absorbed or spell ID?)");
            LOG_INFO("  param7: " + std::to_string(wowEntry.payload.genericParams.param7) + " (extra spell ID?)");
            
            // Try to find spell ID in parameters if we don't have one yet
            if (entry->spellId == 0) {
                // Check various parameters for reasonable spell ID values (typically 1-100000 range)
                std::vector<std::pair<std::string, uint32_t>> candidates = {
                    {"param1", static_cast<uint32_t>(wowEntry.payload.genericParams.param1)},
                    {"param3", static_cast<uint32_t>(wowEntry.payload.genericParams.param3)},
                    {"param5", static_cast<uint32_t>(wowEntry.payload.genericParams.param5)},
                    {"param6", static_cast<uint32_t>(wowEntry.payload.genericParams.param6)},
                    {"param7", static_cast<uint32_t>(wowEntry.payload.genericParams.param7)}
                };
                
                for (const auto& candidate : candidates) {
                    uint32_t value = candidate.second;
                    // Reasonable spell ID range check (WoW 3.3.5a spell IDs are typically 1-100000)
                    if (value > 0 && value < 100000) {
                        LOG_INFO("ConvertWowEntry: 🤔 POTENTIAL spell ID candidate in " + candidate.first + ": " + std::to_string(value));
                    }
                }
            }
        }
        
        // Convert bitmask to legacy enum for compatibility (use first set bit)
        if (entry->spellSchoolMask & static_cast<uint32_t>(SpellSchoolMask::HOLY)) {
            entry->spellSchool = SPELL_SCHOOL_HOLY;
        } else if (entry->spellSchoolMask & static_cast<uint32_t>(SpellSchoolMask::FIRE)) {
            entry->spellSchool = SPELL_SCHOOL_FIRE;
        } else if (entry->spellSchoolMask & static_cast<uint32_t>(SpellSchoolMask::NATURE)) {
            entry->spellSchool = SPELL_SCHOOL_NATURE;
        } else if (entry->spellSchoolMask & static_cast<uint32_t>(SpellSchoolMask::FROST)) {
            entry->spellSchool = SPELL_SCHOOL_FROST;
        } else if (entry->spellSchoolMask & static_cast<uint32_t>(SpellSchoolMask::SHADOW)) {
            entry->spellSchool = SPELL_SCHOOL_SHADOW;
        } else if (entry->spellSchoolMask & static_cast<uint32_t>(SpellSchoolMask::ARCANE)) {
            entry->spellSchool = SPELL_SCHOOL_ARCANE;
        } else {
            entry->spellSchool = SPELL_SCHOOL_NORMAL;  // Physical/Normal
        }
        
        // Try to get spell name if we have a valid spell ID
        if (entry->spellId > 0) {
            entry->spellName = GetSpellNameById(entry->spellId);
        }
        
        LOG_INFO("ConvertWowEntry: 🎯 FINAL SPELL INFO BEING USED:");
        LOG_INFO("  ✅ Spell ID: " + std::to_string(entry->spellId));
        LOG_INFO("  ✅ School Mask: 0x" + std::to_string(entry->spellSchoolMask) + " (" + GetSchoolMaskName(entry->spellSchoolMask) + ")");
        LOG_INFO("  ✅ Spell Name: '" + entry->spellName + "'");

        // Convert parameters based on event type and subEventFlags
        // Different event types use parameters differently
        LOG_DEBUG("ConvertWowEntry: SubEventFlags: 0x" + std::to_string(wowEntry.subEventFlags));
        
        // Parse parameters based on assembly analysis of PushCombatLogToFrameScript
        // The key flag is 0x10 - when set, it triggers the 9-argument damage/heal block
        if (wowEntry.subEventFlags & 0x10) {
            // This is a damage/heal event with the full combat calculation data
            LOG_DEBUG("ConvertWowEntry: Processing damage/heal event (subEventFlags & 0x10)");
            
            // Based on assembly analysis and observation:
            // For healing events with 0x10 flag, handle parameters differently
            if (entry->eventType == CombatEventType::SPELL_HEAL) {
                LOG_INFO("ConvertWowEntry: HEALING EVENT DEBUG - All parameters:");
                LOG_INFO("  param1: " + std::to_string(wowEntry.payload.genericParams.param1));
                LOG_INFO("  param2: " + std::to_string(wowEntry.payload.genericParams.param2));
                LOG_INFO("  param3: " + std::to_string(wowEntry.payload.genericParams.param3));
                LOG_INFO("  param4: " + std::to_string(wowEntry.payload.genericParams.param4));
                LOG_INFO("  param5: " + std::to_string(wowEntry.payload.genericParams.param5));
                LOG_INFO("  param6: " + std::to_string(wowEntry.payload.genericParams.param6));
                LOG_INFO("  param7: " + std::to_string(wowEntry.payload.genericParams.param7));
                LOG_INFO("  subEventFlags: 0x" + std::to_string(wowEntry.subEventFlags));
                
                // Try param1 as effective heal, param2 as overheal
                entry->amount = static_cast<uint32_t>(wowEntry.payload.genericParams.param1);      // Effective heal
                entry->overAmount = static_cast<uint32_t>(wowEntry.payload.genericParams.param2);  // Overheal
                
                LOG_INFO("ConvertWowEntry: Using param1=" + std::to_string(entry->amount) + 
                        " as effective heal, param2=" + std::to_string(entry->overAmount) + " as overheal");
            } else {
                // For damage events with 0x10 flag - use the correct parameter mapping
                LOG_INFO("ConvertWowEntry: DAMAGE EVENT DEBUG - All parameters:");
                LOG_INFO("  param1: " + std::to_string(wowEntry.payload.genericParams.param1));
                LOG_INFO("  param2: " + std::to_string(wowEntry.payload.genericParams.param2));
                LOG_INFO("  param3: " + std::to_string(wowEntry.payload.genericParams.param3));
                LOG_INFO("  param4: " + std::to_string(wowEntry.payload.genericParams.param4));
                LOG_INFO("  param5: " + std::to_string(wowEntry.payload.genericParams.param5));
                LOG_INFO("  param6: " + std::to_string(wowEntry.payload.genericParams.param6));
                LOG_INFO("  param7: " + std::to_string(wowEntry.payload.genericParams.param7));
                LOG_INFO("  subEventFlags: 0x" + std::to_string(wowEntry.subEventFlags));
                
                // CORRECTED DAMAGE PARAMETER MAPPING based on analysis
                // From the logs, we can see that for melee damage:
                // param1 = 0 (this is wrong - means we're reading the wrong field)
                // param2 = actual damage amount (29, 35, 39, 42, etc.)
                // param3 = 0 
                // param4 = school or resisted amount
                
                // The issue is that for SWING_DAMAGE events, the damage is in param2, not param1!
                if (entry->eventType == CombatEventType::MELEE_DAMAGE) {
                    // For melee damage: param2 = damage, param4 = resisted, param1 = overkill?
                    entry->amount = static_cast<uint32_t>(wowEntry.payload.genericParams.param2);      // Actual melee damage
                    entry->overAmount = static_cast<uint32_t>(wowEntry.payload.genericParams.param1);  // Overkill (if any)
                    entry->resisted = static_cast<uint32_t>(wowEntry.payload.genericParams.param4);    // Resisted
                    entry->blocked = static_cast<uint32_t>(wowEntry.payload.genericParams.param5);     // Blocked  
                    entry->absorbed = static_cast<uint32_t>(wowEntry.payload.genericParams.param6);    // Absorbed
                    
                    // For melee, school is typically physical (0x01) but check param3
                    uint32_t damageSchool = static_cast<uint32_t>(wowEntry.payload.genericParams.param3);
                    if (damageSchool == 0) {
                        entry->spellSchoolMask = 0x01; // Physical
                        entry->spellSchool = SPELL_SCHOOL_NORMAL;
                    } else {
                        entry->spellSchoolMask = damageSchool;
                    }
                                 } else {
                     // For spell damage: Figure out parameter mapping and look for spell ID
                     LOG_INFO("ConvertWowEntry: SPELL DAMAGE EVENT - Parameter analysis:");
                     LOG_INFO("  Event Type: " + std::to_string(static_cast<int>(entry->eventType)));
                     
                     // Damage parameter mapping (based on successful melee analysis)
                     entry->amount = static_cast<uint32_t>(wowEntry.payload.genericParams.param2);      // Damage amount
                     entry->overAmount = static_cast<uint32_t>(wowEntry.payload.genericParams.param3);  // Overkill
                     entry->blocked = static_cast<uint32_t>(wowEntry.payload.genericParams.param5);     // Blocked
                     entry->absorbed = static_cast<uint32_t>(wowEntry.payload.genericParams.param6);    // Absorbed
                     
                     // For spell damage, check multiple params for school mask
                     uint32_t damageSchool = static_cast<uint32_t>(wowEntry.payload.genericParams.param4);
                     if (damageSchool > 0 && damageSchool <= 0x7F) { // Valid school mask range
                         entry->spellSchoolMask = damageSchool;
                         LOG_INFO("ConvertWowEntry: ✅ Using spell school from param4: 0x" + std::to_string(damageSchool) + " (" + GetSchoolMaskName(damageSchool) + ")");
                     } else {
                         damageSchool = static_cast<uint32_t>(wowEntry.payload.genericParams.param1);
                         if (damageSchool > 0 && damageSchool <= 0x7F) {
                             entry->spellSchoolMask = damageSchool;
                             LOG_INFO("ConvertWowEntry: ✅ Using spell school from param1: 0x" + std::to_string(damageSchool) + " (" + GetSchoolMaskName(damageSchool) + ")");
                         } else {
                             LOG_INFO("ConvertWowEntry: ⚠️ No valid school found in params, keeping existing: 0x" + std::to_string(entry->spellSchoolMask));
                         }
                     }
                     
                     // For spell damage, resisted amount might be in a different param than melee
                     // Let's see what param4 contains when it's not the school mask
                     if (entry->spellSchoolMask != static_cast<uint32_t>(wowEntry.payload.genericParams.param4)) {
                         entry->resisted = static_cast<uint32_t>(wowEntry.payload.genericParams.param4);
                         LOG_INFO("ConvertWowEntry: Using param4 as resisted amount: " + std::to_string(entry->resisted));
                     } else {
                         // If param4 is school, maybe resisted is in param1 or param7?
                         entry->resisted = static_cast<uint32_t>(wowEntry.payload.genericParams.param1);
                         LOG_INFO("ConvertWowEntry: Using param1 as resisted amount: " + std::to_string(entry->resisted));
                     }
                     
                     LOG_INFO("ConvertWowEntry: SPELL DAMAGE RESULT - amount: " + std::to_string(entry->amount) + 
                             ", overkill: " + std::to_string(entry->overAmount) + 
                             ", school: 0x" + std::to_string(entry->spellSchoolMask));
                 }
                
                // Update the legacy enum based on final school mask
                if (entry->spellSchoolMask & static_cast<uint32_t>(SpellSchoolMask::HOLY)) {
                    entry->spellSchool = SPELL_SCHOOL_HOLY;
                } else if (entry->spellSchoolMask & static_cast<uint32_t>(SpellSchoolMask::FIRE)) {
                    entry->spellSchool = SPELL_SCHOOL_FIRE;
                } else if (entry->spellSchoolMask & static_cast<uint32_t>(SpellSchoolMask::NATURE)) {
                    entry->spellSchool = SPELL_SCHOOL_NATURE;
                } else if (entry->spellSchoolMask & static_cast<uint32_t>(SpellSchoolMask::FROST)) {
                    entry->spellSchool = SPELL_SCHOOL_FROST;
                } else if (entry->spellSchoolMask & static_cast<uint32_t>(SpellSchoolMask::SHADOW)) {
                    entry->spellSchool = SPELL_SCHOOL_SHADOW;
                } else if (entry->spellSchoolMask & static_cast<uint32_t>(SpellSchoolMask::ARCANE)) {
                    entry->spellSchool = SPELL_SCHOOL_ARCANE;
                } else {
                    entry->spellSchool = SPELL_SCHOOL_NORMAL;
                }
                
                // Check for critical hit in param7 or combatFlags
                if (wowEntry.payload.genericParams.param7 != 0 || (wowEntry.combatFlags & 0x01)) {
                    entry->hitFlags = static_cast<HitFlags>(static_cast<uint32_t>(entry->hitFlags) | static_cast<uint32_t>(HitFlags::CRITICAL));
                }
                
                LOG_INFO("ConvertWowEntry: Damage data - amount: " + std::to_string(entry->amount) + 
                        ", overkill: " + std::to_string(entry->overAmount) + 
                        ", school: 0x" + std::to_string(entry->spellSchoolMask) + " (" + GetSchoolMaskName(entry->spellSchoolMask) + ")" +
                        ", resisted: " + std::to_string(entry->resisted) + 
                        ", blocked: " + std::to_string(entry->blocked) + 
                        ", absorbed: " + std::to_string(entry->absorbed) + 
                        ", critical: " + (static_cast<uint32_t>(entry->hitFlags) & static_cast<uint32_t>(HitFlags::CRITICAL) ? "true" : "false"));
            }
        } else {
            // For other events (casts, etc.) or healing events without 0x10 flag
            LOG_DEBUG("ConvertWowEntry: Event without 0x10 flag, checking alternative parameter sources");
            
            // For healing events, try different parameter positions
            if (entry->eventType == CombatEventType::SPELL_HEAL) {
                LOG_INFO("ConvertWowEntry: HEALING EVENT DEBUG (without 0x10 flag) - All parameters:");
                LOG_INFO("  param1: " + std::to_string(wowEntry.payload.genericParams.param1));
                LOG_INFO("  param2: " + std::to_string(wowEntry.payload.genericParams.param2));
                LOG_INFO("  param3: " + std::to_string(wowEntry.payload.genericParams.param3));
                LOG_INFO("  param4: " + std::to_string(wowEntry.payload.genericParams.param4));
                LOG_INFO("  param5: " + std::to_string(wowEntry.payload.genericParams.param5));
                LOG_INFO("  param6: " + std::to_string(wowEntry.payload.genericParams.param6));
                LOG_INFO("  param7: " + std::to_string(wowEntry.payload.genericParams.param7));
                LOG_INFO("  subEventFlags: 0x" + std::to_string(wowEntry.subEventFlags));
                
                // For healing events: param1 = total heal power, param2 = overheal amount
                // Base heal = param1 - param2
                uint32_t totalHealPower = static_cast<uint32_t>(wowEntry.payload.genericParams.param1);
                uint32_t overheal = static_cast<uint32_t>(wowEntry.payload.genericParams.param2);
                uint32_t baseHeal = totalHealPower - overheal;
                
                entry->amount = baseHeal;        // Effective/base healing
                entry->overAmount = overheal;    // Overheal amount
                
                LOG_INFO("ConvertWowEntry: Calculated heal - base: " + std::to_string(baseHeal) + 
                        ", overheal: " + std::to_string(overheal) + 
                        ", total: " + std::to_string(totalHealPower));
            } else {
                // For non-healing events without 0x10 flag
                entry->amount = static_cast<uint32_t>(wowEntry.payload.genericParams.param1);
                entry->overAmount = 0;
                entry->absorbed = 0;
                entry->resisted = 0;
                entry->blocked = 0;
                // Initialize hitFlags to NONE (no critical flag set)
                entry->hitFlags = HitFlags::NONE;
            }
        }
        
        LOG_DEBUG("ConvertWowEntry: Conversion completed successfully");
        
        // Validate the entry before returning
        if (entry->sourceName.empty()) {
            entry->sourceName = "Unknown";
        }
        if (entry->targetName.empty() && entry->targetGUID.ToUint64() != 0) {
            // Only set "Unknown" if we expect a target but don't have one
            entry->targetName = "Unknown";
        }
        
        return entry;
        
    } catch (const std::exception& e) {
        LOG_ERROR("Error converting WoW combat log entry: " + std::string(e.what()));
        return nullptr;
    }
}

std::string CombatLogManager::GetEventTypeName(int32_t eventTypeIndex) {
    try {
        LOG_DEBUG("GetEventTypeName: Looking up event type index " + std::to_string(eventTypeIndex));
        
        // Read from WoW's event name table
        const char** nameTable = reinterpret_cast<const char**>(WowCombatLogAddresses::g_CombatLogEventNameTable);
        LOG_DEBUG("GetEventTypeName: nameTable address = 0x" + 
                 std::to_string(reinterpret_cast<uintptr_t>(nameTable)));
        
        if (!nameTable) {
            LOG_WARNING("GetEventTypeName: nameTable is null");
            return "UNKNOWN";
        }
        
        if (!Memory::IsValidAddress(reinterpret_cast<uintptr_t>(nameTable))) {
            LOG_WARNING("GetEventTypeName: nameTable address is invalid");
            return "UNKNOWN";
        }
        
        // Bounds check (typical combat log has ~50 event types)
        if (eventTypeIndex < 0 || eventTypeIndex > 100) {
            LOG_WARNING("GetEventTypeName: eventTypeIndex " + std::to_string(eventTypeIndex) + " out of bounds");
            return "UNKNOWN";
        }
        
        const char* eventName = nameTable[eventTypeIndex];
        LOG_DEBUG("GetEventTypeName: eventName pointer = 0x" + 
                 std::to_string(reinterpret_cast<uintptr_t>(eventName)));
        
        if (!eventName) {
            LOG_WARNING("GetEventTypeName: eventName is null for index " + std::to_string(eventTypeIndex));
            return "UNKNOWN";
        }
        
        if (!Memory::IsValidAddress(reinterpret_cast<uintptr_t>(eventName))) {
            LOG_WARNING("GetEventTypeName: eventName address is invalid for index " + std::to_string(eventTypeIndex));
            return "UNKNOWN";
        }
        
        std::string result = Memory::ReadString(reinterpret_cast<uintptr_t>(eventName));
        LOG_DEBUG("GetEventTypeName: Successfully read event name: '" + result + "'");
        return result;
        
    } catch (const std::exception& e) {
        LOG_ERROR("Error reading event type name: " + std::string(e.what()));
        return "UNKNOWN";
    }
}

SpellInfo CombatLogManager::GetSpellInfoById(uint32_t spellId) {
    // Check cache first
    {
        std::lock_guard<std::mutex> lock(m_spellLookupMutex);
        auto it = m_spellCache.find(spellId);
        if (it != m_spellCache.end()) {
            LOG_DEBUG("GetSpellInfoById: Found cached spell ID " + std::to_string(spellId) + ": '" + it->second.name + "'");
            return it->second;
        }
    }
    
    // Create a promise/future pair for async lookup
    auto promise = std::make_shared<std::promise<SpellInfo>>();
    auto future = promise->get_future();
    
    // Queue the lookup request for EndScene execution
    {
        std::lock_guard<std::mutex> lock(m_spellLookupMutex);
        m_spellLookupQueue.push({spellId, promise});
    }
    
    LOG_DEBUG("GetSpellInfoById: Queued spell lookup for ID " + std::to_string(spellId));
    
    // Wait for the result with timeout
    auto status = future.wait_for(std::chrono::milliseconds(100));
    if (status == std::future_status::ready) {
        SpellInfo result = future.get();
        LOG_DEBUG("GetSpellInfoById: Got result for spell ID " + std::to_string(spellId) + ": '" + result.name + "'");
        return result;
    } else {
        LOG_WARNING("GetSpellInfoById: Timeout waiting for spell lookup " + std::to_string(spellId));
        return {"Spell " + std::to_string(spellId), 0, false};
    }
}

SpellInfo CombatLogManager::GetSpellInfoByIdInternal(uint32_t spellId) {
    LOG_DEBUG("GetSpellInfoByIdInternal: Looking up spell ID " + std::to_string(spellId));
    
    // Validate function address first
    if (!Memory::IsValidAddress(WowSpellAddresses::FETCH_LOCALIZED_ROW)) {
        LOG_WARNING("GetSpellInfoByIdInternal: FETCH_LOCALIZED_ROW address is invalid");
        return {"Invalid Function Address", 0, false};
    }
    
    // Define the buffer size based on the disassembly (0x2A8 = 680 bytes)
    const size_t spellRecordSize = 0x2A8;
    char resultBuffer[spellRecordSize] = {0};
    
    // Get the Spell DBC context object
    // Based on disassembly analysis, 0x00AD49D0 is the direct context structure
    if (!Memory::IsValidAddress(WowSpellAddresses::SPELL_DB_CONTEXT_POINTER)) {
        LOG_WARNING("GetSpellInfoByIdInternal: Spell DBC context address is invalid");
        return {"Invalid Context Address", 0, false};
    }
    
    void* pSpellContext = reinterpret_cast<void*>(WowSpellAddresses::SPELL_DB_CONTEXT_POINTER);
    
    LOG_DEBUG("GetSpellInfoByIdInternal: Spell context at 0x" + std::to_string(reinterpret_cast<uintptr_t>(pSpellContext)));
    
    // Debug: Check the bounds values in the context (based on disassembly analysis)
    uint32_t* contextPtr = reinterpret_cast<uint32_t*>(pSpellContext);
    if (Memory::IsValidAddress(reinterpret_cast<uintptr_t>(contextPtr + 3))) { // +0x0C = index 3
        uint32_t maxSpellId = contextPtr[3]; // context+0x0C
        LOG_DEBUG("GetSpellInfoByIdInternal: Max Spell ID from context+0x0C: " + std::to_string(maxSpellId));
    }
    if (Memory::IsValidAddress(reinterpret_cast<uintptr_t>(contextPtr + 4))) { // +0x10 = index 4
        uint32_t minSpellId = contextPtr[4]; // context+0x10
        LOG_DEBUG("GetSpellInfoByIdInternal: Min Spell ID from context+0x10: " + std::to_string(minSpellId));
    }
    
    // Call the function using inline assembly to ensure correct __thiscall convention
    LOG_DEBUG("GetSpellInfoByIdInternal: Calling fetchLocalizedRow with inline assembly");
    LOG_DEBUG("  - Context: 0x" + std::to_string(reinterpret_cast<uintptr_t>(pSpellContext)));
    LOG_DEBUG("  - SpellID: " + std::to_string(spellId));
    LOG_DEBUG("  - Buffer: 0x" + std::to_string(reinterpret_cast<uintptr_t>(resultBuffer)));
    
    int success = 0;
    
    __asm {
        // Prepare arguments for __thiscall
        lea eax, resultBuffer           // Get address of our buffer
        push eax                        // Arg2: pResultBuffer (pushed last)
        push spellId                    // Arg1: recordId (pushed first)
        mov ecx, pSpellContext          // Set 'this' pointer in ECX for __thiscall
        
        // Call the function at the fixed address
        mov eax, WowSpellAddresses::FETCH_LOCALIZED_ROW
        call eax
        
        // Store the return value (0 on failure, non-zero on success)
        mov success, eax
    }
    
    LOG_DEBUG("GetSpellInfoByIdInternal: fetchLocalizedRow returned " + std::to_string(success));
    
    if (!success) {
        LOG_DEBUG("GetSpellInfoByIdInternal: Record not found for spell ID " + std::to_string(spellId));
        return {"Record Not Found", 0, false};
    }
    
    SpellInfo outInfo;
    outInfo.success = true;
    
    // Based on the disassembly analysis:
    // - The spell name is stored as a pointer at offset +0x220 in the buffer
    // - From WoW's PushCombatLogToFrameScript at 0x74E42B: mov eax, [ebp+var_A4]
    // - var_A4 corresponds to buffer + 0x220
    const char* spellNamePtr = *reinterpret_cast<const char**>(resultBuffer + 0x220);
    
    LOG_DEBUG("GetSpellInfoByIdInternal: Spell name pointer at +0x220: 0x" + 
              std::to_string(reinterpret_cast<uintptr_t>(spellNamePtr)));
    
    if (spellNamePtr && Memory::IsValidAddress(reinterpret_cast<uintptr_t>(spellNamePtr))) {
        // Validate the string pointer and read the name
        if (*spellNamePtr) {
            outInfo.name = Memory::ReadString(reinterpret_cast<uintptr_t>(spellNamePtr));
            LOG_DEBUG("GetSpellInfoByIdInternal: Found spell name: '" + outInfo.name + "'");
        } else {
            outInfo.name = "Empty Spell Name";
            LOG_DEBUG("GetSpellInfoByIdInternal: Spell name pointer is empty");
        }
    } else {
        outInfo.name = "Invalid Spell Name Pointer";
        LOG_DEBUG("GetSpellInfoByIdInternal: Invalid spell name pointer");
    }
    
    // Read the school mask from offset 0x284 (based on disassembly)
    // From WoW's PushCombatLogToFrameScript at 0x74E438: mov ecx, [ebp+var_40]
    // var_40 corresponds to buffer + 0x284
    outInfo.schoolMask = *reinterpret_cast<uint32_t*>(resultBuffer + 0x284);
    LOG_DEBUG("GetSpellInfoByIdInternal: School mask at +0x284: 0x" + std::to_string(outInfo.schoolMask));
    
    return outInfo;
}

void CombatLogManager::ProcessSpellLookupQueue() {
    std::lock_guard<std::mutex> lock(m_spellLookupMutex);
    
    // Early exit if queue is empty to avoid spam
    if (m_spellLookupQueue.empty()) {
        return;
    }
    
    LOG_DEBUG("ProcessSpellLookupQueue: Processing " + std::to_string(m_spellLookupQueue.size()) + " spell lookup requests");
    
    // Process all queued spell lookups
    while (!m_spellLookupQueue.empty()) {
        auto request = m_spellLookupQueue.front();
        m_spellLookupQueue.pop();
        
        LOG_DEBUG("ProcessSpellLookupQueue: Processing spell ID " + std::to_string(request.spellId));
        
        try {
            // Perform the actual lookup (safe in EndScene)
            SpellInfo result = GetSpellInfoByIdInternal(request.spellId);
            
            // Cache successful lookups
            if (result.success) {
                m_spellCache[request.spellId] = result;
                LOG_DEBUG("ProcessSpellLookupQueue: Successfully cached spell " + std::to_string(request.spellId) + ": '" + result.name + "'");
            } else {
                LOG_DEBUG("ProcessSpellLookupQueue: Failed to lookup spell " + std::to_string(request.spellId) + ": " + result.name);
            }
            
            // Fulfill the promise
            request.promise->set_value(result);
            
        } catch (const std::exception& e) {
            LOG_ERROR("Exception processing spell lookup for ID " + std::to_string(request.spellId) + ": " + std::string(e.what()));
            request.promise->set_value({"Exception", 0, false});
        } catch (...) {
            LOG_ERROR("Unknown exception processing spell lookup for ID " + std::to_string(request.spellId));
            request.promise->set_value({"Unknown Exception", 0, false});
        }
    }
}

std::string CombatLogManager::GetSpellNameById(uint32_t spellId) {
    SpellInfo info = GetSpellInfoById(spellId);
    return info.success ? info.name : ("Spell " + std::to_string(spellId));
} 