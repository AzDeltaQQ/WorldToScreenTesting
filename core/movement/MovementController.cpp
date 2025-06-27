#include "MovementController.h"
#include "../objects/ObjectManager.h"
#include "../objects/WowPlayer.h"
#include "../memory/memory.h"
#include "../types/types.h"
#include "../../dependencies/MinHook/include/MinHook.h"
#include <sstream>
#include <iomanip>
#include <cmath>

// Static member definition
std::unique_ptr<MovementController> MovementController::s_instance = nullptr;

// Global hook handle for reset_tracking_and_input
static ResetTrackingAndInputFn g_originalResetTrackingFunc = nullptr;

MovementController::MovementController()
    : m_objectManager(nullptr)
    , m_handlePlayerClickToMoveFunc(nullptr)
    , m_movementTimeout(10.0f)
    , m_enableLogging(true)
    , m_autoStop(true)
{
    m_state.Reset();
}

MovementController& MovementController::GetInstance() {
    if (!s_instance) {
        s_instance = std::make_unique<MovementController>();
    }
    return *s_instance;
}

void MovementController::Initialize(ObjectManager* objectManager) {
    GetInstance().SetObjectManager(objectManager);
    
    // Initialize the handlePlayerClickToMove function pointer
    auto& instance = GetInstance();
    instance.m_handlePlayerClickToMoveFunc = reinterpret_cast<HandlePlayerClickToMoveFn>(WoWFunctions::HANDLE_PLAYER_CLICK_TO_MOVE);
    
    // The main hook manager in dllmain should handle MH_Initialize
    // We just create our specific hook here.
    MH_STATUS status = MH_CreateHook(
        reinterpret_cast<void*>(WoWFunctions::RESET_TRACKING_AND_INPUT),
        &MovementController::OnMovementStopHook,
        reinterpret_cast<void**>(&g_originalResetTrackingFunc)
    );
    
    if (status == MH_OK) {
        status = MH_EnableHook(reinterpret_cast<void*>(WoWFunctions::RESET_TRACKING_AND_INPUT));
        if (status == MH_OK) {
            LOG_INFO("Movement completion hook installed successfully");
        } else {
            LOG_ERROR("Failed to enable movement completion hook: " + std::to_string(status));
        }
    } else {
        LOG_ERROR("Failed to create movement completion hook: " + std::to_string(status));
    }
    
    LOG_INFO("MovementController initialized with handlePlayerClickToMove");
}

void MovementController::Shutdown() {
    if (s_instance) {
        s_instance->Stop(); // Stop any ongoing movement
        
        // MinHook is managed globally now, so we don't uninitialize it here.
        // We might need to remove specific hooks if this shutdown is isolated.
        if (g_originalResetTrackingFunc) {
            MH_DisableHook(reinterpret_cast<void*>(WoWFunctions::RESET_TRACKING_AND_INPUT));
            // MH_Uninitialize is no longer called here.
            g_originalResetTrackingFunc = nullptr;
            LOG_INFO("MovementController hook disabled");
        }
        
        s_instance.reset();
        LOG_INFO("MovementController shutdown");
    }
}

// This is the function that gets called when the game resets CTM
void __fastcall MovementController::OnMovementStopHook(void* ecx, void* edx, bool arg1, bool arg2) {
    auto& controller = MovementController::GetInstance();
    if (controller.IsMoving()) {
        if (controller.IsLoggingEnabled()) {
            LOG_INFO("Movement completed (hook triggered). Resetting state.");
        }
        controller.m_state.Reset();
    }
    
    // Call the original function
    if (g_originalResetTrackingFunc) {
        g_originalResetTrackingFunc(ecx, edx, arg1, arg2);
    }
}

bool MovementController::GetPlayerPosition(Vector3& outPosition) {
    if (!m_objectManager || !m_objectManager->IsInitialized()) {
        return false;
    }
    
    try {
        auto localPlayer = m_objectManager->GetLocalPlayer();
        if (!localPlayer) {
            return false;
        }
        
        Vector3 pos = localPlayer->GetPosition();
        outPosition = Vector3(pos.x, pos.y, pos.z);
        return true;
    } catch (const MemoryAccessError& e) {
        LOG_ERROR("Memory error getting player position: " + std::string(e.what()));
        return false;
    } catch (...) {
        LOG_ERROR("Unknown error getting player position");
        return false;
    }
}

