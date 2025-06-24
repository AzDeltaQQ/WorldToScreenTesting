#include "types.h"
#include "Logger.h"
#include <Windows.h>
#include <cmath>
#include <float.h>
#include <cstdio>

C3Vector GetLocalPlayerPosition() {
    C3Vector position(0.0f, 0.0f, 0.0f);
    
    try {
        // Add additional safety checks before accessing any memory
        if (IsBadCodePtr(reinterpret_cast<FARPROC>(GET_ACTIVE_PLAYER_OBJECT_ADDR))) {
            LOG_ERROR("GET_ACTIVE_PLAYER_OBJECT_ADDR is invalid");
            return position;
        }
        
        // Step 1: Get the player object using WoW's own function
        GetActivePlayerObjectFn getActivePlayerObject = reinterpret_cast<GetActivePlayerObjectFn>(GET_ACTIVE_PLAYER_OBJECT_ADDR);
        
        // Call WoW's getActivePlayerObject function
        void* pPlayerObject = getActivePlayerObject();
        
        if (!pPlayerObject) {
            LOG_ERROR("getActivePlayerObject returned null - player not in world");
            return position; // Player not in world or not found
        }
        
        // Validate the player object pointer before using it
        if (IsBadReadPtr(pPlayerObject, sizeof(void*))) {
            LOG_ERROR("Invalid player object pointer");
            return position;
        }
        
        // Step 2: Get the position using the virtual function
        // Read the vtable pointer from the player object with safety checks
        void** vtable = *reinterpret_cast<void***>(pPlayerObject);
        
        if (!vtable || IsBadReadPtr(vtable, PLAYER_GETPOSITION_VTABLE_OFFSET + sizeof(void*))) {
            LOG_ERROR("vtable is null or invalid");
            return position;
        }
        
        // Get the GetPosition function pointer from the vtable with bounds checking
        void* getPositionAddr = reinterpret_cast<void*>(vtable[PLAYER_GETPOSITION_VTABLE_OFFSET / 4]);
        
        if (!getPositionAddr || IsBadCodePtr(reinterpret_cast<FARPROC>(getPositionAddr))) {
            LOG_ERROR("GetPosition function pointer is null or invalid");
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
            LOG_ERROR("GetPosition returned invalid floating point values");
            return position;
        }
        
        // Copy the position data from the buffer
        position.x = positionBuffer.x;
        position.y = positionBuffer.y;
        position.z = positionBuffer.z;
        
        // Only log player position occasionally for debugging
        static int logCounter = 0;
        if (++logCounter % 300 == 0) { // Log every 300 calls (about every 5 seconds at 60fps)
            LOG_DEBUG("Player position: (" + std::to_string(position.x) + ", " + 
                     std::to_string(position.y) + ", " + std::to_string(position.z) + ")");
        }
        
    } 
    catch (...) {
        LOG_ERROR("Exception caught in GetLocalPlayerPosition");
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