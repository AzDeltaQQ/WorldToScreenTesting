#include <Windows.h>
#include <d3d9.h>
#pragma comment(lib, "d3d9.lib")

#include "hook.h"
#include "types.h"
#include "worldtoscreen.h"

// Define IMGUI_DISABLE_OBSOLETE_FUNCTIONS before including ImGui
#define IMGUI_DISABLE_OBSOLETE_FUNCTIONS
#include "../dependencies/ImGui/imgui.h"
#include "../dependencies/ImGui/backends/imgui_impl_win32.h"
#include "../dependencies/ImGui/backends/imgui_impl_dx9.h"
#include <MinHook.h>

// Declare the exported C functions
extern "C" {
    void UpdateArrowsEndScene();
    void RenderArrowsEndScene();
}

// Global state
HWND gameHwnd = NULL;

// Global shutdown flag definition
std::atomic<bool> g_isShuttingDown{false};

// EndScene submission queue
namespace {
    std::vector<std::function<void()>> endSceneQueue;
    std::mutex endSceneMutex;
}

void SubmitToEndScene(std::function<void()> func) {
    if (g_isShuttingDown.load()) {
        return;
    }

    std::lock_guard<std::mutex> lock(endSceneMutex);
    if (g_isShuttingDown.load()) {
        return;
    }
    
    endSceneQueue.push_back(std::move(func));
}

// Original function pointers
typedef HRESULT(APIENTRY* EndScene)(LPDIRECT3DDEVICE9 pDevice);
typedef HRESULT(APIENTRY* Reset)(LPDIRECT3DDEVICE9 pDevice, D3DPRESENT_PARAMETERS* pPresentationParameters);

EndScene oEndScene = nullptr;
Reset oReset = nullptr;

// ImGui state
bool imguiInitialized = false;

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
WNDPROC oWndProc = NULL;

LRESULT APIENTRY WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    // Handle key toggle for GUI (INSERT key)
    if (uMsg == WM_KEYDOWN && wParam == VK_INSERT) {
        // Toggle GUI visibility (we'll implement this later)
        return TRUE;
    }
    
    // Handle ImGui input
    if (imguiInitialized) {
        if (ImGui_ImplWin32_WndProcHandler(hwnd, uMsg, wParam, lParam)) {
            return TRUE;
        }
        
        ImGuiIO& io = ImGui::GetIO();
        if (io.WantCaptureMouse || io.WantCaptureKeyboard) {
            if ((uMsg >= WM_KEYFIRST && uMsg <= WM_KEYLAST) && io.WantCaptureKeyboard) {
                return TRUE;
            }
            if ((uMsg >= WM_MOUSEFIRST && uMsg <= WM_MOUSELAST) && io.WantCaptureMouse) {
                return TRUE;
            }
        }
    }
    
    return CallWindowProc(oWndProc, hwnd, uMsg, wParam, lParam);
}

