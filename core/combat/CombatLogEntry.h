#pragma once

#include "../types/types.h"
#include <string>
#include <vector>
#include <chrono>
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <cstdint>

// Use existing types from types.h to avoid redefinition
using CombatSpellSchool = SpellSchool;
using CombatPowerType = PowerType;

// Spell School Mask definitions (from assembly analysis)
// These are bitmask values, not sequential enum values
enum class SpellSchoolMask : uint32_t {
    PHYSICAL = 0x01,    // 1
    HOLY     = 0x02,    // 2  
    FIRE     = 0x04,    // 4
    NATURE   = 0x08,    // 8
    FROST    = 0x10,    // 16
    SHADOW   = 0x20,    // 32
    ARCANE   = 0x40     // 64
};

// Helper function to convert from SpellSchool enum to SpellSchoolMask
inline SpellSchoolMask ConvertSchoolToMask(SpellSchool school) {
    switch (school) {
        case SPELL_SCHOOL_NORMAL: return SpellSchoolMask::PHYSICAL;
        case SPELL_SCHOOL_HOLY:   return SpellSchoolMask::HOLY;
        case SPELL_SCHOOL_FIRE:   return SpellSchoolMask::FIRE;
        case SPELL_SCHOOL_NATURE: return SpellSchoolMask::NATURE;
        case SPELL_SCHOOL_FROST:  return SpellSchoolMask::FROST;
        case SPELL_SCHOOL_SHADOW: return SpellSchoolMask::SHADOW;
        case SPELL_SCHOOL_ARCANE: return SpellSchoolMask::ARCANE;
        default: return SpellSchoolMask::PHYSICAL;
    }
}

// Helper function to get school name from mask
inline std::string GetSchoolMaskName(uint32_t schoolMask) {
    std::vector<std::string> schools;
    
    if (schoolMask & static_cast<uint32_t>(SpellSchoolMask::PHYSICAL)) schools.push_back("Physical");
    if (schoolMask & static_cast<uint32_t>(SpellSchoolMask::HOLY)) schools.push_back("Holy");
    if (schoolMask & static_cast<uint32_t>(SpellSchoolMask::FIRE)) schools.push_back("Fire");
    if (schoolMask & static_cast<uint32_t>(SpellSchoolMask::NATURE)) schools.push_back("Nature");
    if (schoolMask & static_cast<uint32_t>(SpellSchoolMask::FROST)) schools.push_back("Frost");
    if (schoolMask & static_cast<uint32_t>(SpellSchoolMask::SHADOW)) schools.push_back("Shadow");
    if (schoolMask & static_cast<uint32_t>(SpellSchoolMask::ARCANE)) schools.push_back("Arcane");
    
    if (schools.empty()) return "Unknown";
    if (schools.size() == 1) return schools[0];
    
    std::string result = schools[0];
    for (size_t i = 1; i < schools.size(); ++i) {
        result += "/" + schools[i];
    }
    return result;
}

// Damage effect types from WoW 3.3.5a analysis
enum class DamageEffectType {
    DIRECT_DAMAGE = 0,           // used for normal weapon damage (not for class abilities or spells)
    SPELL_DIRECT_DAMAGE = 1,     // spell/class abilities damage
    DOT = 2,                     // damage over time
    HEAL = 3,                    // healing
    NODAMAGE = 4,                // used also in case when damage applied to health but not applied to spell channelInterruptFlags/etc
    SELF_DAMAGE = 5              // self-inflicted damage
};

// Unit type mask from WoW analysis
enum UnitTypeMask {
    UNIT_MASK_NONE                  = 0x00000000,
    UNIT_MASK_SUMMON                = 0x00000001,
    UNIT_MASK_MINION                = 0x00000002,
    UNIT_MASK_GUARDIAN              = 0x00000004,
    UNIT_MASK_TOTEM                 = 0x00000008,
    UNIT_MASK_PET                   = 0x00000010,
    UNIT_MASK_VEHICLE               = 0x00000020,
    UNIT_MASK_PUPPET                = 0x00000040,
    UNIT_MASK_HUNTER_PET            = 0x00000080,
    UNIT_MASK_CONTROLABLE_GUARDIAN  = 0x00000100,
    UNIT_MASK_ACCESSORY             = 0x00000200
};

