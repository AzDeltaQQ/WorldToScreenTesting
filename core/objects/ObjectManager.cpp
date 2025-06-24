#include "ObjectManager.h"
#include "WowObject.h"
#include "WowUnit.h"
#include "WowPlayer.h"
#include "WowGameObject.h"
#include "../memory/memory.h"
#include "../types/types.h"
#include "../logs/Logger.h"
#include <cmath>
#include <memory>
#include <algorithm>

// Static member definitions
ObjectManager* ObjectManager::s_instance = nullptr;
std::mutex ObjectManager::s_instanceMutex;

ObjectManager::~ObjectManager() {
    std::lock_guard<std::mutex> lock(m_cacheMutex);
    m_objectCache.clear();
    m_localPlayer.reset();
}

ObjectManager* ObjectManager::GetInstance() {
    if (!s_instance) {
        std::lock_guard<std::mutex> lock(s_instanceMutex);
        if (!s_instance) {
            s_instance = new ObjectManager();
        }
    }
    return s_instance;
}

void ObjectManager::ResetState() {
    std::lock_guard<std::mutex> lock(m_cacheMutex);
    m_objectCache.clear();
    m_localPlayer.reset();
    m_localPlayerGuid = WGUID();
    m_isInitialized = false;
}

bool ObjectManager::InitializeFunctions(uintptr_t enumVisibleObjectsAddr, uintptr_t getObjectPtrByGuidInnerAddr, uintptr_t getLocalPlayerGuidAddr) {
    try {
        // Set function pointers - use passed addresses
        m_enumVisibleObjectsFn = reinterpret_cast<EnumVisibleObjectsFn>(enumVisibleObjectsAddr);
        m_getObjectPtrByGuidInnerFn = reinterpret_cast<GetObjectPtrByGuidInnerFn>(getObjectPtrByGuidInnerAddr);
        m_getLocalPlayerGuidFn = reinterpret_cast<GetLocalPlayerGuidFn>(getLocalPlayerGuidAddr);
        
        // Validate addresses
        if (enumVisibleObjectsAddr != 0 && getObjectPtrByGuidInnerAddr != 0) {
            LOG_INFO("Function pointers set successfully");
            return true;
        } else {
            LOG_WARNING("Some function addresses are null");
            return false;
        }
    } catch (...) {
        LOG_ERROR("Exception in InitializeFunctions");
        m_isInitialized = false;
        return false;
    }
}

void ObjectManager::Shutdown() {
    std::lock_guard<std::mutex> lock(s_instanceMutex);
    delete s_instance;
    s_instance = nullptr;
}

bool ObjectManager::TryFinishInitialization() {
    if (m_isInitialized) {
        return true;
    }
    
    try {
        // Check if we have the basic function pointer
        if (!m_enumVisibleObjectsFn) {
            LOG_DEBUG("TryFinishInitialization: No enum function");
            return false;
        }
        
        // Try to read object manager instance from memory
        uintptr_t clientConnection = Memory::Read<uintptr_t>(GameOffsets::STATIC_CLIENT_CONNECTION);
        if (clientConnection == 0) {
            LOG_DEBUG("TryFinishInitialization: No client connection");
            return false;
        }
        
        uintptr_t objMgrAddress = clientConnection + GameOffsets::OBJECT_MANAGER_OFFSET;
        uintptr_t objMgrPtr = Memory::Read<uintptr_t>(objMgrAddress);
        if (objMgrPtr == 0) {
            LOG_DEBUG("TryFinishInitialization: No object manager pointer");
            return false;
        }
        
        m_objectManagerPtr = objMgrPtr;
        m_isInitialized = true;
        
        LOG_INFO("Initialization successful!");
        return true;
        
    } catch (const MemoryAccessError& e) {
        LOG_ERROR("Memory error during initialization: " + std::string(e.what()));
        return false;
    } catch (...) {
        LOG_ERROR("Unknown error during initialization");
        return false;
    }
}

