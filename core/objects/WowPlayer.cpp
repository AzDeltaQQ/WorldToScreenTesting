#include "WowPlayer.h"
#include "ObjectManager.h"
#include "../memory/memory.h"
#include "../types/types.h"

WowPlayer::WowPlayer(uintptr_t objectPtr) : WowUnit(objectPtr) {
}

WowPlayer::WowPlayer(uintptr_t objectPtr, WGUID guid) : WowUnit(objectPtr, guid) {
}

uint32_t WowPlayer::GetLevel() const {
    // Placeholder implementation
    return GetDescriptorField<uint32_t>(0x88); // UNIT_FIELD_LEVEL
}

bool WowPlayer::IsLocalPlayer() const {
    // Use the same logic as WowUnit::IsLocalPlayer()
    auto objectManager = ObjectManager::GetInstance();
    if (!objectManager) return false;
    
    WGUID localPlayerGuid = objectManager->GetLocalPlayerGuid();
    if (!localPlayerGuid.IsValid()) return false;
    
    // Compare GUIDs
    WGUID thisGuid = this->GetGUID();
    return thisGuid.IsValid() && (localPlayerGuid.ToUint64() == thisGuid.ToUint64());
}

bool WowPlayer::IsLooting() const {
    // EXACT SAME AS ROTATIONS - Check for 0x400 flag bit
    const uint32_t UNIT_FLAG_IS_LOOTING = 0x400;
    
    uint32_t currentFlags = this->GetUnitFlags();
    bool isLooting = (currentFlags & UNIT_FLAG_IS_LOOTING) != 0;
    
    return isLooting;
} 