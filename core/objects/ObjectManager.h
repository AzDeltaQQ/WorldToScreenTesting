#pragma once

#include "../types/types.h"
#include "WowObject.h"
#include "WowUnit.h"
#include "WowPlayer.h"
#include "WowGameObject.h"
#include <memory>
#include <map>
#include <vector>
#include <mutex>
#include <unordered_map>
#include <string>

// Forward declarations
class WowObject;
class WowUnit;
class WowPlayer;
class WowGameObject;

// Function pointer types for WoW API functions
typedef int(__cdecl* EnumVisibleObjectsCallback)(uint32_t guid_low, uint32_t guid_high, int callback_arg);
typedef int(__cdecl* EnumVisibleObjectsFn)(EnumVisibleObjectsCallback callback, int callback_arg);
typedef void* (__thiscall* GetObjectPtrByGuidInnerFn)(void* thisptr, uint32_t guid_low, WGUID* pGuidStruct);
typedef WGUID*(__thiscall* GetLocalPlayerGuidFn)(WGUID* result);

class ObjectManager {
private:
    static ObjectManager* s_instance;
    static std::mutex s_instanceMutex;

    // WoW API function pointers
    EnumVisibleObjectsFn m_enumVisibleObjectsFn = nullptr;
    GetObjectPtrByGuidInnerFn m_getObjectPtrByGuidInnerFn = nullptr;
    GetLocalPlayerGuidFn m_getLocalPlayerGuidFn = nullptr;
    
    // Object cache with thread safety
    std::unordered_map<WGUID, std::shared_ptr<WowObject>, WGUIDHash> m_objectCache;
    mutable std::mutex m_cacheMutex;
    
    // Local player cache
    std::shared_ptr<WowPlayer> m_localPlayer;
    WGUID m_localPlayerGuid;
    
    // Initialization state
    bool m_isInitialized = false;
    bool m_shutdownRequested = false; // New flag to prevent deadlocks during shutdown
    
    // Object manager instance pointer (from game memory)
    uintptr_t m_objectManagerPtr = 0;
    
    // Private constructor for singleton
    ObjectManager() = default;

    // Static callback for object enumeration
    static int __cdecl EnumObjectsCallback(uint32_t guid_low, uint32_t guid_high, int callback_arg);
    
    // Object creation and processing
    void ProcessFoundObject(WGUID guid, void* objectPtr);
    std::shared_ptr<WowObject> CreateObjectFromPtr(uintptr_t objectPtr, WGUID guid);
    
    // Helper methods for reading object data
    WGUID ReadGUID(uintptr_t baseAddress, uintptr_t offset);
    WowObjectType ReadObjectType(uintptr_t baseAddress, uintptr_t offset);
    uintptr_t ReadObjectBaseAddress(uintptr_t entryAddress);
    
    // Descriptor field reading
    template <typename T>
    T ReadDescriptorField(uintptr_t baseAddress, uint32_t fieldOffset);
    
    // Thread-safe object access
    std::shared_ptr<WowObject> GetObjectByGUID_locked(WGUID guid);

public:
    ~ObjectManager();
    
    // Singleton management
    static ObjectManager* GetInstance();
    static void Shutdown(); 
    
    // Reset state
    void ResetState();
    
    // Initialization
    bool InitializeFunctions(uintptr_t enumVisibleObjectsAddr, uintptr_t getObjectPtrByGuidInnerAddr, uintptr_t getLocalPlayerGuidAddr);
    
    // Try to complete initialization with retry logic
    bool TryFinishInitialization(); 
    
    // Check initialization status
    bool IsInitialized() const;
    
    // Request shutdown (call before Shutdown() to prevent deadlocks)
    void RequestShutdown();
    
    // Update object cache (enumerates objects)
    void Update();
    
    // Refresh local player cache specifically
    void RefreshLocalPlayerCache();
    
    // Check if player is in world
    bool IsPlayerInWorld() const;

    // Object Accessors (Using WGUID like WoWBot)
    std::shared_ptr<WowObject> GetObjectByGUID(WGUID guid);
    std::shared_ptr<WowObject> GetObjectByGUID(uint64_t guid64);
    std::vector<std::shared_ptr<WowObject>> GetObjectsByType(WowObjectType type);
    std::shared_ptr<WowPlayer> GetLocalPlayer();
    
    // Get local player GUID
    WGUID GetLocalPlayerGuid() const;
    
    // Get all objects (thread-safe copy)
    std::map<WGUID, std::shared_ptr<WowObject>> GetAllObjects(); 
    
    // Object search methods
    std::vector<std::shared_ptr<WowObject>> FindObjectsByName(const std::string& name);
    
    // Spatial queries
    std::shared_ptr<WowObject> GetNearestObject(WowObjectType type, float maxDistance = 100.0f);
    
    // Get objects within distance of a point
    std::vector<std::shared_ptr<WowObject>> GetObjectsWithinDistance(const Vector3& center, float distance);
    
    // Type-specific accessors (convenience methods)
    std::vector<std::shared_ptr<WowUnit>> GetAllUnits();
    std::vector<std::shared_ptr<WowPlayer>> GetAllPlayers();
    std::vector<std::shared_ptr<WowGameObject>> GetAllGameObjects();
    
    // Type-safe GUID accessors
    std::shared_ptr<WowObject> GetObjectByGuid(const WGUID& guid) const;
    std::shared_ptr<WowUnit> GetUnitByGuid(const WGUID& guid) const;
    std::shared_ptr<WowPlayer> GetPlayerByGuid(const WGUID& guid) const;
    std::shared_ptr<WowGameObject> GetGameObjectByGuid(const WGUID& guid) const;
    
    // Utility conversions
    static WGUID Guid64ToWGUID(uint64_t guid64) { return WGUID(guid64); }
    static uint64_t WGUIDToGuid64(WGUID wguid) { return wguid.ToUint64(); }
    
    // Combat utility methods
    int CountUnitsInMeleeRange(std::shared_ptr<WowUnit> centerUnit, float range = 5.0f, bool includeHostile = true, bool includeFriendly = false, bool includeNeutral = false);
    int CountUnitsInFrontalCone(std::shared_ptr<WowUnit> caster, float range, float coneAngleDegrees, bool includeHostile = true, bool includeFriendly = false, bool includeNeutral = false);
}; 