#include "WowUnit.h"
#include "ObjectManager.h"
#include "../memory/memory.h"
#include "../types/types.h"
#include <sstream>
#include <iomanip>

// Unit field offsets (corrected for our Cryotherapist project)
namespace {
    constexpr uint32_t OBJECT_POS_Y = 0x798;  // Y coordinate is at lower address
    constexpr uint32_t OBJECT_POS_X = 0x79C;  // X coordinate is at higher address  
    constexpr uint32_t OBJECT_POS_Z = 0x7A0;
    constexpr uint32_t OBJECT_FACING = 0x7A8;
    constexpr uint32_t OBJECT_DESCRIPTOR_PTR = 0x8;
    
    // Basic unit fields that were already working
    constexpr uint32_t UNIT_FIELD_HEALTH = 0x18 * 4;      // 0x60
    constexpr uint32_t UNIT_FIELD_MAXHEALTH = 0x20 * 4;   // 0x80
    constexpr uint32_t UNIT_FIELD_LEVEL = 0x36 * 4;       // 0xD8
    constexpr uint32_t UNIT_FIELD_POWER_BASE = 0x19 * 4;  // 0x64
    constexpr uint32_t UNIT_FIELD_MAXPOWER_BASE = 0x21 * 4; // 0x84
    constexpr uint32_t UNIT_FIELD_POWER_TYPE = 0x47;      // Power type byte
    constexpr uint32_t UNIT_FIELD_TARGET = 0x48;          // Target GUID
    
    // Enhanced fields (EXACT SAME AS ROTATIONS PROJECT)
    constexpr uint32_t UNIT_FIELD_FLAGS = 0x3B * 4;       // Unit flags (0xEC) - FROM ROTATIONS
    constexpr uint32_t UNIT_FIELD_FLAGS_2 = 0x49 * 4;     // Unit flags 2 (0x124)
    constexpr uint32_t UNIT_DYNAMIC_FLAGS = 0x47 * 4;     // Dynamic flags (0x11C)
    constexpr uint32_t UNIT_FIELD_FACTIONTEMPLATE = 0x30 * 4; // Faction - FROM ROTATIONS
    
    // Casting and channeling (EXACT SAME AS ROTATIONS PROJECT)
    constexpr uint32_t OBJECT_FIELD_CASTING_SPELL = 0xA6C;    // Current casting spell - FROM ROTATIONS
    constexpr uint32_t OBJECT_FIELD_CHANNEL_SPELL = 0xA80;    // Current channel spell - FROM ROTATIONS
    
    // Movement flags (from object base)
    constexpr uint32_t OBJECT_MOVEMENT_FLAGS = 0x9C8;     // Movement flags
    
    // Threat information offsets
    constexpr uint32_t THREAT_MANAGER_OFFSET = 0xFE0;
    constexpr uint32_t THREAT_HIGHEST_OFFSET = 0xFD8;
    constexpr uint32_t THREAT_TOP_ENTRY_OFFSET = 0xFEC;
    constexpr uint32_t THREAT_ENTRY_GUID_OFFSET = 0x20;
    constexpr uint32_t THREAT_ENTRY_STATUS_OFFSET = 0x28;
    constexpr uint32_t THREAT_ENTRY_PERCENTAGE_OFFSET = 0x29;
    constexpr uint32_t THREAT_ENTRY_RAW_OFFSET = 0x2C;
}

// Constructor for backward compatibility
WowUnit::WowUnit(uintptr_t objectPtr) : WowObject(objectPtr) {
    InitializeCache();
}

// Constructor that takes GUID from enumeration
WowUnit::WowUnit(uintptr_t objectPtr, WGUID guid) : WowObject(objectPtr, guid) {
    InitializeCache();
}

void WowUnit::InitializeCache() {
    m_cachedHealth = 0;
    m_cachedMaxHealth = 0;
    m_cachedLevel = 0;
    m_cachedPowerType = 0;
    m_cachedUnitFlags = 0;
    m_cachedUnitFlags2 = 0;
    m_cachedDynamicFlags = 0;
    m_cachedMovementFlags = 0;
    m_cachedCastingSpellId = 0;
    m_cachedChannelSpellId = 0;
    m_cachedCastingEndTimeMs = 0;
    m_cachedChannelEndTimeMs = 0;
    m_cachedTargetGUID = WGUID();
    m_cachedFactionId = 0;
    m_cachedFacing = 0.0f;
    m_cachedScale = 1.0f;
    m_cachedIsInCombat = false;
    m_cachedPosition = Vector3();
    
    // Initialize power arrays
    for (int i = 0; i < 8; i++) {
        m_cachedPowers[i] = 0;
        m_cachedMaxPowers[i] = 0;
        m_hasPowerType[i] = false;
    }
    
    // Initialize threat data
    m_cachedHighestThreatTargetGUID = WGUID();
    m_cachedThreatManagerBasePtr = 0;
    m_cachedTopThreatEntryPtr = 0;
    m_cachedThreatTableEntries.clear();
}

