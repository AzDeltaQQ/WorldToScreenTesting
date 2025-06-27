#pragma once

#include <cstdint>
#include <cmath>
#include <functional>
#include <Windows.h>
#include <cstdio>
#include <float.h>

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
    
    // Additional methods needed for humanization
    float Dot(const Vector3& other) const {
        return x * other.x + y * other.y + z * other.z;
    }
    
    float Length() const {
        return std::sqrt(x * x + y * y + z * z);
    }
    
    Vector3 Normalized() const {
        float len = Length();
        if (len > 0.0001f) {
            return Vector3(x / len, y / len, z / len);
        }
        return Vector3(0.0f, 0.0f, 0.0f);
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

// Forward declarations
struct IDirect3DDevice9;

// Navigation forward declarations
namespace Navigation {
    class NavigationManager;
    struct NavigationPath;
    struct Waypoint;
    struct PathfindingOptions;
    struct VisualizationSettings;
    enum class PathResult;
    enum class MovementType;
}

// Compatibility typedef for existing drawing code
typedef Vector3 C3Vector;

// WorldFrame structure (simplified)
struct CGWorldFrame {
    // Padding to reach the important offsets
    uint8_t padding1[0x64];      // 0x00 - 0x63
    float screenBounds[4];       // 0x64 - 0x70 (left, top, right, bottom for outcode)
    uint8_t padding2[0x2BC];     // 0x74 - 0x32F  
    float viewportBounds[4];     // 0x330 - 0x33C (left, top, right, bottom for viewport)
    float viewProjectionMatrix[16]; // 0x340 - 0x37F (4x4 matrix)
    uint8_t padding3[0x7AA0];    // 0x380 - 0x7E1F
    void* pActiveCamera;         // 0x7E20 - pointer to CCamera object
};

// Camera structure (simplified)
struct CCamera {
    uint8_t padding1[0x08];      // 0x00 - 0x07
    float position[3];           // 0x08 - 0x13 (x, y, z)
    uint8_t padding2[0x20];      // 0x14 - 0x37
    float nearClipPlane;         // 0x38 - near clip distance
};

// Outcode bits for screen bounds checking
enum OutcodeBits {
    OUTCODE_LEFT   = 0x01,
    OUTCODE_TOP    = 0x02, 
    OUTCODE_RIGHT  = 0x04,
    OUTCODE_BOTTOM = 0x08
};

// WorldToScreen function pointer type based on assembly analysis
// Function signature: bool __thiscall WorldToScreen(CGWorldFrame* this, float* worldPos, float* screenPos, int* outcode)
typedef bool(__thiscall* WorldToScreenFn)(CGWorldFrame* worldFrame, float* worldPos, float* screenPos, int* outcode);

// Function pointer type for EndScene
typedef HRESULT(__stdcall* EndSceneFn)(IDirect3DDevice9* device);

// Player object function pointer types
typedef void*(__cdecl* GetActivePlayerObjectFn)();
typedef void(__thiscall* GetPositionFn)(void* pObject, C3Vector* pOutVec);

// Game memory addresses - Original addresses from user's analysis
constexpr uintptr_t WORLDFRAME_PTR = 0x00B7436C;      // Pointer to WorldFrame
constexpr uintptr_t WORLDTOSCREEN_ADDR = 0x004F6D20;  // WorldToScreen function

// Local player addresses
constexpr uintptr_t GET_ACTIVE_PLAYER_OBJECT_ADDR = 0x004038F0; // getActivePlayerObject function
constexpr uintptr_t PLAYER_GETPOSITION_VTABLE_OFFSET = 0x2C;    // GetPosition virtual function offset

// Runtime address detection functions
uintptr_t FindWorldFramePtr();
uintptr_t FindWorldToScreenAddr();
uintptr_t FindLuaDoStringAddr();

// Address validation functions
bool IsValidMemoryAddress(uintptr_t address, size_t size);
bool IsValidCodeAddress(uintptr_t address);
bool ValidateGameAddresses();

// Player coordinate functions
C3Vector GetLocalPlayerPosition();
bool IsValidPlayerPosition(const C3Vector& pos);

// Implementation of player coordinate functions
inline C3Vector GetLocalPlayerPosition() {
    C3Vector position(0.0f, 0.0f, 0.0f);
    
    try {
        // Add additional safety checks before accessing any memory
        if (IsBadCodePtr(reinterpret_cast<FARPROC>(GET_ACTIVE_PLAYER_OBJECT_ADDR))) {
            // LOG_ERROR would need to be included, so we'll skip logging in header
            return position;
        }
        
        // Step 1: Get the player object using WoW's own function
        GetActivePlayerObjectFn getActivePlayerObject = reinterpret_cast<GetActivePlayerObjectFn>(GET_ACTIVE_PLAYER_OBJECT_ADDR);
        
        // Call WoW's getActivePlayerObject function
        void* pPlayerObject = getActivePlayerObject();
        
        if (!pPlayerObject) {
            return position; // Player not in world or not found
        }
        
        // Validate the player object pointer before using it
        if (IsBadReadPtr(pPlayerObject, sizeof(void*))) {
            return position;
        }
        
        // Step 2: Get the position using the virtual function
        // Read the vtable pointer from the player object with safety checks
        void** vtable = *reinterpret_cast<void***>(pPlayerObject);
        
        if (!vtable || IsBadReadPtr(vtable, PLAYER_GETPOSITION_VTABLE_OFFSET + sizeof(void*))) {
            return position;
        }
        
        // Get the GetPosition function pointer from the vtable with bounds checking
        void* getPositionAddr = reinterpret_cast<void*>(vtable[PLAYER_GETPOSITION_VTABLE_OFFSET / 4]);
        
        if (!getPositionAddr || IsBadCodePtr(reinterpret_cast<FARPROC>(getPositionAddr))) {
            return position;
        }
        
        GetPositionFn getPosition = reinterpret_cast<GetPositionFn>(getPositionAddr);
        
        // Create a buffer on the stack to hold the result
        C3Vector positionBuffer;
        
        // Call the GetPosition virtual function with correct signature
        getPosition(pPlayerObject, &positionBuffer);
        
        // Validate the returned position data using standard C++ functions
        if (std::isnan(positionBuffer.x) || std::isnan(positionBuffer.y) || std::isnan(positionBuffer.z) ||
            !std::isfinite(positionBuffer.x) || !std::isfinite(positionBuffer.y) || !std::isfinite(positionBuffer.z)) {
            return position;
        }
        
        // Copy the position data from the buffer
        position.x = positionBuffer.x;
        position.y = positionBuffer.y;
        position.z = positionBuffer.z;
        
    } 
    catch (...) {
        // Return default position on any exception
        position.x = 0.0f;
        position.y = 0.0f;
        position.z = 0.0f;
    }
    
    return position;
}

inline bool IsValidPlayerPosition(const C3Vector& pos) {
    // Check if position is not zero and within reasonable bounds
    if (pos.IsZero()) {
        return false;
    }
    
    // Check for NaN or infinite values using standard C++ functions
    if (std::isnan(pos.x) || std::isnan(pos.y) || std::isnan(pos.z) ||
        !std::isfinite(pos.x) || !std::isfinite(pos.y) || !std::isfinite(pos.z)) {
        return false;
    }
    
    // Check for reasonable coordinate bounds (WoW world coordinates)
    // Typical WoW coordinates range from -17000 to +17000
    const float MAX_COORD = 20000.0f;
    
    if (abs(pos.x) > MAX_COORD || abs(pos.y) > MAX_COORD || abs(pos.z) > MAX_COORD) {
        return false;
    }
    
    return true;
}

// Global managers - forward declarations
class ObjectManager;
class CombatLogAnalyzer; 