void MovementController::LogMovementAction(const std::string& action, const Vector3& position, uint64_t guid) {
    if (!m_enableLogging) {
        return;
    }
    
    std::string logMessage = "Movement: " + action + 
                           " Position: (" + std::to_string(position.x) + ", " + 
                           std::to_string(position.y) + ", " + std::to_string(position.z) + ")";
    
    if (guid != 0) {
        logMessage += " GUID: 0x" + std::to_string(guid);
    }
    
    LOG_INFO(logMessage);
}

bool MovementController::ClickToMove(const Vector3& targetPos) {
    Vector3 playerPos;
    if (!GetPlayerPosition(playerPos)) {
        LOG_ERROR("Failed to get player position for Click-to-Move");
        return false;
    }
    
    return ClickToMove(targetPos, playerPos);
}

bool MovementController::ClickToMove(const Vector3& targetPos, const Vector3& playerPos) {
    if (!IsInitialized()) {
        LOG_ERROR("MovementController not properly initialized");
        return false;
    }
    
    if (m_enableLogging) {
        LogMovementAction("ClickToMove (queued)", targetPos);
    }
    
    // Queue the command for safe execution during EndScene
    QueueCommand(MovementCommand(MovementCommandType::MOVE_TO_TERRAIN, targetPos));
    return true;
}

bool MovementController::MoveToObject(uint64_t targetGuid) {
    if (!IsInitialized()) {
        LOG_ERROR("MovementController not properly initialized");
        return false;
    }
    
    if (m_enableLogging) {
        LOG_INFO("MoveToObject (queued) called with GUID: 0x" + std::to_string(targetGuid));
    }
    
    // Queue the command for safe execution during EndScene
    QueueCommand(MovementCommand(MovementCommandType::MOVE_TO_OBJECT, Vector3(), targetGuid));
    return true;
}

bool MovementController::InteractWithObject(uint64_t targetGuid) {
    // Same as MoveToObject - handlePlayerClickToMove handles both movement and interaction
    return MoveToObject(targetGuid);
}

bool MovementController::AttackTarget(uint64_t targetGuid) {
    if (!IsInitialized()) {
        LOG_ERROR("MovementController not properly initialized");
        return false;
    }
    
    if (m_enableLogging) {
        LOG_INFO("AttackTarget (queued) called with GUID: 0x" + std::to_string(targetGuid));
    }
    
    // Queue the command for safe execution during EndScene
    QueueCommand(MovementCommand(MovementCommandType::ATTACK_TARGET, Vector3(), targetGuid));
    return true;
}

void MovementController::Stop() {
    if (!IsInitialized()) {
        return;
    }
    
    if (m_enableLogging) {
        LOG_INFO("Stop movement (queued)");
    }
    
    // Queue the stop command for safe execution during EndScene
    QueueCommand(MovementCommand(MovementCommandType::STOP));
    
    // Reset internal state immediately
    m_state.Reset();
}

void MovementController::FaceTarget(uint64_t targetGuid) {
    if (!IsInitialized()) {
        return;
    }
    
    if (m_enableLogging) {
        LOG_INFO("FaceTarget (queued) called with GUID: 0x" + std::to_string(targetGuid));
    }
    
    // Queue the command for safe execution during EndScene
    QueueCommand(MovementCommand(MovementCommandType::FACE_TARGET, Vector3(), targetGuid));
}

void MovementController::FacePosition(const Vector3& position) {
    if (!IsInitialized()) {
        return;
    }
    
    if (m_enableLogging) {
        LogMovementAction("FacePosition (queued)", position);
    }
    
    // Queue the command for safe execution during EndScene
    QueueCommand(MovementCommand(MovementCommandType::FACE_POSITION, position));
}

void MovementController::RightClickAt(const Vector3& targetPos) {
    // Right click is essentially the same as move to position
    ClickToMove(targetPos);
    
    if (m_enableLogging) {
        LogMovementAction("RightClickAt", targetPos);
    }
}

