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
    // Update WorldToScreen arrows
    g_WorldToScreenManager.Update();
    
    // Call original EndScene
    return g_OriginalEndScene(device);
}

bool InitializeHooks() {
    if (g_HooksInitialized) {
        return true;
    }

    // Initialize MinHook
    if (MH_Initialize() != MH_OK) {
        return false;
    }

    // Get D3D9 device
    IDirect3D9* d3d9 = Direct3DCreate9(D3D_SDK_VERSION);
    if (!d3d9) {
        MH_Uninitialize();
        return false;
    }

    D3DPRESENT_PARAMETERS pp = {};
    pp.Windowed = TRUE;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.hDeviceWindow = GetDesktopWindow();

    IDirect3DDevice9* device = nullptr;
    HRESULT hr = d3d9->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, pp.hDeviceWindow,
        D3DCREATE_SOFTWARE_VERTEXPROCESSING, &pp, &device);

    if (FAILED(hr) || !device) {
        d3d9->Release();
        MH_Uninitialize();
        return false;
    }

    // Get EndScene function address from vtable
    void** vtable = *reinterpret_cast<void***>(device);
    void* endSceneAddr = vtable[42]; // EndScene is at index 42 in D3D9 vtable

    // Clean up temporary D3D objects
    device->Release();
    d3d9->Release();

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

void ShutdownHooks() {
    if (!g_HooksInitialized) {
        return;
    }

    // Clear all arrows
    g_WorldToScreenManager.ClearAllArrows();
    
    // Disable and remove hooks
    MH_DisableHook(MH_ALL_HOOKS);
    MH_Uninitialize();

    g_HooksInitialized = false;
} 