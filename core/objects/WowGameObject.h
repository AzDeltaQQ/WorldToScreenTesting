#pragma once

#include "WowObject.h"

class WowGameObject : public WowObject {
public:
    // Original constructor (for backward compatibility)
    WowGameObject(uintptr_t objectPtr);
    
    // New constructor that takes GUID from enumeration
    WowGameObject(uintptr_t objectPtr, WGUID guid);
    
    virtual ~WowGameObject() = default;

    // Override position reading for GameObjects
    virtual Vector3 GetPosition() const override;

    // GameObject-specific methods
    uint32_t GetDisplayId() const;
    
    // Fishing-specific methods
    bool IsBobbing() const;
    void Interact() const;
}; 