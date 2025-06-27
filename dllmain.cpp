#include <Windows.h>
#include <thread>
#include "core/logs/Logger.h"
#include "core/hook.h"
#include "core/drawing/drawing.h"
#include "core/objects/ObjectManager.h"
#include "core/movement/MovementController.h"
#include "core/navigation/NavigationManager.h"

// Global instance
extern WorldToScreenManager g_WorldToScreenManager;
extern volatile bool g_shutdownRequested; // Use the global flag from hook.cpp

// Module handle
static HMODULE g_hModule = NULL;

// Thread tracking
static HANDLE g_initThread = nullptr;
static bool g_initialized = false;

// Thread function for delayed initialization
DWORD WINAPI DelayedInitialization(LPVOID lpParam) {
    LOG_INFO("Starting delayed initialization thread...");
    
    // Wait in smaller chunks to allow for early termination if needed
    for (int i = 0; i < 50; i++) { // 5 seconds total (50 * 100ms)
        Sleep(100);
        
        // Check if shutdown was requested
        if (g_shutdownRequested) {
            LOG_INFO("Shutdown requested, aborting initialization");
            return 0;
        }
        
        // Check if the process is shutting down
        if (GetModuleHandle(L"kernel32.dll") == NULL) {
            LOG_INFO("Process shutting down, aborting initialization");
            return 0;
        }
    }
    
    // Final shutdown check before initializing
    if (g_shutdownRequested) {
        LOG_INFO("Shutdown requested after delay, aborting");
        return 0;
    }
    
    LOG_INFO("Delay complete, initializing systems...");

    MovementController::Initialize(ObjectManager::GetInstance());
    Navigation::NavigationManager::SetModuleHandle(g_hModule);
    Navigation::NavigationManager::Instance().Initialize();
    
    LOG_INFO("Delay complete, initializing hooks...");
    
    // Initialize hooks with extensive safety checks
    try {
        // Verify we're still in a valid process state
        HMODULE hKernel32 = GetModuleHandle(L"kernel32.dll");
        if (!hKernel32) {
            LOG_ERROR("kernel32.dll not available");
            return 0;
        }
        
        // Try to initialize hooks with retry logic
        bool initSuccess = false;
        for (int attempt = 1; attempt <= 3 && !g_shutdownRequested; attempt++) {
            LOG_INFO("Hook initialization attempt " + std::to_string(attempt) + "/3");
            
            if (InitializeHooks()) {
                LOG_INFO("Hooks initialized successfully!");
                initSuccess = true;
                g_initialized = true;
                break;
            } else {
                LOG_ERROR("Hook initialization attempt " + std::to_string(attempt) + " failed");
                
                if (attempt < 3 && !g_shutdownRequested) {
                    LOG_INFO("Waiting before retry...");
                    // Wait in smaller chunks to allow for shutdown
                    for (int j = 0; j < 20 && !g_shutdownRequested; j++) {
                        Sleep(100); // 2 seconds total
                    }
                }
            }
        }
        
        if (!initSuccess) {
            LOG_ERROR("All hook initialization attempts failed");
        }
    }
    catch (...) {
        LOG_ERROR("Exception during delayed initialization");
    }
    
    LOG_INFO("Delayed initialization thread ending");
    return 0;
}

// Safe Logger initialization without exception handling
static BOOL SafeInitializeLogger() {
    // Just call Logger::Initialize() directly - it now has internal exception handling
    Logger::Initialize();
    return TRUE;
}

// Safe Logger shutdown without exception handling  
static void SafeShutdownLogger() {
    // Just call Logger::Shutdown() directly - it should be safe
    Logger::Shutdown();
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    switch (ul_reason_for_call) {
        case DLL_PROCESS_ATTACH:
        {
            // Initialize logger safely
            if (!SafeInitializeLogger()) {
                return FALSE;
            }
            
            LOG_INFO("DLL_PROCESS_ATTACH started");
            DisableThreadLibraryCalls(hModule);
            
            g_hModule = hModule;

            // Reset shutdown flag
            g_shutdownRequested = false;
            
            // Create initialization thread
            g_initThread = CreateThread(nullptr, 0, DelayedInitialization, nullptr, 0, nullptr);
            
            if (!g_initThread) {
                LOG_ERROR("Failed to create initialization thread!");
                return FALSE;
            }
            
            LOG_INFO("Initialization thread created successfully");
            break;
        }
        
        case DLL_PROCESS_DETACH:
        {
            LOG_INFO("DLL_PROCESS_DETACH received");
            
            // Signal shutdown to any running threads
            g_shutdownRequested = true;
            
            // Close init thread handle without waiting (waiting can deadlock under loader lock)
            if (g_initThread) {
                CloseHandle(g_initThread);
                g_initThread = nullptr;
            }
            
            // Spawn a detached thread to perform cleanup outside loader lock
            HANDLE hCleanup = CreateThread(nullptr, 0, [](LPVOID lp)->DWORD {
                bool isTerminating = (lp != nullptr);
                CleanupHook(isTerminating);
                SafeShutdownLogger();
                return 0;
            }, lpReserved, 0, nullptr);
            if (hCleanup) {
                CloseHandle(hCleanup); // We don't need to wait
            }
            
            break;
        }
        
        case DLL_THREAD_ATTACH:
        case DLL_THREAD_DETACH:
            break;
    }
    return TRUE;
} 