HRESULT APIENTRY HookedEndScene(LPDIRECT3DDEVICE9 pDevice) {
    if (!imguiInitialized) {
        static int initCounter = 0;
        if (++initCounter < 100) { // Wait for stability
            return oEndScene(pDevice);
        }
        
        OutputDebugStringA("Arrow Renderer: Initializing ImGui...\n");
        
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
        io.IniFilename = NULL; // Disable imgui.ini
        
        // Get game window handle
        D3DDEVICE_CREATION_PARAMETERS params;
        if (FAILED(pDevice->GetCreationParameters(&params))) {
            OutputDebugStringA("Arrow Renderer: Failed to get device creation parameters\n");
            ImGui::DestroyContext();
            return oEndScene(pDevice);
        }
        gameHwnd = params.hFocusWindow;
        
        if (!gameHwnd) {
            OutputDebugStringA("Arrow Renderer: Failed to get valid game HWND\n");
            ImGui::DestroyContext();
            return oEndScene(pDevice);
        }
        
        // Initialize ImGui backends
        if (!ImGui_ImplWin32_Init(gameHwnd)) {
            OutputDebugStringA("Arrow Renderer: Failed to initialize ImGui Win32 backend\n");
            ImGui::DestroyContext();
            return oEndScene(pDevice);
        }
        
        if (!ImGui_ImplDX9_Init(pDevice)) {
            OutputDebugStringA("Arrow Renderer: Failed to initialize ImGui DX9 backend\n");
            ImGui_ImplWin32_Shutdown();
            ImGui::DestroyContext();
            return oEndScene(pDevice);
        }
        
        // Hook window procedure
        oWndProc = (WNDPROC)SetWindowLongPtr(gameHwnd, GWLP_WNDPROC, (LONG_PTR)WndProc);
        
        imguiInitialized = true;
        OutputDebugStringA("Arrow Renderer: ImGui initialization complete\n");
        
        // Initialize arrow rendering system
        WorldToScreen::Initialize();
    }
    
    // Execute EndScene queue first
    {
        std::lock_guard<std::mutex> lock(endSceneMutex);
        for (auto& func : endSceneQueue) {
            try {
                func();
            } catch (...) {
                // Silently handle exceptions
            }
        }
        endSceneQueue.clear();
    }

    // Use the exported C functions for safety
    UpdateArrowsEndScene();

    // Render ImGui
    if (imguiInitialized) {
        ImGui_ImplDX9_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        
        // Render everything through the safe exported function
        RenderArrowsEndScene();
        
        ImGui::EndFrame();
        ImGui::Render();
        ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
    }

    return oEndScene(pDevice);
}

HRESULT APIENTRY HookedReset(LPDIRECT3DDEVICE9 pDevice, D3DPRESENT_PARAMETERS* pPresentationParameters) {
    if (imguiInitialized) {
        ImGui_ImplDX9_InvalidateDeviceObjects();
    }
    
    HRESULT result = oReset(pDevice, pPresentationParameters);
    
    if (imguiInitialized && SUCCEEDED(result)) {
        ImGui_ImplDX9_CreateDeviceObjects();
    }
    
    return result;
}

