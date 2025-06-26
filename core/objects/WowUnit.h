#pragma once

#include "WowObject.h"
#include <vector>
#include <string>

// Threat entry structure (from Rotations project)
struct ThreatEntry {
    WGUID targetGUID;        // GUID of the unit this threat is against
    uint8_t status;          // Threat status (e.g., tanking, high, low)
    uint8_t percentage;      // Threat percentage
    uint32_t rawValue;       // Raw numerical threat value
    std::string targetName;  // Cached name of the target

    ThreatEntry() : status(0), percentage(0), rawValue(0) {}
};

class WowUnit : public WowObject {
protected:
    // Cached unit data (like Rotations project)
    int m_cachedHealth = 0;
    int m_cachedMaxHealth = 0;
    int m_cachedLevel = 0;
    uint8_t m_cachedPowerType = 0; // Primary power type
    int m_cachedPowers[8] = {0}; // Power values for all types
    int m_cachedMaxPowers[8] = {0}; // Max power values for all types
    bool m_hasPowerType[8] = {false}; // Tracks which power types are active
    
    // Enhanced unit flags and status (added to match Rotations)
    uint32_t m_cachedUnitFlags = 0;
    uint32_t m_cachedUnitFlags2 = 0;
    uint32_t m_cachedDynamicFlags = 0;
    uint32_t m_cachedMovementFlags = 0;
    
    // Casting and channeling info (added to match Rotations)
    uint32_t m_cachedCastingSpellId = 0;
    uint32_t m_cachedChannelSpellId = 0;
    uint32_t m_cachedCastingEndTimeMs = 0;
    uint32_t m_cachedChannelEndTimeMs = 0;
    
    WGUID m_cachedTargetGUID;
    uint32_t m_cachedFactionId = 0;
    float m_cachedFacing = 0.0f;
    float m_cachedScale = 1.0f;
    bool m_cachedIsInCombat = false;
    
    // Cached position (updated by UpdateDynamicData)
    Vector3 m_cachedPosition;

    // Cached threat data
    WGUID m_cachedHighestThreatTargetGUID;
    uintptr_t m_cachedThreatManagerBasePtr = 0;
    uintptr_t m_cachedTopThreatEntryPtr = 0;
    std::vector<ThreatEntry> m_cachedThreatTableEntries;

    // Helper to initialize cached data
    void InitializeCache();

public:
    // Unit flag constants (from Rotations project)
    static const uint32_t UNIT_FLAG_IN_COMBAT = 0x00080000;
    static const uint32_t UNIT_FLAG_FLEEING = 0x00800000;

    // Original constructor (for backward compatibility)
    WowUnit(uintptr_t objectPtr);
    
    // New constructor that takes GUID from enumeration
    WowUnit(uintptr_t objectPtr, WGUID guid);
    
    virtual ~WowUnit() = default;

    // Override position reading for units (like Rotations project)
    virtual Vector3 GetPosition() const override;
    
    // Override UpdateDynamicData to read unit-specific fields
    virtual void UpdateDynamicData();

    // Basic unit methods
    uint32_t GetHealth() const;
    uint32_t GetMaxHealth() const;
    uint32_t GetMana() const;
    uint32_t GetMaxMana() const;
    uint32_t GetLevel() const;
    bool IsAlive() const;
    bool IsInCombat() const;
    bool IsDead() const;
    float GetFacing() const;
    WGUID GetTargetGUID() const;
    
    // Additional methods needed for healing
    float GetHealthPercent() const;
    bool IsHostile() const;
    bool IsFriendly() const;
    
    // Enhanced unit flags and status (added to match Rotations)
    uint32_t GetUnitFlags() const { return m_cachedUnitFlags; }
    uint32_t GetUnitFlags2() const { return m_cachedUnitFlags2; }
    uint32_t GetDynamicFlags() const { return m_cachedDynamicFlags; }
    uint32_t GetMovementFlags() const { return m_cachedMovementFlags; }
    
    // Enhanced status methods (added to match Rotations)
    bool HasFlag(uint32_t flag) const { return (m_cachedUnitFlags & flag) != 0; }
    bool IsFleeing() const { return HasFlag(UNIT_FLAG_FLEEING); }
    bool IsCasting() const;
    bool IsChanneling() const;
    bool IsMoving() const;
    bool IsLocalPlayer() const;
    
    // Height and scale methods for head position calculation
    // Note: Based on vtable info provided:
    // CGUnit vtable: 0-83, GetScale is at index 15
    // Height may need different approach than vtable call
    float GetHeight() const;
    float GetScale() const;
    float GetVisualHeight() const; // baseHeight * scale
    Vector3 GetHeadPosition() const; // position + visual height
    
    // Casting/channeling info (added to match Rotations)
    uint32_t GetCastingSpellId() const { return m_cachedCastingSpellId; }
    uint32_t GetChannelSpellId() const { return m_cachedChannelSpellId; }
    
    // Power type methods (like Rotations project)
    bool HasPowerType(uint8_t powerType) const;
    int GetPowerByType(uint8_t powerType) const;
    int GetMaxPowerByType(uint8_t powerType) const;
    std::string GetPowerTypeString(uint8_t powerType) const;
    
    // Threat information accessors
    const std::vector<ThreatEntry>& GetThreatTableEntries() const;
    WGUID GetHighestThreatTargetGUID() const;
}; 