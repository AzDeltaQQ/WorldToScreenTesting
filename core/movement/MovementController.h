#pragma once

#include "../types/types.h"
#include "../logs/Logger.h"
#include <chrono>
#include <memory>
#include <queue>
#include <mutex>

// Forward declarations
class ObjectManager;
class WowPlayer;

// Function signature for handlePlayerClickToMove (0x727400)
typedef char (__fastcall* HandlePlayerClickToMoveFn)(
    void* pUnit,              // ECX: CGUnit_C* (local player object)
    void* edx,                // EDX: unused in __fastcall
    int actionType,           // Stack: CTM action type
    uint64_t* pTargetGuid,    // Stack: Pointer to target GUID (or nullptr)
    float* pCoordinates,      // Stack: Pointer to XYZ coordinates (3 floats)
    float facingAngle         // Stack: Initial facing angle
);

// Function signature for reset_tracking_and_input hook
typedef void (__fastcall* ResetTrackingAndInputFn)(void* ecx, void* edx, bool arg1, bool arg2);

// CTM action types (from handlePlayerClickToMove analysis)
namespace CTMActions {
    const int FACE_TARGET = 2;           // Face a specific target
    const int MOVE_TO_TERRAIN = 4;       // Move to a point on the ground
    const int INTERACT_OBJECT = 7;       // Interact with GameObject/NPC
    const int FOLLOW_TARGET = 8;         // Follow a target
    const int ATTACK_TARGET = 10;        // Attack a target
}

// Function addresses for WoW 3.3.5a (12340)
namespace WoWFunctions {
    const uintptr_t HANDLE_PLAYER_CLICK_TO_MOVE = 0x727400;  // handlePlayerClickToMove
    const uintptr_t RESET_TRACKING_AND_INPUT = 0x7272C0;     // reset_tracking_and_input (for hooks)
}

// CTM Global State Variables (The CTM Context)
namespace CTMGlobals {
    const uintptr_t G_CTM_ACTION_TYPE = 0xCA11F4;     // Current CTM Action Type (int)
    const uintptr_t G_CTM_TARGET_GUID = 0xCA11F8;     // Target GUID (uint64_t)
    const uintptr_t G_CTM_TARGET_POS = 0xCA1264;      // Target Coordinates (float[3])
    const uintptr_t G_CTM_FACING = 0xCA11EC;          // Target Facing Angle (float)
    const uintptr_t G_CTM_STOP_DISTANCE = 0xCA11E4;   // Stopping Distance (float)
    const uintptr_t G_CTM_START_TIME = 0xCA11F0;      // Action Start Time (uint32_t)
}

// Movement command types for safe execution
enum class MovementCommandType {
    MOVE_TO_TERRAIN,
    MOVE_TO_OBJECT,
    ATTACK_TARGET,
    FACE_TARGET,
    FACE_POSITION,
    STOP
};

// Movement command structure
struct MovementCommand {
    MovementCommandType type;
    Vector3 position;
    uint64_t targetGUID;
    float facingAngle;
    
    MovementCommand(MovementCommandType t, const Vector3& pos = Vector3(), uint64_t guid = 0, float angle = 0.0f)
        : type(t), position(pos), targetGUID(guid), facingAngle(angle) {}
};

// Movement state tracking
struct MovementState {
    bool isMoving = false;
    bool hasDestination = false;
    Vector3 currentDestination;
    Vector3 lastPlayerPosition;
    uint32_t currentAction = 0;
    uint64_t targetGUID = 0;
    std::chrono::steady_clock::time_point movementStartTime;
    std::chrono::steady_clock::time_point lastUpdateTime;
    
    void Reset() {
        isMoving = false;
        hasDestination = false;
        currentDestination = Vector3();
        lastPlayerPosition = Vector3();
        currentAction = 0;
        targetGUID = 0;
    }
};

class MovementController {
private:
    static std::unique_ptr<MovementController> s_instance;
    
    // Core dependencies
    ObjectManager* m_objectManager;
    HandlePlayerClickToMoveFn m_handlePlayerClickToMoveFunc;
    
    // Command queue for safe execution during EndScene
    std::queue<MovementCommand> m_commandQueue;
    mutable std::mutex m_queueMutex;
    
    // Movement state
    MovementState m_state;
    
    // Settings
    float m_movementTimeout;
    bool m_enableLogging;
    bool m_autoStop;
    
    // Internal methods
    void UpdateMovementState();
    void LogMovementAction(const std::string& action, const Vector3& position, uint64_t guid = 0);
    void QueueCommand(const MovementCommand& command);
    bool ExecuteCommand(const MovementCommand& command);
    void LogCTMState(const std::string& context);
    
    // Hooking for state management
    static void __fastcall OnMovementStopHook(void* ecx, void* edx, bool arg1, bool arg2);

public:
    MovementController();
    ~MovementController() = default;
    MovementController(const MovementController&) = delete;
    MovementController& operator=(const MovementController&) = delete;
    
    // Singleton access
    static MovementController& GetInstance();
    static void Initialize(ObjectManager* objectManager);
    static void Shutdown();
    
    // Core initialization
    void SetObjectManager(ObjectManager* objectManager) { m_objectManager = objectManager; }
    
    // Main movement functions
    bool GetPlayerPosition(Vector3& outPosition);
    bool ClickToMove(const Vector3& targetPos);
    bool ClickToMove(const Vector3& targetPos, const Vector3& playerPos);
    bool MoveToObject(uint64_t targetGuid);
    bool InteractWithObject(uint64_t targetGuid);
    bool AttackTarget(uint64_t targetGuid);
    
    // Utility functions
    void Stop();
    void FaceTarget(uint64_t targetGuid);
    void FacePosition(const Vector3& position);
    void RightClickAt(const Vector3& targetPos);
    
    // State queries
    bool IsMoving() const { return m_state.isMoving; }
    bool HasDestination() const { return m_state.hasDestination; }
    Vector3 GetCurrentDestination() const { return m_state.currentDestination; }
    uint32_t GetCurrentAction() const { return m_state.currentAction; }
    uint64_t GetTargetGUID() const { return m_state.targetGUID; }
    const MovementState& GetState() const { return m_state; }
    
    // Settings
    void SetMovementTimeout(float seconds) { m_movementTimeout = seconds; }
    void SetLoggingEnabled(bool enabled) { m_enableLogging = enabled; }
    void SetAutoStopEnabled(bool enabled) { m_autoStop = enabled; }
    
    float GetMovementTimeout() const { return m_movementTimeout; }
    bool IsLoggingEnabled() const { return m_enableLogging; }
    bool IsAutoStopEnabled() const { return m_autoStop; }
    
    // Update method (call from main loop)
    void Update(float deltaTime);
    
    // Process queued commands (call from EndScene hook)
    void ProcessQueuedCommands();
    
    // Diagnostics
    bool IsInitialized() const { return m_objectManager != nullptr && m_handlePlayerClickToMoveFunc != nullptr; }
    std::string GetStatusString() const;
}; 