Vector3 WowUnit::GetPosition() const {
    return m_cachedPosition;
}

void WowUnit::UpdateDynamicData() {
    if (!m_objectPtr) {
        WowObject::UpdateDynamicData();
        InitializeCache();
        return;
    }

    // Call base class update
    WowObject::UpdateDynamicData();

    try {
        // Read position from WoW's memory layout
        // Note: WoW stores coordinates in a specific order that requires swapping for our coordinate system
        float rawPosX = Memory::Read<float>(m_objectPtr + OBJECT_POS_X);  // 0x79C
        float rawPosY = Memory::Read<float>(m_objectPtr + OBJECT_POS_Y);  // 0x798
        float rawPosZ = Memory::Read<float>(m_objectPtr + OBJECT_POS_Z);  // 0x7A0
        
        // Apply coordinate mapping to match our coordinate system (swap X/Y from WoW's layout)
        m_cachedPosition.x = rawPosY;  // WoW's Y becomes our X
        m_cachedPosition.y = rawPosX;  // WoW's X becomes our Y  
        m_cachedPosition.z = rawPosZ;  // Z stays the same

        // Read descriptor pointer (this was already working)
        uintptr_t descriptorPtr = Memory::Read<uintptr_t>(m_objectPtr + OBJECT_DESCRIPTOR_PTR);
        if (descriptorPtr) {
            // Read basic unit fields (these were already working)
            m_cachedHealth = Memory::Read<uint32_t>(descriptorPtr + UNIT_FIELD_HEALTH);
            m_cachedMaxHealth = Memory::Read<uint32_t>(descriptorPtr + UNIT_FIELD_MAXHEALTH);
            m_cachedLevel = Memory::Read<uint32_t>(descriptorPtr + UNIT_FIELD_LEVEL);
            
            // Read target GUID (this was already working)
            uint64_t targetGuid64 = Memory::Read<uint64_t>(descriptorPtr + UNIT_FIELD_TARGET);
            m_cachedTargetGUID = WGUID(targetGuid64);
            
            // Read power type and powers (this was already working)
            m_cachedPowerType = Memory::Read<uint8_t>(descriptorPtr + UNIT_FIELD_POWER_TYPE);
            
            // Read all power types (this was already working)
            for (int i = 0; i < 8; i++) {
                try {
                    m_cachedPowers[i] = Memory::Read<uint32_t>(descriptorPtr + UNIT_FIELD_POWER_BASE + (i * 4));
                    m_cachedMaxPowers[i] = Memory::Read<uint32_t>(descriptorPtr + UNIT_FIELD_MAXPOWER_BASE + (i * 4));
                    m_hasPowerType[i] = (m_cachedMaxPowers[i] > 0);
                } catch (...) {
                    m_cachedPowers[i] = 0;
                    m_cachedMaxPowers[i] = 0;
                    m_hasPowerType[i] = false;
                }
            }
            
            // Read enhanced unit fields (fixed to use simpler approach)
            try {
                // Read unit flags from descriptor
                m_cachedUnitFlags = Memory::Read<uint32_t>(descriptorPtr + UNIT_FIELD_FLAGS);
                m_cachedUnitFlags2 = Memory::Read<uint32_t>(descriptorPtr + UNIT_FIELD_FLAGS_2);
                m_cachedDynamicFlags = Memory::Read<uint32_t>(descriptorPtr + UNIT_DYNAMIC_FLAGS);
                m_cachedFactionId = Memory::Read<uint32_t>(descriptorPtr + UNIT_FIELD_FACTIONTEMPLATE);
                
                // Determine combat status from flags
                m_cachedIsInCombat = (m_cachedUnitFlags & UNIT_FLAG_IN_COMBAT) != 0;
                
            } catch (...) {
                // If we can't read enhanced fields, set defaults
                m_cachedUnitFlags = 0;
                m_cachedUnitFlags2 = 0;
                m_cachedDynamicFlags = 0;
                m_cachedIsInCombat = false;
            }
            
            // Read facing (this was already working)
            m_cachedFacing = Memory::Read<float>(m_objectPtr + OBJECT_FACING);
        }

        // Read casting info from object base
        try {
            // Read casting spells from object base
            m_cachedCastingSpellId = Memory::Read<uint32_t>(m_objectPtr + OBJECT_FIELD_CASTING_SPELL);
            m_cachedChannelSpellId = Memory::Read<uint32_t>(m_objectPtr + OBJECT_FIELD_CHANNEL_SPELL);
        } catch (...) {
            // Clear on error
            m_cachedCastingSpellId = 0;
            m_cachedChannelSpellId = 0;
        }

        // Read movement flags (EXACT SAME AS ROTATIONS - only for local player)
        try {
            // Check if this is the local player (need to implement this check)
            bool isLocalPlayer = IsLocalPlayer(); // We need to implement this method
            
            if (isLocalPlayer) {
                uintptr_t movementComponentPtr = 0;
                // 1. Read POINTER to CMovement component
                try {
                    movementComponentPtr = Memory::Read<uintptr_t>(m_objectPtr + 0xD8); // UNIT_MOVEMENT_COMPONENT_PTR
                } catch (...) {
                    m_cachedMovementFlags = 0;
                    movementComponentPtr = 0;
                }
                
                if (movementComponentPtr != 0) {
                    // 2. Read flags from [movementComponentPtr + 0x44]
                    try {
                        m_cachedMovementFlags = Memory::Read<uint32_t>(movementComponentPtr + 0x44); // MOVEMENT_FLAGS
                    } catch (...) {
                        m_cachedMovementFlags = 0;
                    }
                } else {
                    m_cachedMovementFlags = 0;
                }
            } else {
                // Not local player - movement flags are always 0
                m_cachedMovementFlags = 0;
            }
        } catch (...) {
            m_cachedMovementFlags = 0;
        }

        // Read threat information (keep existing logic)
        try {
            m_cachedThreatManagerBasePtr = Memory::Read<uintptr_t>(m_objectPtr + THREAT_MANAGER_OFFSET);
            if (m_cachedThreatManagerBasePtr) {
                m_cachedHighestThreatTargetGUID = WGUID(Memory::Read<uint64_t>(m_objectPtr + THREAT_HIGHEST_OFFSET));
                m_cachedTopThreatEntryPtr = Memory::Read<uintptr_t>(m_objectPtr + THREAT_TOP_ENTRY_OFFSET);
                
                // Read threat table entries
                m_cachedThreatTableEntries.clear();
                if (m_cachedTopThreatEntryPtr) {
                    ThreatEntry entry;
                    entry.targetGUID = WGUID(Memory::Read<uint64_t>(m_cachedTopThreatEntryPtr + THREAT_ENTRY_GUID_OFFSET));
                    entry.status = Memory::Read<uint8_t>(m_cachedTopThreatEntryPtr + THREAT_ENTRY_STATUS_OFFSET);
                    entry.percentage = Memory::Read<uint8_t>(m_cachedTopThreatEntryPtr + THREAT_ENTRY_PERCENTAGE_OFFSET);
                    entry.rawValue = Memory::Read<uint32_t>(m_cachedTopThreatEntryPtr + THREAT_ENTRY_RAW_OFFSET);
                    
                    if (entry.targetGUID.ToUint64() != 0) {
                        m_cachedThreatTableEntries.push_back(entry);
                    }
                }
            }
        } catch (...) {
            // Clear threat data on error
            m_cachedHighestThreatTargetGUID = WGUID();
            m_cachedThreatManagerBasePtr = 0;
            m_cachedTopThreatEntryPtr = 0;
            m_cachedThreatTableEntries.clear();
        }

    } catch (const std::exception&) {
        // Handle memory read errors silently
        m_cachedPosition = Vector3();
    }
}