bool ObjectManager::IsInitialized() const {
    return m_isInitialized;
}

void ObjectManager::RequestShutdown() {
    LOG_INFO("Shutdown requested - stopping updates");
    m_shutdownRequested = true;
}

int __cdecl ObjectManager::EnumObjectsCallback(uint32_t guid_low, uint32_t guid_high, int callback_arg) {
    ObjectManager* objMgr = reinterpret_cast<ObjectManager*>(callback_arg);
    if (!objMgr) return 1;
    
    try {
        WGUID guid(guid_low, guid_high);
        
        // Get object pointer using the WoW function
        if (objMgr->m_getObjectPtrByGuidInnerFn && objMgr->m_objectManagerPtr) {
            void* objectPtr = objMgr->m_getObjectPtrByGuidInnerFn(
                reinterpret_cast<void*>(objMgr->m_objectManagerPtr), 
                guid_low, 
                &guid
            );
            
            if (objectPtr) {
                objMgr->ProcessFoundObject(guid, objectPtr);
            }
        }
    } catch (...) {
        // Continue enumeration on error
    }
    
    return 1; // Continue enumeration
}

void ObjectManager::ProcessFoundObject(WGUID guid, void* objectPtr) {
    if (!objectPtr || !guid.IsValid()) return;
    
    try {
        uintptr_t objAddress = reinterpret_cast<uintptr_t>(objectPtr);
        
        // Check if object is valid
        if (!Memory::IsValidAddress(objAddress)) return;
        
        // Read object type using GameOffsets
        WowObjectType objType = Memory::Read<WowObjectType>(objAddress + GameOffsets::OBJECT_TYPE_OFFSET);
        
        // Create appropriate object type
        std::shared_ptr<WowObject> obj = nullptr;
        
        switch (objType) {
            case OBJECT_PLAYER:
                obj = std::make_shared<WowPlayer>(objAddress, guid);
                break;
            case OBJECT_UNIT:
                obj = std::make_shared<WowUnit>(objAddress, guid);
                break;
            case OBJECT_GAMEOBJECT:
                obj = std::make_shared<WowGameObject>(objAddress, guid);
                break;
            default:
                obj = std::make_shared<WowObject>(objAddress, guid);
                break;
        }
        
        if (obj && obj->IsValid()) {
            // Update dynamic data to cache position, health, etc.
            obj->UpdateDynamicData();
            
            // Add to cache (thread-safe)
            std::lock_guard<std::mutex> lock(m_cacheMutex);
            m_objectCache[guid] = obj;
            
            // Check if this is the local player
            if (objType == OBJECT_PLAYER) {
                auto player = std::dynamic_pointer_cast<WowPlayer>(obj);
                if (player) {
                    // In CryoSource, local player is determined differently
                    // For now, we'll consider any player as potential local player
                    if (!m_localPlayer) {
                        m_localPlayer = player;
                        m_localPlayerGuid = guid;
                    }
                }
            }
        }
    } catch (...) {
        // Silently ignore invalid objects
    }
}

void ObjectManager::Update() {
    // Exit immediately if shutdown was requested
    if (m_shutdownRequested) {
        return;
    }
    
    if (!m_isInitialized || !m_enumVisibleObjectsFn) {
        return;
    }
    
    try {
        // Clear the cache
        {
            std::lock_guard<std::mutex> lock(m_cacheMutex);
            m_objectCache.clear();
            // Keep local player reference for now
        }
        
        // Refresh local player GUID first
        RefreshLocalPlayerCache();
        
        // Enumerate visible objects
        m_enumVisibleObjectsFn(EnumObjectsCallback, reinterpret_cast<int>(this));
        
        // Update dynamic data for all cached objects (like CryoSource)
        {
            std::lock_guard<std::mutex> lock(m_cacheMutex);
            for (const auto& pair : m_objectCache) {
                if (pair.second) {
                    try {
                        pair.second->UpdateDynamicData();
                    } catch (...) {
                        // Ignore update errors for individual objects
                    }
                }
            }
        }
        
        // Debug output
        static int updateCounter = 0;
        updateCounter++;
        if (updateCounter % 300 == 0) { // Every 5 seconds at 60 FPS
            std::lock_guard<std::mutex> lock(m_cacheMutex);
            LOG_DEBUG("Found " + std::to_string(m_objectCache.size()) + " objects");
        }
        
    } catch (...) {
        // Handle enumeration errors silently
    }
}