// Melee hit outcome from WoW analysis
enum class MeleeHitOutcome : uint8_t {
    MELEE_HIT_EVADE, MELEE_HIT_MISS, MELEE_HIT_DODGE, MELEE_HIT_BLOCK, MELEE_HIT_PARRY,
    MELEE_HIT_GLANCING, MELEE_HIT_CRIT, MELEE_HIT_CRUSHING, MELEE_HIT_NORMAL
};

// WoW 3.3.5a Combat Log Memory Structure (from reverse engineering analysis)
// Size: 120 bytes (0x78)
struct WowCombatLogEntry {
    WowCombatLogEntry* pNext;       // +0x00: Pointer to the next entry in the linked list
    WowCombatLogEntry* pPrev;       // +0x04: Pointer to the previous entry in the linked list

    uint32_t timestamp;             // +0x08: Timestamp of the event (from GetTickCount())
    int32_t eventTypeIndex;         // +0x0C: Index into g_CombatLogEventNameTable for the event string

    uint32_t unknown_10;            // +0x10: Often 0, related to entry state
    uint32_t unknown_14;            // +0x14: Often 0, related to entry state

    uint64_t srcGuid;               // +0x18: Source unit's GUID
    const char* srcName;            // +0x20: Pointer to the source unit's name string
    const char* srcRealm;           // +0x24: Pointer to the source unit's realm name string
    uint32_t srcFlags;              // +0x28: Bitmask of flags (player, npc, friendly, hostile, etc.)
    uint32_t srcRaidFlags;          // +0x2C: Raid target icon flags for the source unit

    uint64_t destGuid;              // +0x30: Destination unit's GUID
    const char* destName;           // +0x38: Pointer to the destination unit's name string
    const char* destRealm;          // +0x3C: Pointer to the destination unit's realm name string
    uint32_t destFlags;             // +0x40: Bitmask of flags for the destination unit

    uint32_t spellId;               // +0x44: The primary spell ID for the event (CORRECTED!)
    const char* spellName;          // +0x48: Pointer to the spell's name string  
    uint32_t spellSchool;           // +0x4C: The school of magic for the spell (CORRECTED!)
    uint32_t unknown_50;            // +0x50: Unused padding or unknown field

    uint32_t subEventFlags;         // +0x54: Bitmask indicating which optional params are valid (was payloadFlags)

    // --- Payload Flags & Data ---
    union {
        // Generic payload access (for backwards compatibility)
        struct {
            intptr_t param1;                // +0x58: Generic parameter 1
            intptr_t param2;                // +0x5C: Amount (Damage or Heal) when subEventFlags & 0x10
            intptr_t param3;                // +0x60: Overkill/Overheal when subEventFlags & 0x10
            intptr_t param4;                // +0x64: School mask or resisted amount when subEventFlags & 0x10
            intptr_t param5;                // +0x68: Blocked amount when subEventFlags & 0x10
            intptr_t param6;                // +0x6C: Absorbed amount when subEventFlags & 0x10
            intptr_t param7;                // +0x70: Critical/glancing/crushing flags when subEventFlags & 0x10
            intptr_t param8;                // +0x74: Additional parameter
            intptr_t param9;                // +0x78: Additional parameter
        } genericParams;

        // Generic payload access
        uint32_t args[9];

        // For subEventFlags & 0x1 (e.g., SWING_MISSED)
        struct {
            uint32_t missReason; // e.g., 1 for MISS, 2 for DODGE
        } SwingMissInfo;

        // For subEventFlags & 0x2 (e.g., RANGE_MISSED)
        struct {
            char* missReasonStr; // "Miss", "Dodge", etc.
        } RangeMissInfo;

        // For subEventFlags & 0x4 (e.g., SPELL_AURA_APPLIED)
        struct {
            uint32_t auraTypeEnum; // 0 for BUFF, 1 for DEBUFF
        } AuraTypeInfo;
        
        // For subEventFlags & 0x8 (e.g., UNIT_DIED)
        struct {
            uint32_t deathSourceEnum; // e.g., 0=FATIGUE
        } UnitDiedInfo;

