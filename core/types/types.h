#pragma once

#include <cstdint>
#include <cmath>
#include <functional>

// Basic Windows types
typedef unsigned long DWORD;

// GUID structure for WoW objects
struct WGUID {
    uint32_t low;
    uint32_t high;

    WGUID() : low(0), high(0) {}
    WGUID(uint32_t l, uint32_t h) : low(l), high(h) {}
    explicit WGUID(uint64_t guid64) {
        low = static_cast<uint32_t>(guid64 & 0xFFFFFFFF);
        high = static_cast<uint32_t>((guid64 >> 32) & 0xFFFFFFFF);
    }

    uint64_t ToUint64() const {
        return (static_cast<uint64_t>(high) << 32) | low;
    }

    bool operator==(const WGUID& other) const {
        return low == other.low && high == other.high;
    }
    
    bool operator!=(const WGUID& other) const {
        return !(*this == other);
    }
    
    bool operator<(const WGUID& other) const {
        if (high != other.high) {
            return high < other.high;
        }
        return low < other.low;
    }

    bool IsValid() const {
        return low != 0 || high != 0;
    }
};

// Hash function for WGUID to use in unordered containers
struct WGUIDHash {
    std::size_t operator()(const WGUID& guid) const {
        return std::hash<uint64_t>()(guid.ToUint64());
    }
};

// 3D Vector structure
struct Vector3 {
    float x, y, z;

    Vector3() : x(0.0f), y(0.0f), z(0.0f) {}
    Vector3(float _x, float _y, float _z) : x(_x), y(_y), z(_z) {}

    float Distance(const Vector3& other) const {
        float dx = x - other.x;
        float dy = y - other.y;
        float dz = z - other.z;
        return std::sqrt(dx * dx + dy * dy + dz * dz);
    }

    float DistanceSq(const Vector3& other) const {
        float dx = x - other.x;
        float dy = y - other.y;
        float dz = z - other.z;
        return dx * dx + dy * dy + dz * dz;
    }
    
    bool IsZero() const {
        return x == 0.0f && y == 0.0f && z == 0.0f;
    }

    Vector3 operator+(const Vector3& other) const {
        return Vector3(x + other.x, y + other.y, z + other.z);
    }

    Vector3 operator-(const Vector3& other) const {
        return Vector3(x - other.x, y - other.y, z - other.z);
    }

    Vector3 operator*(float scalar) const {
        return Vector3(x * scalar, y * scalar, z * scalar);
    }
};

// WoW Object Types
enum WowObjectType : int {
    OBJECT_NONE = 0,
    OBJECT_ITEM = 1,
    OBJECT_CONTAINER = 2,
    OBJECT_UNIT = 3,
    OBJECT_PLAYER = 4,
    OBJECT_GAMEOBJECT = 5,
    OBJECT_DYNAMICOBJECT = 6,
    OBJECT_CORPSE = 7,
    OBJECT_TOTAL
};

// Power Types (WoW 3.3.5a)
enum PowerType : uint8_t {
    POWER_TYPE_MANA = 0,
    POWER_TYPE_RAGE = 1,
    POWER_TYPE_FOCUS = 2,
    POWER_TYPE_ENERGY = 3,
    POWER_TYPE_HAPPINESS = 4,
    POWER_TYPE_RUNE = 6,
    POWER_TYPE_RUNIC_POWER = 7,
    POWER_TYPE_COUNT
};

// Spell Schools (WoW 3.3.5a)
enum SpellSchool : uint8_t {
    SPELL_SCHOOL_NORMAL = 0,
    SPELL_SCHOOL_HOLY = 1,
    SPELL_SCHOOL_FIRE = 2,
    SPELL_SCHOOL_NATURE = 3,
    SPELL_SCHOOL_FROST = 4,
    SPELL_SCHOOL_SHADOW = 5,
    SPELL_SCHOOL_ARCANE = 6
};

// Game memory offsets (WoW 3.3.5a specific) - copied from CryoSource
namespace GameOffsets {
    constexpr uintptr_t STATIC_CLIENT_CONNECTION = 0x00C79CE0;
    constexpr uintptr_t OBJECT_MANAGER_OFFSET = 0x2ED0;
    constexpr uintptr_t OBJECT_TYPE_OFFSET = 0x14;
    constexpr uintptr_t CURRENT_TARGET_GUID_ADDR = 0x00BD07B0;
    constexpr uintptr_t LOCAL_GUID_OFFSET = 0xC0;
    constexpr uintptr_t IS_IN_WORLD_ADDR = 0x00B6AA38;
    constexpr uintptr_t ENUM_VISIBLE_OBJECTS_ADDR = 0x004D4B30;
    constexpr uintptr_t GET_OBJECT_BY_GUID_INNER_ADDR = 0x004D4BB0;
    constexpr uintptr_t GET_LOCAL_PLAYER_GUID_ADDR = 0x0;
    constexpr uintptr_t WORLD_LOADED_FLAG_ADDR = 0x00BEBA40;
    constexpr uintptr_t PLAYER_IS_LOOTING_OFFSET = 0x18E8;
}

// Game function signatures
typedef int(__cdecl* EnumVisibleObjectsCallback)(uint32_t guid_low, uint32_t guid_high, int callback_arg);
typedef int(__cdecl* EnumVisibleObjectsFn)(EnumVisibleObjectsCallback callback, int callback_arg);
typedef void*(__thiscall* GetObjectPtrByGuidInnerFn)(void* thisptr, uint32_t guid_low, WGUID* pGuidStruct);

// Forward declarations
class WowObject;

// Game's object manager structure (partial)
struct ObjectManagerActual {
    uint8_t padding1[0x1C];
    void* hashTableBase;
    uint8_t padding2[0x4];
    uint32_t hashTableMask;
    // ... other fields
}; 