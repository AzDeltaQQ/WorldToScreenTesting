#pragma once

#include "../types/types.h"
#include "../memory/memory.h"
#include <string>
#include <cstdint>

class WowObject {
protected:
    uintptr_t m_objectPtr;
    WGUID m_guid;
    WowObjectType m_objectType;
    
    // Cached position for performance
    mutable Vector3 m_cachedPosition;
    mutable bool m_positionCached;

public:
    // Original constructor (for backward compatibility)
    WowObject(uintptr_t objectPtr);
    
    // New constructor that takes GUID from enumeration (like Rotations project)
    WowObject(uintptr_t objectPtr, WGUID guid);
    
    virtual ~WowObject() = default;

    // Basic accessors
    WGUID GetGUID() const { return m_guid; }
    uint64_t GetGUID64() const { return m_guid.ToUint64(); }
    WowObjectType GetObjectType() const { return m_objectType; }
    uintptr_t GetObjectPtr() const { return m_objectPtr; }
    
    // Validity checks
    bool IsValid() const;
    bool IsValidPtr() const;
    
    // Position and movement
    virtual Vector3 GetPosition() const;
    float GetOrientation() const;
    
    // Basic properties
    std::string GetName() const;
    
    // Update dynamic data (virtual method for derived classes)
    virtual void UpdateDynamicData() {}
    
    // Descriptor field reading
    template<typename T>
    T GetDescriptorField(uint32_t offset) const {
        if (!IsValidPtr()) return T{};
        return Memory::Read<T>(m_objectPtr, offset);
    }
    
protected:
    // Initialize from object pointer
    virtual void Initialize();
    
    // Initialize with known GUID (new method)
    virtual void Initialize(WGUID guid);
    
    // Helper method to read name via VTable (based on WoWBot approach)
    std::string ReadNameFromVTable() const;
    
    // Helper methods for reading memory (used by derived classes)
    template<typename T>
    T ReadDescriptorField(uint32_t fieldOffset) const;
    
    uint32_t ReadUInt32(uintptr_t offset) const;
    float ReadFloat(uintptr_t offset) const;
    uintptr_t ReadPointer(uintptr_t offset) const;
    std::string ReadString(uintptr_t offset, size_t maxLength = 256) const;
    
    // Descriptor offset constants (WoW 3.3.5a specific)
    static constexpr uint32_t DESCRIPTOR_OFFSET = 0x8;
    static constexpr uint32_t OBJECT_FIELD_GUID = 0x0;
    static constexpr uint32_t OBJECT_FIELD_TYPE = 0x14;
    
    // VTable offsets for name reading (WoW 3.3.5a specific)
    static constexpr uint32_t VF_GetName = 54; // VTable function index for GetName (from working Rotations project)
}; 