void MovementController::UpdateMovementState() {
    // Movement state is now managed by the hook, so this is simplified
    if (m_state.isMoving) {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - m_state.movementStartTime).count();
        
        if (elapsed > m_movementTimeout) {
            if (m_enableLogging) {
                LOG_WARNING("Movement timed out after " + std::to_string(elapsed) + " seconds");
            }
            m_state.Reset();
        }
    }
}

void MovementController::Update(float deltaTime) {
    if (!IsInitialized()) {
        return;
    }
    
    UpdateMovementState();
}

void MovementController::QueueCommand(const MovementCommand& command) {
    std::lock_guard<std::mutex> lock(m_queueMutex);
    m_commandQueue.push(command);
}

void MovementController::ProcessQueuedCommands() {
    if (!IsInitialized() || !m_handlePlayerClickToMoveFunc) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(m_queueMutex);
    
    if (m_state.isMoving) {
        return; // Busy walking – wait until current segment completes
    }

    if (!m_commandQueue.empty()) {
        const MovementCommand& command = m_commandQueue.front();
        LOG_INFO("ProcessQueuedCommands: About to execute command type " + std::to_string(static_cast<int>(command.type)));

        bool result = ExecuteCommand(command);

        LOG_INFO("ProcessQueuedCommands: Command execution " + std::string(result ? "succeeded" : "failed"));

        // Pop the command regardless of success to avoid stalls; caller can
        // re-queue if needed.
        m_commandQueue.pop();
    }
}

