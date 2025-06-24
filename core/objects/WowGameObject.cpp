#include "WowGameObject.h"

WowGameObject::WowGameObject(uintptr_t objectPtr) : WowObject(objectPtr) {
}

WowGameObject::WowGameObject(uintptr_t objectPtr, WGUID guid) : WowObject(objectPtr, guid) {
}

uint32_t WowGameObject::GetDisplayId() const {
    // Placeholder implementation
    return GetDescriptorField<uint32_t>(0x20); // GAMEOBJECT_DISPLAYID
}

Vector3 WowGameObject::GetPosition() const {
    if (!IsValidPtr()) return Vector3();
    
    // GameObject position offsets for WoW 3.3.5a (CORRECTED from Rotations project)
    // GameObjects use different offsets than units/players
    static constexpr uint32_t GO_RAW_POS_X = 0xE8;
    static constexpr uint32_t GO_RAW_POS_Y = 0xEC; // X + 0x4
    static constexpr uint32_t GO_RAW_POS_Z = 0xF0; // X + 0x8
    
    try {
        float posX = Memory::Read<float>(m_objectPtr + GO_RAW_POS_X);
        float posY = Memory::Read<float>(m_objectPtr + GO_RAW_POS_Y);
        float posZ = Memory::Read<float>(m_objectPtr + GO_RAW_POS_Z);
        
        // Return positions as-is (no coordinate swapping needed for GameObjects)
        return Vector3(posX, posY, posZ);
    } catch (const MemoryAccessError& e) {
        (void)e; // Mark as unused
        return Vector3(); // Return zero vector on error
    } catch (...) {
        return Vector3(); // Return zero vector on any other error
    }
}

bool WowGameObject::IsBobbing() const {
    if (!IsValidPtr()) return false;
    
    try {
        // For fishing bobbers, check if the bobber is "bobbing" (fish bite detected)
        // Based on Rotations project - use exact same offset and data type
        static constexpr uint32_t BOBBING_FLAG_OFFSET = 0xBC;
        uint8_t bobbingFlag = Memory::Read<uint8_t>(m_objectPtr + BOBBING_FLAG_OFFSET);
        
        // Flag value 1 indicates bobbing/bite detected
        return bobbingFlag == 1;
    } catch (const MemoryAccessError& e) {
        (void)e; // Mark as unused
        return false;
    } catch (...) {
        return false;
    }
}

void WowGameObject::Interact() const {
    if (!IsValidPtr()) return;
    
    try {
        // Call the GameObject's Interact VTable function
        // Based on Rotations project - VTable function for interaction
        static constexpr uint32_t VF_Interact = 44; // VTable function index for Interact (corrected from Rotations)
        
        uintptr_t* vTable = Memory::Read<uintptr_t*>(m_objectPtr);
        if (vTable) {
            typedef void(__thiscall* InteractFn)(uintptr_t thisPtr);
            InteractFn interactFunc = reinterpret_cast<InteractFn>(vTable[VF_Interact]);
            if (interactFunc) {
                interactFunc(m_objectPtr);
            }
        }
    } catch (const MemoryAccessError& e) {
        (void)e; // Mark as unused
        // Silently fail on memory access errors
    } catch (...) {
        // Silently fail on any other errors
    }
} 