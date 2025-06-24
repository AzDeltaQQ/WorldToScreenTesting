#pragma once

#include <cstdint>
#include <cmath>
#include <Windows.h>
#include <cstdio>
#include <float.h>

// Forward declarations
struct IDirect3DDevice9;

// Basic vector structure matching WoW's C3Vector
struct C3Vector {
    float x, y, z;
    
    C3Vector() : x(0), y(0), z(0) {}
    C3Vector(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}

    float Distance(const C3Vector& other) const {
        float dx = x - other.x;
        float dy = y - other.y;
        float dz = z - other.z;
        return std::sqrt(dx * dx + dy * dy + dz * dz);
    }

    bool IsZero() const {
        return x == 0.0f && y == 0.0f && z == 0.0f;
    }
};

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

// Forward declaration for WoW's WorldFrame structure
struct CGWorldFrame;

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