bool MovementController::ExecuteCommand(const MovementCommand& command) {
    try {
        // Get local player object pointer
        auto localPlayer = m_objectManager->GetLocalPlayer();
        if (!localPlayer) {
            LOG_ERROR("Local player not found for command execution!");
            return false;
        }
        
        void* playerPtr = reinterpret_cast<void*>(localPlayer->GetObjectPtr());
        if (!playerPtr) {
            LOG_ERROR("Invalid player object pointer!");
            return false;
        }
        
        switch (command.type) {
            case MovementCommandType::MOVE_TO_TERRAIN: {
                float coordinates[3] = { command.position.x, command.position.y, command.position.z };
                
                LOG_INFO("ExecuteCommand: About to call handlePlayerClickToMove for MOVE_TO_TERRAIN");
                std::string coordsStr = "ExecuteCommand: PlayerPtr=" + std::to_string(reinterpret_cast<uintptr_t>(playerPtr)) + 
                        ", Coords=(" + std::to_string(coordinates[0]) + "," + std::to_string(coordinates[1]) + "," + std::to_string(coordinates[2]) + ")";
                LOG_INFO(coordsStr);
                
                // Log CTM state before the call
                LogCTMState("BEFORE handlePlayerClickToMove");
                
                // For terrain movement, we still need a valid GUID pointer, but set to 0
                uint64_t nullGuid = 0;
                
                char result = m_handlePlayerClickToMoveFunc(
                    playerPtr,                    // ECX: Player object
                    nullptr,                      // EDX: unused
                    CTMActions::MOVE_TO_TERRAIN,  // Action type: Move to terrain
                    &nullGuid,                    // Pointer to null GUID (not nullptr!)
                    coordinates,                  // Target coordinates (3 floats)
                    0.0f                         // No specific facing angle
                );
                
                LOG_INFO("ExecuteCommand: handlePlayerClickToMove returned: " + std::to_string(result));
                
                // Log CTM state after the call
                LogCTMState("AFTER handlePlayerClickToMove");
                
                if (result) {
                    // Update internal state
                    m_state.currentDestination = command.position;
                    m_state.currentAction = CTMActions::MOVE_TO_TERRAIN;
                    m_state.targetGUID = 0;
                    m_state.isMoving = true;
                    m_state.hasDestination = true;
                    m_state.movementStartTime = std::chrono::steady_clock::now();
                    
                    if (m_enableLogging) {
                        LOG_INFO("Executed MOVE_TO_TERRAIN command successfully");
                    }
                    return true;
                } else {
                    LOG_ERROR("MOVE_TO_TERRAIN command failed");
                    return false;
                }
                break;
            }
            
            case MovementCommandType::MOVE_TO_OBJECT: {
                // Get target object to get its position
                auto targetObject = m_objectManager->GetObjectByGUID(command.targetGUID);
                if (!targetObject) {
                    LOG_ERROR("Target object not found for MOVE_TO_OBJECT!");
                    return false;
                }
                
                Vector3 targetPos = targetObject->GetPosition();
                float coordinates[3] = { targetPos.x, targetPos.y, targetPos.z };
                uint64_t guid = command.targetGUID;
                
                char result = m_handlePlayerClickToMoveFunc(
                    playerPtr,                    // ECX: Player object
                    nullptr,                      // EDX: unused
                    CTMActions::INTERACT_OBJECT,  // Action type: Interact with object
                    &guid,                        // Target GUID
                    coordinates,                  // Target coordinates
                    0.0f                         // No specific facing angle
                );
                
                if (result) {
                    // Update internal state
                    m_state.currentDestination = targetPos;
                    m_state.currentAction = CTMActions::INTERACT_OBJECT;
                    m_state.targetGUID = command.targetGUID;
                    m_state.isMoving = true;
                    m_state.hasDestination = true;
                    m_state.movementStartTime = std::chrono::steady_clock::now();
                    
                    if (m_enableLogging) {
                        LOG_INFO("Executed MOVE_TO_OBJECT command successfully");
                    }
                    return true;
                } else {
                    LOG_ERROR("MOVE_TO_OBJECT command failed");
                    return false;
                }
                break;
            }
            
            case MovementCommandType::ATTACK_TARGET: {
                // Get target object to get its position
                auto targetObject = m_objectManager->GetObjectByGUID(command.targetGUID);
                if (!targetObject) {
                    LOG_ERROR("Target object not found for ATTACK_TARGET!");
                    return false;
                }
                
                Vector3 targetPos = targetObject->GetPosition();
                float coordinates[3] = { targetPos.x, targetPos.y, targetPos.z };
                uint64_t guid = command.targetGUID;
                
                char result = m_handlePlayerClickToMoveFunc(
                    playerPtr,                    // ECX: Player object
                    nullptr,                      // EDX: unused
                    CTMActions::ATTACK_TARGET,    // Action type: Attack target
                    &guid,                        // Target GUID
                    coordinates,                  // Target coordinates
                    0.0f                         // No specific facing angle
                );
                
                if (result) {
                    // Update internal state
                    m_state.currentDestination = targetPos;
                    m_state.currentAction = CTMActions::ATTACK_TARGET;
                    m_state.targetGUID = command.targetGUID;
                    m_state.isMoving = true;
                    m_state.hasDestination = true;
                    m_state.movementStartTime = std::chrono::steady_clock::now();
                    
                    if (m_enableLogging) {
                        LOG_INFO("Executed ATTACK_TARGET command successfully");
                    }
                    return true;
                } else {
                    LOG_ERROR("ATTACK_TARGET command failed");
                    return false;
                }
                break;
            }
            
            case MovementCommandType::FACE_TARGET: {
                // Get target object
                auto targetObject = m_objectManager->GetObjectByGUID(command.targetGUID);
                if (!targetObject) {
                    LOG_ERROR("Target object not found for FACE_TARGET!");
                    return false;
                }
                
                Vector3 targetPos = targetObject->GetPosition();
                float coordinates[3] = { targetPos.x, targetPos.y, targetPos.z };
                uint64_t guid = command.targetGUID;
                
                m_handlePlayerClickToMoveFunc(
                    playerPtr,                    // ECX: Player object
                    nullptr,                      // EDX: unused
                    CTMActions::FACE_TARGET,      // Action type: Face target
                    &guid,                        // Target GUID
                    coordinates,                  // Target coordinates
                    0.0f                         // No specific facing angle
                );
                
                if (m_enableLogging) {
                    LOG_INFO("Executed FACE_TARGET command successfully");
                }
                return true;
                break;
            }
            
            case MovementCommandType::FACE_POSITION: {
                float coordinates[3] = { command.position.x, command.position.y, command.position.z };
                uint64_t nullGuid = 0;
                
                m_handlePlayerClickToMoveFunc(
                    playerPtr,                    // ECX: Player object
                    nullptr,                      // EDX: unused
                    CTMActions::FACE_TARGET,      // Action type: Face target (works for positions too)
                    &nullGuid,                    // Pointer to null GUID (not nullptr!)
                    coordinates,                  // Target coordinates
                    0.0f                         // No specific facing angle
                );
                
                if (m_enableLogging) {
                    LOG_INFO("Executed FACE_POSITION command successfully");
                }
                return true;
                break;
            }
            
            case MovementCommandType::STOP: {
                Vector3 currentPos = localPlayer->GetPosition();
                float coordinates[3] = { currentPos.x, currentPos.y, currentPos.z };
                uint64_t nullGuid = 0;
                
                m_handlePlayerClickToMoveFunc(
                    playerPtr,                    // ECX: Player object
                    nullptr,                      // EDX: unused
                    CTMActions::MOVE_TO_TERRAIN,  // Action type: Move to terrain (current position)
                    &nullGuid,                    // Pointer to null GUID (not nullptr!)
                    coordinates,                  // Current coordinates
                    0.0f                         // No specific facing angle
                );
                
                if (m_enableLogging) {
                    LOG_INFO("Executed STOP command successfully");
                }
                return true;
                break;
            }
        }
        
    } catch (const MemoryAccessError& e) {
        LOG_ERROR("Memory error executing command: " + std::string(e.what()));
        return false;
    } catch (...) {
        LOG_ERROR("Unknown error executing command");
        return false;
    }
    
    return false;
}

