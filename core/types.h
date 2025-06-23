#pragma once

#include <cstdint>
#include <cmath>

// Basic 3D Vector structure
struct C3Vector {
    float x, y, z;

    C3Vector() : x(0.0f), y(0.0f), z(0.0f) {}
    C3Vector(float _x, float _y, float _z) : x(_x), y(_y), z(_z) {}

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

// WorldToScreen function pointer type based on your analysis
typedef bool(__thiscall* WorldToScreenFn)(CGWorldFrame* pWorldFrame, const C3Vector* pWorldPos, C3Vector* pScreenPos, int* pOutcode);

// Game offsets - Updated with real addresses
namespace GameOffsets {
    constexpr uintptr_t WORLD_FRAME_PTR = 0x00B7436C; // Global WorldFrame pointer from your analysis
    constexpr uintptr_t WORLD_TO_SCREEN_FUNC = 0x004F6D20; // Real WorldToScreen function address
} 