#pragma once

#include <d3d9.h>
#include <functional>
#include "types/types.h"

// Function pointer type for EndScene
typedef HRESULT(__stdcall* EndSceneFn)(IDirect3DDevice9* device);

// Function pointer type for Reset
typedef HRESULT(__stdcall* ResetFn)(IDirect3DDevice9* device, D3DPRESENT_PARAMETERS* pPresentationParameters);

// Global function pointers for original functions
extern EndSceneFn g_OriginalEndScene;
extern ResetFn g_OriginalReset;

// Global shutdown flag
extern volatile bool g_shutdownRequested;

// Hook initialization and management
bool InitializeHooks();
void ShutdownHooks();
void CleanupHook(bool isForceTermination = false);

// EndScene submission for thread-safe operations
void SubmitToEndScene(std::function<void()> func); 