// Basic unit methods (existing)
uint32_t WowUnit::GetHealth() const {
    return m_cachedHealth;
}

uint32_t WowUnit::GetMaxHealth() const {
    return m_cachedMaxHealth;
}

uint32_t WowUnit::GetMana() const {
    return GetPowerByType(0); // Mana is power type 0
}

uint32_t WowUnit::GetMaxMana() const {
    return GetMaxPowerByType(0); // Mana is power type 0
}

uint32_t WowUnit::GetLevel() const {
    return m_cachedLevel;
}

bool WowUnit::IsAlive() const {
    return m_cachedHealth > 0;
}

bool WowUnit::IsInCombat() const {
    return m_cachedIsInCombat;
}

bool WowUnit::IsDead() const {
    return m_cachedHealth == 0;
}

float WowUnit::GetFacing() const {
    return m_cachedFacing;
}

WGUID WowUnit::GetTargetGUID() const {
    return m_cachedTargetGUID;
}

// Enhanced status methods (simplified and corrected)
bool WowUnit::IsCasting() const {
    return m_cachedCastingSpellId != 0;
}

bool WowUnit::IsChanneling() const {
    return m_cachedChannelSpellId != 0;
}

bool WowUnit::IsMoving() const {
    // EXACT SAME AS ROTATIONS - Check comprehensive movement flags
    const uint32_t ACTIVE_LOCOMOTION_MASK =
        0x00000001 | // MOVEMENTFLAG_FORWARD
        0x00000002 | // MOVEMENTFLAG_BACKWARD
        0x00000004 | // MOVEMENTFLAG_STRAFE_LEFT
        0x00000008 | // MOVEMENTFLAG_STRAFE_RIGHT
        0x00001000 | // MOVEMENTFLAG_FALLING
        0x00002000 | // MOVEMENTFLAG_FALLING_FAR
        0x00200000 | // MOVEMENTFLAG_SWIMMING
        0x00400000 | // MOVEMENTFLAG_ASCENDING
        0x00800000 | // MOVEMENTFLAG_DESCENDING
        0x02000000 | // MOVEMENTFLAG_FLYING
        0x04000000 | // MOVEMENTFLAG_SPLINE_ELEVATION
        0x08000000 | // MOVEMENTFLAG_SPLINE_ENABLED
        0x40000000;  // MOVEMENTFLAG_HOVER
        
    return (m_cachedMovementFlags & ACTIVE_LOCOMOTION_MASK) != 0;
}