void ObjectManager::RefreshLocalPlayerCache() {
    // Try to get local player GUID using CryoSource method
    try {
        uintptr_t clientConnection = Memory::Read<uintptr_t>(GameOffsets::STATIC_CLIENT_CONNECTION);
        if (clientConnection != 0) {
            uint64_t guid64 = Memory::Read<uint64_t>(clientConnection + GameOffsets::LOCAL_GUID_OFFSET);
            if (guid64 != 0) {
                m_localPlayerGuid = WGUID(guid64);
            }
        }
    } catch (...) {
        // Handle errors silently
    }
}

bool ObjectManager::IsPlayerInWorld() const {
    return m_localPlayer && m_localPlayer->IsValid();
}

std::shared_ptr<WowObject> ObjectManager::GetObjectByGUID(WGUID guid) {
    std::lock_guard<std::mutex> lock(m_cacheMutex);
    return GetObjectByGUID_locked(guid);
}

std::shared_ptr<WowObject> ObjectManager::GetObjectByGUID(uint64_t guid64) {
    return GetObjectByGUID(WGUID(guid64));
}

std::shared_ptr<WowObject> ObjectManager::GetObjectByGUID_locked(WGUID guid) {
    auto it = m_objectCache.find(guid);
    if (it != m_objectCache.end()) {
        return it->second;
    }
    return nullptr;
}

std::vector<std::shared_ptr<WowObject>> ObjectManager::GetObjectsByType(WowObjectType type) {
    std::vector<std::shared_ptr<WowObject>> result;
    std::lock_guard<std::mutex> lock(m_cacheMutex);
    
    for (const auto& pair : m_objectCache) {
        if (pair.second && pair.second->GetObjectType() == type) {
            result.push_back(pair.second);
        }
    }
    return result;
}

std::shared_ptr<WowPlayer> ObjectManager::GetLocalPlayer() {
    std::lock_guard<std::mutex> lock(m_cacheMutex);
    return m_localPlayer;
}

WGUID ObjectManager::GetLocalPlayerGuid() const {
    return m_localPlayerGuid;
}

std::map<WGUID, std::shared_ptr<WowObject>> ObjectManager::GetAllObjects() {
    std::lock_guard<std::mutex> lock(m_cacheMutex);
    std::map<WGUID, std::shared_ptr<WowObject>> result;
    
    for (const auto& pair : m_objectCache) {
        result[pair.first] = pair.second;
    }
    return result;
}

std::vector<std::shared_ptr<WowObject>> ObjectManager::FindObjectsByName(const std::string& name) {
    std::vector<std::shared_ptr<WowObject>> result;
    std::lock_guard<std::mutex> lock(m_cacheMutex);
    
    for (const auto& pair : m_objectCache) {
        if (pair.second && pair.second->GetName() == name) {
            result.push_back(pair.second);
        }
    }
    return result;
}

std::vector<std::shared_ptr<WowUnit>> ObjectManager::GetAllUnits() {
    std::vector<std::shared_ptr<WowUnit>> result;
    std::lock_guard<std::mutex> lock(m_cacheMutex);
    
    for (const auto& pair : m_objectCache) {
        if (pair.second && (pair.second->GetObjectType() == OBJECT_UNIT || pair.second->GetObjectType() == OBJECT_PLAYER)) {
            auto unit = std::dynamic_pointer_cast<WowUnit>(pair.second);
            if (unit) {
                result.push_back(unit);
            }
        }
    }
    return result;
}