        // For subEventFlags & 0x10 (e.g., SWING_DAMAGE, SPELL_DAMAGE)
        struct {
            int32_t  amount;         // 0x58
            int32_t  overkill;       // 0x5C
            uint32_t schoolMask;     // 0x60
            int32_t  resisted;       // 0x64
            int32_t  blocked;        // 0x68
            int32_t  absorbed;       // 0x6C
            uint32_t boolFlags;      // 0x70: bit0=critical, bit1=glancing, bit2=crushing
        } DamageInfo;

        // For subEventFlags & 0x20 (e.g., SPELL_PERIODIC_DAMAGE)
        struct {
            int32_t  amount;         // 0x58
            int32_t  overkill;       // 0x5C
            uint32_t schoolMask;     // 0x60
            uint8_t  isCritical;     // 0x64
        } PeriodicDamageInfo;
        
        // For subEventFlags & 0x40 (e.g., SPELL_ENERGIZE)
        struct {
            int32_t amount;          // 0x58
            uint32_t powerType;      // 0x5C (e.g., 0 for mana, 1 for rage)
        } PowerGainInfo;

        // For subEventFlags & 0x80 (e.g., SPELL_DRAIN)
        struct {
            int32_t  amount;         // 0x58
            uint32_t powerType;      // 0x5C
            int32_t  extraAmount;    // 0x60 (e.g., mana gained from draining life)
        } PowerDrainInfo;
        
        // For subEventFlags & 0x100 (e.g., SPELL_DISPEL)
        struct {
            uint32_t extraSpellId;   // 0x58: The spell that was dispelled.
            char*    extraSpellName; // 0x5C: Name of the dispelled spell.
            uint32_t extraSpellSchool; // 0x60
        } DispelInfo;

        // For subEventFlags & 0x200 (e.g., SPELL_STOLEN)
        struct {
            uint32_t extraSpellId;    // 0x58
            char*    extraSpellName;  // 0x5C
        } StolenSpellInfo;
        
        // For subEventFlags & 0x400 (e.g., SPELL_AURA_APPLIED)
        struct {
            uint8_t auraType; // 0 for BUFF, 1 for DEBUFF
        } AuraAppliedInfo;
        
        // For subEventFlags & 0x800 (e.g., SPELL_AURA_REMOVED)
        struct {
            uint32_t amount; // For stack removal, this is the remaining stacks
        } AuraRemovedInfo;

        // For subEventFlags & 0x1000 (e.g., ENCHANT_APPLIED)
        struct {
            char*    enchantName;  // 0x58
        } EnchantInfo;
        
    } payload; // 0x58

    uint32_t combatFlags;           // +0x7C: Additional combat-related flags
};

// WoW Combat Log Global Memory Addresses (3.3.5a)
namespace WowCombatLogAddresses {
    // Manager-based approach (like Python)
    constexpr uintptr_t g_CombatLogListManager = 0xADB980;       // Base manager address
    constexpr uintptr_t g_CombatLogListHeadOffset = 0x0;         // Head pointer offset from manager
    constexpr uintptr_t g_CombatLogListTailOffset = 0x4;         // Tail pointer offset from manager
    
    // Legacy direct addresses for comparison
    constexpr uintptr_t g_CombatLogEntryListHead = 0xADB980;     // Primary entry point
    constexpr uintptr_t g_CombatLogEntryListTail = 0xADB97C;     // Tail of main list
    
    // Other addresses
    constexpr uintptr_t g_CombatLogLuaIteratorCurrent = 0xCA1390; // Current Lua iterator
    constexpr uintptr_t g_CombatLogLuaIteratorNext = 0xCA1394;    // Next Lua iterator
    constexpr uintptr_t g_CombatLogFilterListHead = 0xADB968;     // Filter settings
    constexpr uintptr_t g_CombatLogEventNameTable = 0xADB758;    // Event name strings
    constexpr uintptr_t g_CombatLogMissTypeNameTable = 0xAC8050; // Miss type strings
    constexpr uintptr_t g_CombatLogFreeListHead = 0xADB988;      // Free list for allocation
    constexpr uintptr_t g_CombatLogGarbageListHead = 0xADB974;   // Garbage collection list
    