DWORD WINAPI InitializeHook(LPVOID lpParam) {
    OutputDebugStringA("Arrow Renderer: InitializeHook thread started\n");
    
    HMODULE hModule = (HMODULE)lpParam;
    
    OutputDebugStringA("Arrow Renderer: Starting hook initialization...\n");
    
    if (MH_Initialize() != MH_OK) {
        OutputDebugStringA("Arrow Renderer: MH_Initialize failed!\n");
        return 0;
    }
    OutputDebugStringA("Arrow Renderer: MinHook initialized\n");
    
    // Find WoW window for D3D device creation
    HWND tempHwnd = FindWindowA("GxWindowClass", NULL);
    if (!tempHwnd) tempHwnd = GetDesktopWindow();
    
    LPDIRECT3D9 pD3D = Direct3DCreate9(D3D_SDK_VERSION);
    if (!pD3D) {
        OutputDebugStringA("Arrow Renderer: Failed to create D3D9 interface\n");
        MH_Uninitialize();
        return 0;
    }
    
    D3DPRESENT_PARAMETERS d3dpp = {0};
    d3dpp.Windowed = TRUE;
    d3dpp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    d3dpp.hDeviceWindow = tempHwnd;
    
    LPDIRECT3DDEVICE9 pDevice = nullptr;
    HRESULT result = pD3D->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, tempHwnd,
                                      D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_DISABLE_DRIVER_MANAGEMENT,
                                      &d3dpp, &pDevice);
    
    if (FAILED(result) || !pDevice) {
        OutputDebugStringA("Arrow Renderer: Failed to create D3D9 device\n");
        pD3D->Release();
        MH_Uninitialize();
        return 0;
    }
    
    void** vTable = *reinterpret_cast<void***>(pDevice);
    if (!vTable) {
        OutputDebugStringA("Arrow Renderer: Failed to get device vtable\n");
        pDevice->Release();
        pD3D->Release();
        MH_Uninitialize();
        return 0;
    }
    
    void* targetEndScene = vTable[42];
    void* targetReset = vTable[16];
    
    // Create hooks
    MH_STATUS mhStatus = MH_CreateHook(
        reinterpret_cast<LPVOID>(targetEndScene),
        reinterpret_cast<LPVOID>(&HookedEndScene),
        reinterpret_cast<LPVOID*>(&oEndScene)
    );
    if (mhStatus != MH_OK) {
        OutputDebugStringA("Arrow Renderer: MH_CreateHook for EndScene failed!\n");
        pDevice->Release();
        pD3D->Release();
        MH_Uninitialize();
        return 0;
    }
    
    mhStatus = MH_CreateHook(
        reinterpret_cast<LPVOID>(targetReset),
        reinterpret_cast<LPVOID>(&HookedReset),
        reinterpret_cast<LPVOID*>(&oReset)
    );
    if (mhStatus != MH_OK) {
        OutputDebugStringA("Arrow Renderer: MH_CreateHook for Reset failed!\n");
        MH_RemoveHook(targetEndScene);
        pDevice->Release();
        pD3D->Release();
        MH_Uninitialize();
        return 0;
    }
    
    // Enable hooks
    mhStatus = MH_EnableHook(targetEndScene);
    if (mhStatus != MH_OK) {
        OutputDebugStringA("Arrow Renderer: MH_EnableHook for EndScene failed!\n");
        MH_RemoveHook(targetReset);
        MH_RemoveHook(targetEndScene);
        pDevice->Release();
        pD3D->Release();
        MH_Uninitialize();
        return 0;
    }
    
    mhStatus = MH_EnableHook(targetReset);
    if (mhStatus != MH_OK) {
        OutputDebugStringA("Arrow Renderer: MH_EnableHook for Reset failed!\n");
        MH_DisableHook(targetEndScene);
        MH_RemoveHook(targetReset);
        MH_RemoveHook(targetEndScene);
        pDevice->Release();
        pD3D->Release();
        MH_Uninitialize();
        return 0;
    }
    
    pDevice->Release();
    pD3D->Release();
    OutputDebugStringA("Arrow Renderer: D3D Hooks placed and enabled. Dummy device released.\n");
    
    OutputDebugStringA("Arrow Renderer: Hook initialization complete\n");
    return 0;
}

void CleanupHook() {
    OutputDebugStringA("Arrow Renderer: Cleanup starting\n");
    
    // Set shutdown flag
    g_isShuttingDown.store(true);
    
    try {
        // Shutdown arrow rendering system
        WorldToScreen::Shutdown();
        
        if (imguiInitialized) {
            OutputDebugStringA("Arrow Renderer: Cleaning up ImGui...\n");
            
            // Restore original window procedure
            if (oWndProc && gameHwnd && IsWindow(gameHwnd)) {
                WNDPROC currentProc = (WNDPROC)GetWindowLongPtr(gameHwnd, GWLP_WNDPROC);
                if (currentProc == WndProc) {
                    SetWindowLongPtr(gameHwnd, GWLP_WNDPROC, (LONG_PTR)oWndProc);
                }
            }
            
            ImGui_ImplDX9_Shutdown();
            ImGui_ImplWin32_Shutdown();
            ImGui::DestroyContext();
            
            imguiInitialized = false;
        }
        
        // Clear EndScene queue
        {
            std::lock_guard<std::mutex> lock(endSceneMutex);
            endSceneQueue.clear();
        }
        
        // Cleanup MinHook
        MH_DisableHook(MH_ALL_HOOKS);
        MH_RemoveHook(MH_ALL_HOOKS);
        MH_Uninitialize();
        
        OutputDebugStringA("Arrow Renderer: Cleanup complete\n");
    }
    catch (...) {
        OutputDebugStringA("Arrow Renderer: Exception during cleanup\n");
    }
} 