bool WowUnit::IsLocalPlayer() const {
    // Check if this unit's GUID matches the local player GUID
    auto objectManager = ObjectManager::GetInstance();
    if (!objectManager) return false;
    
    WGUID localPlayerGuid = objectManager->GetLocalPlayerGuid();
    if (!localPlayerGuid.IsValid()) return false;
    
    // Compare GUIDs
    WGUID thisGuid = this->GetGUID();
    return thisGuid.IsValid() && (localPlayerGuid.ToUint64() == thisGuid.ToUint64());
}

// Power type methods (existing)
bool WowUnit::HasPowerType(uint8_t powerType) const {
    if (powerType >= 8) return false;
    return m_hasPowerType[powerType];
}

int WowUnit::GetPowerByType(uint8_t powerType) const {
    if (powerType >= 8) return 0;
    return m_cachedPowers[powerType];
}

int WowUnit::GetMaxPowerByType(uint8_t powerType) const {
    if (powerType >= 8) return 0;
    return m_cachedMaxPowers[powerType];
}

std::string WowUnit::GetPowerTypeString(uint8_t powerType) const {
    switch (powerType) {
        case 0: return "Mana";
        case 1: return "Rage";
        case 2: return "Focus";
        case 3: return "Energy";
        case 4: return "Happiness";
        case 5: return "Health";
        case 6: return "Rune";
        case 7: return "Runic Power";
        default: return "Unknown";
    }
}

// Threat information accessors (existing)
const std::vector<ThreatEntry>& WowUnit::GetThreatTableEntries() const {
    return m_cachedThreatTableEntries;
}

WGUID WowUnit::GetHighestThreatTargetGUID() const {
    return m_cachedHighestThreatTargetGUID;
}

// Additional methods needed for healing
float WowUnit::GetHealthPercent() const {
    if (m_cachedMaxHealth == 0) {
        return 0.0f;
    }
    return (static_cast<float>(m_cachedHealth) / static_cast<float>(m_cachedMaxHealth)) * 100.0f;
}

bool WowUnit::IsHostile() const {
    // Simplified implementation - opposite of friendly
    return !IsFriendly();
}

bool WowUnit::IsFriendly() const {
    // Simplified implementation for now
    // In a full implementation, this would check faction relationships
    // For now, assume non-players are hostile unless proven otherwise
    return false; // Default to hostile for enemy targeting
} 