std::vector<std::shared_ptr<WowPlayer>> ObjectManager::GetAllPlayers() {
    std::vector<std::shared_ptr<WowPlayer>> result;
    std::lock_guard<std::mutex> lock(m_cacheMutex);
    
    for (const auto& pair : m_objectCache) {
        if (pair.second && pair.second->GetObjectType() == OBJECT_PLAYER) {
            auto player = std::dynamic_pointer_cast<WowPlayer>(pair.second);
            if (player) {
                result.push_back(player);
            }
        }
    }
    return result;
}

std::vector<std::shared_ptr<WowGameObject>> ObjectManager::GetAllGameObjects() {
    std::vector<std::shared_ptr<WowGameObject>> result;
    std::lock_guard<std::mutex> lock(m_cacheMutex);
    
    for (const auto& pair : m_objectCache) {
        if (pair.second && pair.second->GetObjectType() == OBJECT_GAMEOBJECT) {
            auto gameObject = std::dynamic_pointer_cast<WowGameObject>(pair.second);
            if (gameObject) {
                result.push_back(gameObject);
            }
        }
    }
    return result;
}

std::shared_ptr<WowObject> ObjectManager::GetObjectByGuid(const WGUID& guid) const {
    std::lock_guard<std::mutex> lock(m_cacheMutex);
    auto it = m_objectCache.find(guid);
    if (it != m_objectCache.end()) {
        return it->second;
    }
    return nullptr;
}

std::shared_ptr<WowUnit> ObjectManager::GetUnitByGuid(const WGUID& guid) const {
    auto obj = GetObjectByGuid(guid);
    return std::dynamic_pointer_cast<WowUnit>(obj);
}

std::shared_ptr<WowPlayer> ObjectManager::GetPlayerByGuid(const WGUID& guid) const {
    auto obj = GetObjectByGuid(guid);
    return std::dynamic_pointer_cast<WowPlayer>(obj);
}

std::shared_ptr<WowGameObject> ObjectManager::GetGameObjectByGuid(const WGUID& guid) const {
    auto obj = GetObjectByGuid(guid);
    return std::dynamic_pointer_cast<WowGameObject>(obj);
}

WGUID ObjectManager::ReadGUID(uintptr_t baseAddress, uintptr_t offset) {
    try {
        uint32_t low = Memory::Read<uint32_t>(baseAddress + offset);
        uint32_t high = Memory::Read<uint32_t>(baseAddress + offset + 4);
        return WGUID(low, high);
    } catch (...) {
        return WGUID();
    }
}

WowObjectType ObjectManager::ReadObjectType(uintptr_t baseAddress, uintptr_t offset) {
    try {
        return Memory::Read<WowObjectType>(baseAddress + offset);
    } catch (...) {
        return OBJECT_NONE;
    }
}

uintptr_t ObjectManager::ReadObjectBaseAddress(uintptr_t entryAddress) {
    try {
        return Memory::Read<uintptr_t>(entryAddress);
    } catch (...) {
        return 0;
    }
}

template <typename T>
T ObjectManager::ReadDescriptorField(uintptr_t baseAddress, uint32_t fieldOffset) {
    try {
        return Memory::Read<T>(baseAddress + 0x8 + fieldOffset);
    } catch (...) {
        return T{};
    }
}

// Continue with the rest of the implementation...
std::shared_ptr<WowObject> ObjectManager::GetNearestObject(WowObjectType type, float maxDistance) {
    auto localPlayer = GetLocalPlayer();
    if (!localPlayer) return nullptr;
    
    Vector3 playerPos = localPlayer->GetPosition();
    std::shared_ptr<WowObject> nearest = nullptr;
    float nearestDistance = maxDistance;
    
    std::lock_guard<std::mutex> lock(m_cacheMutex);
    for (const auto& pair : m_objectCache) {
        if (pair.second && pair.second->GetObjectType() == type) {
            Vector3 objPos = pair.second->GetPosition();
            float distance = playerPos.Distance(objPos);
            
            if (distance < nearestDistance) {
                nearestDistance = distance;
                nearest = pair.second;
            }
        }
    }
    
    return nearest;
}

