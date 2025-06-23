#include <Windows.h>
#include "core/hook.h"

// Global module handle
HMODULE g_hModule = NULL;

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    switch (ul_reason_for_call) {
        case DLL_PROCESS_ATTACH:
        {
            OutputDebugStringA("WorldToScreen Test: DLL_PROCESS_ATTACH\n");
            g_hModule = hModule;
            
            // Disable thread library calls to avoid issues
            DisableThreadLibraryCalls(hModule);
            
            // Initialize hooks in separate thread
            HANDLE hookThread = CreateThread(nullptr, 0, InitializeHook, hModule, 0, nullptr);
            if (hookThread) {
                CloseHandle(hookThread);
            } else {
                OutputDebugStringA("WorldToScreen Test: Failed to create hook thread!\n");
            }
            break;
        }
        
        case DLL_PROCESS_DETACH:
        {
            OutputDebugStringA("WorldToScreen Test: DLL_PROCESS_DETACH\n");
            CleanupHook();
            break;
        }
        
        case DLL_THREAD_ATTACH:
        case DLL_THREAD_DETACH:
            break;
    }
    return TRUE;
} 