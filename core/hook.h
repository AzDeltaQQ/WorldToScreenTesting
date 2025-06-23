#pragma once

#include <Windows.h>
#include <d3d9.h>
#include <atomic>
#include <functional>
#include <vector>
#include <mutex>

// Global module handle
extern HMODULE g_hModule;

// Global shutdown flag
extern std::atomic<bool> g_isShuttingDown;

// Core hook functions
DWORD WINAPI InitializeHook(LPVOID lpParam);
HRESULT APIENTRY HookedEndScene(LPDIRECT3DDEVICE9 pDevice);
void CleanupHook();

// EndScene submission system (same pattern as your main project)
void SubmitToEndScene(std::function<void()> func);

// WorldToScreen testing functions
namespace WorldToScreen {
    void Initialize();
    void Shutdown();
    bool TestWorldToScreen(float worldX, float worldY, float worldZ, float& screenX, float& screenY);
    void RenderTestGUI();
} 