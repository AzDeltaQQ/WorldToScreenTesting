#include "WowObject.h"
#include "../memory/memory.h"

WowObject::WowObject(uintptr_t objectPtr) 
    : m_objectPtr(objectPtr), m_guid(), m_objectType(OBJECT_NONE) {
    Initialize();
}

WowObject::WowObject(uintptr_t objectPtr, WGUID guid) 
    : m_objectPtr(objectPtr), m_guid(guid), m_objectType(OBJECT_NONE) {
    Initialize(guid);
}

void WowObject::Initialize() {
    if (!IsValidPtr()) return;
    
    // Read GUID from object
    uint64_t guid64 = GetDescriptorField<uint64_t>(OBJECT_FIELD_GUID);
    m_guid = WGUID(guid64);
    
    // Read object type
    m_objectType = static_cast<WowObjectType>(Memory::Read<int>(m_objectPtr, OBJECT_FIELD_TYPE));
}

void WowObject::Initialize(WGUID guid) {
    if (!IsValidPtr()) return;
    
    // GUID is already set from constructor parameter
    m_guid = guid;
    
    // Read object type
    try {
        m_objectType = static_cast<WowObjectType>(Memory::Read<int>(m_objectPtr, OBJECT_FIELD_TYPE));
        
        // Basic validation
        if (m_objectType < OBJECT_NONE || m_objectType >= OBJECT_TOTAL) {
            m_objectType = OBJECT_NONE;
        }
    } catch (const MemoryAccessError& e) {
        (void)e; // Mark as unused
        m_objectType = OBJECT_NONE;
    } catch (...) {
        m_objectType = OBJECT_NONE;
    }
}

bool WowObject::IsValid() const {
    return m_guid.IsValid() && IsValidPtr();
}

bool WowObject::IsValidPtr() const {
    return m_objectPtr != 0 && Memory::IsValidAddress(m_objectPtr);
}

Vector3 WowObject::GetPosition() const {
    // Base class returns zero vector - position reading should be handled by derived classes
    // (WowUnit, WowGameObject, etc.) that know the correct offsets for their object type
    return Vector3();
}

float WowObject::GetOrientation() const {
    if (!IsValidPtr()) return 0.0f;
    
    static constexpr uint32_t ORIENTATION_OFFSET = 0x9C4;
    return Memory::Read<float>(m_objectPtr, ORIENTATION_OFFSET);
}

std::string WowObject::GetName() const {
    if (!IsValidPtr()) return "";
    
    // Use VTable-based name reading
    return ReadNameFromVTable();
}

// Helper method to read name via VTable (WoWBot method)
std::string WowObject::ReadNameFromVTable() const {
    if (!m_objectPtr) return "";
    
    // Define the function signature based on WoWBot structure
    typedef char* (__thiscall* GetNameFunc)(void* thisptr);

    try {
        // 1. Read VTable Pointer from Object Base
        uintptr_t vtableAddr = Memory::Read<uintptr_t>(m_objectPtr);
        if (!vtableAddr) { return "[Error VTable Null]"; }

        // 2. Read Function Pointer from VTable Address using the index
        uintptr_t funcAddr = Memory::Read<uintptr_t>(vtableAddr + (VF_GetName * sizeof(void*)));
        if (!funcAddr) { return "[Error Func Addr Null]"; }

        // 3. Cast and Call the Function
        GetNameFunc func = reinterpret_cast<GetNameFunc>(funcAddr);
        char* namePtrFromGame = func(reinterpret_cast<void*>(m_objectPtr)); 
        if (!namePtrFromGame) { return ""; } // Empty name is valid

        // 4. Read the string content from the pointer returned by the game function
        // Use the existing Memory::ReadString for safety
        return Memory::ReadString(reinterpret_cast<uintptr_t>(namePtrFromGame), 100); // Limit length

    } catch (const MemoryAccessError& e) {
        (void)e; // Mark as unused
        // Return error string for debugging
        return "[Error VTable Name Read]";
    } catch (...) {
        return "[Error VTable Name Unknown]";
    }
}

template<typename T>
T WowObject::ReadDescriptorField(uint32_t fieldOffset) const {
    try {
        // Descriptor fields start at offset 0x8
        return Memory::Read<T>(m_objectPtr + 0x8 + fieldOffset);
    } catch (...) {
        return T{};
    }
}

uint32_t WowObject::ReadUInt32(uintptr_t offset) const {
    try {
        return Memory::Read<uint32_t>(m_objectPtr + offset);
    } catch (...) {
        return 0;
    }
}

float WowObject::ReadFloat(uintptr_t offset) const {
    try {
        return Memory::Read<float>(m_objectPtr + offset);
    } catch (...) {
        return 0.0f;
    }
}

uintptr_t WowObject::ReadPointer(uintptr_t offset) const {
    try {
        return Memory::Read<uintptr_t>(m_objectPtr + offset);
    } catch (...) {
        return 0;
    }
}

std::string WowObject::ReadString(uintptr_t offset, size_t maxLength) const {
    try {
        uintptr_t stringPtr = Memory::Read<uintptr_t>(m_objectPtr + offset);
        if (stringPtr != 0) {
            return Memory::ReadString(stringPtr, maxLength);
        }
    } catch (...) {
        // Fall through
    }
    return "";
}

// Explicit template instantiation for common types
template uint32_t WowObject::ReadDescriptorField<uint32_t>(uint32_t fieldOffset) const;
template float WowObject::ReadDescriptorField<float>(uint32_t fieldOffset) const;
template uint64_t WowObject::ReadDescriptorField<uint64_t>(uint32_t fieldOffset) const; 