std::vector<std::shared_ptr<WowObject>> ObjectManager::GetObjectsWithinDistance(const Vector3& center, float distance) {
    std::vector<std::shared_ptr<WowObject>> result;
    float distanceSq = distance * distance;
    
    std::lock_guard<std::mutex> lock(m_cacheMutex);
    for (const auto& pair : m_objectCache) {
        if (pair.second) {
            Vector3 objPos = pair.second->GetPosition();
            if (center.DistanceSq(objPos) <= distanceSq) {
                result.push_back(pair.second);
            }
        }
    }
    
    return result;
}

int ObjectManager::CountUnitsInMeleeRange(std::shared_ptr<WowUnit> centerUnit, float range, bool includeHostile, bool includeFriendly, bool includeNeutral) {
    if (!centerUnit) return 0;
    
    Vector3 centerPos = centerUnit->GetPosition();
    int count = 0;
    float rangeSq = range * range;
    
    std::lock_guard<std::mutex> lock(m_cacheMutex);
    for (const auto& pair : m_objectCache) {
        auto unit = std::dynamic_pointer_cast<WowUnit>(pair.second);
        if (!unit || unit == centerUnit) continue;
        
        Vector3 unitPos = unit->GetPosition();
        if (centerPos.DistanceSq(unitPos) <= rangeSq) {
            // Check faction filters
            if (unit->IsHostile() && includeHostile) count++;
            else if (unit->IsFriendly() && includeFriendly) count++;
            else if (!unit->IsHostile() && !unit->IsFriendly() && includeNeutral) count++;
        }
    }
    
    return count;
}

int ObjectManager::CountUnitsInFrontalCone(std::shared_ptr<WowUnit> caster, float range, float coneAngleDegrees, bool includeHostile, bool includeFriendly, bool includeNeutral) {
    if (!caster) return 0;
    
    Vector3 casterPos = caster->GetPosition();
    float casterFacing = caster->GetFacing();
    int count = 0;
    float rangeSq = range * range;
    float halfConeAngleRad = (coneAngleDegrees / 2.0f) * (3.14159f / 180.0f);
    
    std::lock_guard<std::mutex> lock(m_cacheMutex);
    for (const auto& pair : m_objectCache) {
        auto unit = std::dynamic_pointer_cast<WowUnit>(pair.second);
        if (!unit || unit == caster) continue;
        
        Vector3 unitPos = unit->GetPosition();
        Vector3 diff = unitPos - casterPos;
        float distanceSq = diff.x * diff.x + diff.y * diff.y;
        
        if (distanceSq <= rangeSq) {
            // Calculate angle between caster facing and direction to unit
            float angleToUnit = std::atan2(diff.y, diff.x);
            float angleDiff = std::abs(angleToUnit - casterFacing);
            
            // Normalize angle difference to [0, PI]
            if (angleDiff > 3.14159f) {
                angleDiff = 2.0f * 3.14159f - angleDiff;
            }
            
            if (angleDiff <= halfConeAngleRad) {
                // Check faction filters
                if (unit->IsHostile() && includeHostile) count++;
                else if (unit->IsFriendly() && includeFriendly) count++;
                else if (!unit->IsHostile() && !unit->IsFriendly() && includeNeutral) count++;
            }
        }
    }
    
    return count;
}

// Explicit template instantiation for common types
template uint32_t ObjectManager::ReadDescriptorField<uint32_t>(uintptr_t baseAddress, uint32_t fieldOffset);
template float ObjectManager::ReadDescriptorField<float>(uintptr_t baseAddress, uint32_t fieldOffset);
template uint64_t ObjectManager::ReadDescriptorField<uint64_t>(uintptr_t baseAddress, uint32_t fieldOffset); 