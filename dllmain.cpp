#include <Windows.h>
#include "core/hook.h"
#include "core/worldtoscreen.h"

// Global instance
extern WorldToScreenManager g_WorldToScreenManager;

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    switch (ul_reason_for_call) {
    case DLL_PROCESS_ATTACH:
        // Disable DLL_THREAD_ATTACH and DLL_THREAD_DETACH notifications
        DisableThreadLibraryCalls(hModule);
        
        // Initialize hooks
        if (InitializeHooks()) {
            // Add some test arrows
            g_WorldToScreenManager.AddArrow(0.0f, 0.0f, 0.0f, "Origin");
            g_WorldToScreenManager.AddArrow(5.0f, 0.0f, 0.0f, "East 5");
            g_WorldToScreenManager.AddArrow(0.0f, 5.0f, 0.0f, "North 5");
            g_WorldToScreenManager.AddArrow(0.0f, 0.0f, 2.0f, "Up 2");
        }
        break;
        
    case DLL_PROCESS_DETACH:
        // Cleanup
        ShutdownHooks();
        break;
    }
    return TRUE;
} 