    // Node structure offsets
    constexpr uintptr_t g_CombatLogEventNextOffset = 0x0;        // pNext offset in node
    constexpr uintptr_t g_CombatLogEventPrevOffset = 0x4;        // pPrev offset in node
}

// Combat log event types based on the analysis
enum class CombatEventType {
    UNKNOWN = 0,
    SPELL_DAMAGE,
    SPELL_HEAL,
    SPELL_MISS,
    SPELL_CAST_START,
    SPELL_CAST_SUCCESS,
    SPELL_CAST_FAILED,
    SPELL_AURA_APPLIED,
    SPELL_AURA_REMOVED,
    SPELL_ENERGIZE,
    SPELL_DRAIN,
    SPELL_INTERRUPT,
    SPELL_INSTAKILL,
    MELEE_DAMAGE,
    MELEE_MISS,
    ENVIRONMENTAL_DAMAGE,
    PARTY_KILL,
    HONOR_GAIN,
    EXPERIENCE_GAIN,
    REPUTATION_GAIN,
    PROC_RESIST,
    DISPEL_FAILED,
    ENCHANTMENT_LOG,
    BUILDING_DAMAGE
};

// Hit flags from the analysis
enum class HitFlags : uint32_t {
    NONE = 0x00000000,
    CRITICAL = 0x00000200,
    GLANCING = 0x00010000,
    CRUSHING = 0x00020000,
    ABSORB = 0x00000020,
    BLOCK = 0x00002000,
    RESIST = 0x00000080,
    MISS = 0x00000001,
    DODGE = 0x00000002,
    PARRY = 0x00000004,
    DEFLECT = 0x00000008,
    IMMUNE = 0x00000010
};

// Use existing enums from types.h - no need to redefine

// Base combat log entry structure
struct CombatLogEntry {
    std::chrono::steady_clock::time_point timestamp;
    uint64_t serverTimestamp;  // Server-provided timestamp
    CombatEventType eventType;
    
    // Source information
    WGUID sourceGUID;
    std::string sourceName;
    uint32_t sourceFlags;
    
    // Target information  
    WGUID targetGUID;
    std::string targetName;
    uint32_t targetFlags;
    
    // Spell information (if applicable)
    uint32_t spellId;
    std::string spellName;
    CombatSpellSchool spellSchool;      // Legacy enum for compatibility
    uint32_t spellSchoolMask;           // Raw bitmask from WoW memory (+0x50)
    
    // Damage/Heal information
    uint32_t amount;
    uint32_t overAmount;  // Overkill/Overheal
    uint32_t absorbed;
    uint32_t resisted;
    uint32_t blocked;
    
    // Hit information
    HitFlags hitFlags;
    
    // Damage type information
    DamageEffectType damageType;
    MeleeHitOutcome meleeOutcome;
    
    // Additional data (varies by event type)
    std::unordered_map<std::string, std::string> additionalData;
    
    CombatLogEntry() : 
        timestamp(std::chrono::steady_clock::now()),
        serverTimestamp(0),
        eventType(CombatEventType::UNKNOWN),
        sourceFlags(0),
        targetFlags(0),
        spellId(0),
        spellSchool(SPELL_SCHOOL_NORMAL),
        spellSchoolMask(0),
        amount(0),
        overAmount(0),
        absorbed(0),
        resisted(0),
        blocked(0),
        hitFlags(HitFlags::NONE),
        damageType(DamageEffectType::DIRECT_DAMAGE),
        meleeOutcome(MeleeHitOutcome::MELEE_HIT_NORMAL) {}
};

// Specialized entry types for different events
struct DamageLogEntry : public CombatLogEntry {
    bool isMelee;
    uint32_t mitigated;  // Total damage mitigated (blocked + absorbed + resisted)
    
    DamageLogEntry() : isMelee(false), mitigated(0) {
        eventType = CombatEventType::SPELL_DAMAGE;
    }
};

struct HealLogEntry : public CombatLogEntry {
    bool isCritical;
    
    HealLogEntry() : isCritical(false) {
        eventType = CombatEventType::SPELL_HEAL;
    }
};

struct AuraLogEntry : public CombatLogEntry {
    bool isApplied;  // true for applied, false for removed
    uint32_t auraType;
    uint32_t stackCount;
    
