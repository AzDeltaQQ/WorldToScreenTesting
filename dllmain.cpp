#include <Windows.h>
#include "core/hook.h"
#include "core/worldtoscreen.h"

// Global instance
extern WorldToScreenManager g_WorldToScreenManager;

// Timer for delayed initialization
static DWORD g_initTimer = 0;
static bool g_initialized = false;

// Thread function for delayed initialization
DWORD WINAPI DelayedInitialization(LPVOID lpParam) {
    // Wait a bit for the process to stabilize
    Sleep(2000); // Increased delay to allow WoW to fully load
    
    // Initialize hooks with safety checks
    try {
        if (InitializeHooks()) {
            // No need to add test arrows - Update() will automatically add player arrow
            g_initialized = true;
        }
    }
    catch (...) {
        // Handle any initialization errors silently
    }
    
    return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    switch (ul_reason_for_call) {
    case DLL_PROCESS_ATTACH:
        {
            // Disable DLL_THREAD_ATTACH and DLL_THREAD_DETACH notifications
            DisableThreadLibraryCalls(hModule);
            
            // Create a thread for delayed initialization to avoid DLL_PROCESS_ATTACH restrictions
            HANDLE hThread = CreateThread(nullptr, 0, DelayedInitialization, hModule, 0, nullptr);
            if (hThread) {
                CloseHandle(hThread); // We don't need to wait for it
            }
            break;
        }
        
    case DLL_PROCESS_DETACH:
        {
            // Cleanup only if we were initialized
            if (g_initialized) {
                __try {
                    ShutdownHooks();
                }
                __except(EXCEPTION_EXECUTE_HANDLER) {
                    // Handle cleanup errors silently
                }
            }
            break;
        }
    }
    return TRUE;
} 