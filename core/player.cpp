#include "types.h"
#include <Windows.h>
#include <cstdio>

C3Vector GetLocalPlayerPosition() {
    C3Vector position(0.0f, 0.0f, 0.0f);
    
    try {
        // Step 1: Get the player object using WoW's own function
        GetActivePlayerObjectFn getActivePlayerObject = reinterpret_cast<GetActivePlayerObjectFn>(GET_ACTIVE_PLAYER_OBJECT_ADDR);
        
        if (!getActivePlayerObject) {
            OutputDebugStringA("[Player] ERROR: getActivePlayerObject function pointer is null");
            return position;
        }
        
        OutputDebugStringA("[Player] Calling getActivePlayerObject...");
        
        // Call WoW's getActivePlayerObject function
        void* pPlayerObject = getActivePlayerObject();
        if (!pPlayerObject) {
            OutputDebugStringA("[Player] ERROR: getActivePlayerObject returned null - player not in world");
            return position; // Player not in world or not found
        }
        
        char debugMsg[256];
        sprintf_s(debugMsg, "[Player] Got player object: 0x%08X", reinterpret_cast<uintptr_t>(pPlayerObject));
        OutputDebugStringA(debugMsg);
        
        // Step 2: Get the position using the virtual function
        // Read the vtable pointer from the player object
        void** vtable = *reinterpret_cast<void***>(pPlayerObject);
        if (!vtable) {
            OutputDebugStringA("[Player] ERROR: vtable is null");
            return position;
        }
        
        sprintf_s(debugMsg, "[Player] Got vtable: 0x%08X", reinterpret_cast<uintptr_t>(vtable));
        OutputDebugStringA(debugMsg);
        
        // Get the GetPosition function pointer from the vtable
        GetPositionFn getPosition = reinterpret_cast<GetPositionFn>(reinterpret_cast<uintptr_t*>(vtable)[PLAYER_GETPOSITION_VTABLE_OFFSET / 4]);
        if (!getPosition) {
            OutputDebugStringA("[Player] ERROR: GetPosition function pointer is null");
            return position;
        }
        
        sprintf_s(debugMsg, "[Player] Got GetPosition function: 0x%08X", reinterpret_cast<uintptr_t>(getPosition));
        OutputDebugStringA(debugMsg);
        
        OutputDebugStringA("[Player] Calling GetPosition virtual function...");
        
        // Create a buffer on the stack to hold the result
        C3Vector positionBuffer;
        
        // Call the GetPosition virtual function with correct signature
        try {
            getPosition(pPlayerObject, &positionBuffer);
        }
        catch (...) {
            OutputDebugStringA("[Player] ERROR: Exception during GetPosition call");
            return position;
        }
        
        OutputDebugStringA("[Player] GetPosition call completed successfully");
        
        // Copy the position data from the buffer
        position.x = positionBuffer.x;
        position.y = positionBuffer.y;
        position.z = positionBuffer.z;
        
        sprintf_s(debugMsg, "[Player] Player position: (%.2f, %.2f, %.2f)", position.x, position.y, position.z);
        OutputDebugStringA(debugMsg);
        
    } catch (...) {
        OutputDebugStringA("[Player] ERROR: Exception caught in GetLocalPlayerPosition");
        // Return default position on any exception
        position.x = 0.0f;
        position.y = 0.0f;
        position.z = 0.0f;
    }
    
    return position;
}

bool IsValidPlayerPosition(const C3Vector& pos) {
    // Check if position is not zero and within reasonable bounds
    if (pos.IsZero()) {
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