    AuraLogEntry() : isApplied(true), auraType(0), stackCount(1) {}
};

struct ExperienceLogEntry : public CombatLogEntry {
    bool isQuest;
    bool isGroupBonus;
    uint32_t baseAmount;
    uint32_t bonusAmount;
    
    ExperienceLogEntry() : isQuest(false), isGroupBonus(false), baseAmount(0), bonusAmount(0) {
        eventType = CombatEventType::EXPERIENCE_GAIN;
    }
};

struct HonorLogEntry : public CombatLogEntry {
    uint32_t pvpRank;
    bool hadRankChange;
    
    HonorLogEntry() : pvpRank(0), hadRankChange(false) {
        eventType = CombatEventType::HONOR_GAIN;
    }
};

// Combat statistics structures for analysis
struct DamageStatistics {
    uint64_t totalDamage = 0;
    uint64_t totalHits = 0;
    uint64_t totalCrits = 0;
    uint64_t totalMisses = 0;
    uint64_t totalDodges = 0;
    uint64_t totalParries = 0;
    uint64_t totalBlocks = 0;
    uint64_t totalAbsorbed = 0;
    uint64_t totalResisted = 0;
    uint64_t totalOverkill = 0;
    
    double averageDamage = 0.0;
    double critRate = 0.0;
    double missRate = 0.0;
    double dps = 0.0;
    
    std::unordered_map<uint32_t, uint64_t> damageBySpell;
    std::unordered_map<CombatSpellSchool, uint64_t> damageBySchool;
};

struct HealingStatistics {
    uint64_t totalHealing = 0;
    uint64_t totalHits = 0;
    uint64_t totalCrits = 0;
    uint64_t totalOverheal = 0;
    
    double averageHeal = 0.0;
    double critRate = 0.0;
    double hps = 0.0;
    double overhealPercent = 0.0;
    
    std::unordered_map<uint32_t, uint64_t> healingBySpell;
};

struct CombatSession {
    std::chrono::steady_clock::time_point startTime;
    std::chrono::steady_clock::time_point endTime;
    bool isActive = false;
    
    std::vector<std::shared_ptr<CombatLogEntry>> entries;
    std::unordered_map<WGUID, DamageStatistics, WGUIDHash> damageStats;
    std::unordered_map<WGUID, HealingStatistics, WGUIDHash> healingStats;
    
    // Quick access maps
    std::unordered_map<WGUID, std::string, WGUIDHash> participantNames;
    std::unordered_set<WGUID, WGUIDHash> friendlyUnits;
    std::unordered_set<WGUID, WGUIDHash> hostileUnits;
    
    double GetDurationSeconds() const {
        if (!isActive && endTime > startTime) {
            return std::chrono::duration<double>(endTime - startTime).count();
        }
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - startTime).count();
    }
};

// Filter criteria for combat log analysis
struct CombatLogFilter {
    bool enabled = true;
    
    // Time filters
    std::chrono::steady_clock::time_point startTime;
    std::chrono::steady_clock::time_point endTime;
    bool useTimeFilter = false;
    
    // Event type filters
    std::unordered_set<CombatEventType> allowedEventTypes;
    
    // Entity filters
    std::unordered_set<WGUID, WGUIDHash> allowedSources;
    std::unordered_set<WGUID, WGUIDHash> allowedTargets;
    std::vector<std::string> sourceNameFilters;
    std::vector<std::string> targetNameFilters;
    
    // Spell filters
    std::unordered_set<uint32_t> allowedSpells;
    std::unordered_set<SpellSchool> allowedSchools;
    
    // Amount filters
    uint32_t minAmount = 0;
    uint32_t maxAmount = UINT32_MAX;
    
    // Hit type filters
    bool showCrits = true;
    bool showNormalHits = true;
    bool showMisses = true;
    bool showResists = true;
    
    CombatLogFilter() {
        // Default to showing damage and healing events
        allowedEventTypes.insert(CombatEventType::SPELL_DAMAGE);
        allowedEventTypes.insert(CombatEventType::SPELL_HEAL);
        allowedEventTypes.insert(CombatEventType::MELEE_DAMAGE);
    }
}; 