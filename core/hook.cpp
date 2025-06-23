#include "hook.h"
#include "worldtoscreen.h"
#include <Windows.h>
#include <d3d9.h>
#include <iostream>

// MinHook
#include "../dependencies/MinHook/include/MinHook.h"

// Global variables
static WNDPROC g_OriginalWndProc = nullptr;
static EndSceneFn g_OriginalEndScene = nullptr;
static bool g_HooksInitialized = false;

// Forward declarations
extern WorldToScreenManager g_WorldToScreenManager;

HRESULT __stdcall HookedEndScene(IDirect3DDevice9* device) {
    __try {
        // Only update if safely initialized
        if (g_HooksInitialized) {
            g_WorldToScreenManager.Update();
        }
    } 
    __except(EXCEPTION_EXECUTE_HANDLER) {
        // Handle any exceptions silently to avoid crashes
    }
    
    // Call original EndScene
    return g_OriginalEndScene(device);
}

bool InitializeHooks() {
    if (g_HooksInitialized) {
        return true;
    }

    __try {
        // Initialize MinHook
        if (MH_Initialize() != MH_OK) {
            return false;
        }

        // Get WoW window handle - try multiple approaches
        HWND wowWindow = nullptr;
        
        // Method 1: Try known WoW window classes
        const char* wowClasses[] = {
            "GxWindowClass",
            "GxWindowClassD3d", 
            "World of Warcraft",
            "Warcraft III"
        };
        
        for (const char* className : wowClasses) {
            wowWindow = FindWindowA(className, nullptr);
            if (wowWindow) break;
        }
        
        // Method 2: If still not found, use current process window
        if (!wowWindow) {
            wowWindow = GetActiveWindow();
        }
        
        // Method 3: Last resort - desktop window
        if (!wowWindow) {
            wowWindow = GetDesktopWindow();
        }

        // Create a minimal D3D9 device to get EndScene address
        IDirect3D9* d3d9 = Direct3DCreate9(D3D_SDK_VERSION);
        if (!d3d9) {
            MH_Uninitialize();
            return false;
        }

        // Simple presentation parameters
        D3DPRESENT_PARAMETERS pp = {};
        pp.Windowed = TRUE;
        pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
        pp.hDeviceWindow = wowWindow;
        pp.BackBufferFormat = D3DFMT_UNKNOWN;
        pp.BackBufferWidth = 1;
        pp.BackBufferHeight = 1;
        pp.EnableAutoDepthStencil = FALSE;

        IDirect3DDevice9* device = nullptr;
        HRESULT hr = d3d9->CreateDevice(
            D3DADAPTER_DEFAULT, 
            D3DDEVTYPE_HAL, 
            wowWindow,
            D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_NOWINDOWCHANGES,
            &pp, 
            &device
        );

        if (FAILED(hr) || !device) {
            d3d9->Release();
            MH_Uninitialize();
            return false;
        }

        // Get EndScene address from vtable
        void** vtable = *reinterpret_cast<void***>(device);
        void* endSceneAddr = vtable[42]; // EndScene is at index 42

        // Clean up temporary objects
        device->Release();
        d3d9->Release();

        if (!endSceneAddr) {
            MH_Uninitialize();
            return false;
        }

        // Hook EndScene
        if (MH_CreateHook(endSceneAddr, &HookedEndScene, reinterpret_cast<LPVOID*>(&g_OriginalEndScene)) != MH_OK) {
            MH_Uninitialize();
            return false;
        }

        if (MH_EnableHook(endSceneAddr) != MH_OK) {
            MH_Uninitialize();
            return false;
        }

        g_HooksInitialized = true;
        return true;
    }
    __except(EXCEPTION_EXECUTE_HANDLER) {
        // Handle any exceptions during initialization
        MH_Uninitialize();
        return false;
    }
}

void ShutdownHooks() {
    if (!g_HooksInitialized) {
        return;
    }

    __try {
        // Clear all arrows first
        g_WorldToScreenManager.ClearAllArrows();
        
        // Disable and remove hooks
        MH_DisableHook(MH_ALL_HOOKS);
        MH_Uninitialize();
    } 
    __except(EXCEPTION_EXECUTE_HANDLER) {
        // Handle cleanup errors silently
    }

    g_HooksInitialized = false;
} 