#pragma once

#include "WowUnit.h"

class WowPlayer : public WowUnit {
public:
    // Original constructor (for backward compatibility)
    WowPlayer(uintptr_t objectPtr);
    
    // New constructor that takes GUID from enumeration
    WowPlayer(uintptr_t objectPtr, WGUID guid);
    
    virtual ~WowPlayer() = default;

    // Player-specific methods (placeholder)
    uint32_t GetLevel() const;
    bool IsLocalPlayer() const;
    
    // Enhanced player-specific methods (added to match Rotations)
    bool IsLooting() const;
}; 