std::string MovementController::GetStatusString() const {
    std::stringstream ss;
    ss << "MovementController Status:\n";
    ss << "  Initialized: " << (IsInitialized() ? "Yes" : "No") << "\n";
    ss << "  Moving: " << (m_state.isMoving ? "Yes" : "No") << "\n";
    ss << "  Has Destination: " << (m_state.hasDestination ? "Yes" : "No") << "\n";
    ss << "  Current Action: " << m_state.currentAction << "\n";
    ss << "  Target GUID: 0x" << std::hex << m_state.targetGUID << "\n";
    ss << "  Destination: (" << m_state.currentDestination.x << ", " 
       << m_state.currentDestination.y << ", " << m_state.currentDestination.z << ")\n";
    ss << "  Logging Enabled: " << (m_enableLogging ? "Yes" : "No") << "\n";
    ss << "  Movement Timeout: " << m_movementTimeout << " seconds\n";
    
    // Add queue status
    std::lock_guard<std::mutex> lock(m_queueMutex);
    ss << "  Queued Commands: " << m_commandQueue.size() << "\n";
    
    return ss.str();
}

void MovementController::LogCTMState(const std::string& context) {
    try {
        // Read CTM global state variables
        int* ctmActionType = reinterpret_cast<int*>(CTMGlobals::G_CTM_ACTION_TYPE);
        uint64_t* ctmTargetGuid = reinterpret_cast<uint64_t*>(CTMGlobals::G_CTM_TARGET_GUID);
        float* ctmTargetPos = reinterpret_cast<float*>(CTMGlobals::G_CTM_TARGET_POS);
        float* ctmFacing = reinterpret_cast<float*>(CTMGlobals::G_CTM_FACING);
        float* ctmStopDistance = reinterpret_cast<float*>(CTMGlobals::G_CTM_STOP_DISTANCE);
        uint32_t* ctmStartTime = reinterpret_cast<uint32_t*>(CTMGlobals::G_CTM_START_TIME);
        
        std::stringstream ss;
        ss << "CTM State [" << context << "]:";
        ss << " ActionType=" << *ctmActionType;
        ss << " TargetGUID=0x" << std::hex << *ctmTargetGuid << std::dec;
        ss << " TargetPos=(" << ctmTargetPos[0] << "," << ctmTargetPos[1] << "," << ctmTargetPos[2] << ")";
        ss << " Facing=" << *ctmFacing;
        ss << " StopDistance=" << *ctmStopDistance;
        ss << " StartTime=" << *ctmStartTime;
        
        LOG_INFO(ss.str());
        
    } catch (...) {
        LOG_ERROR("Failed to read CTM state for context: " + context);
    }
}
