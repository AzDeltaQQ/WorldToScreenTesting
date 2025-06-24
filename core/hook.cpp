#include "hook.h"
#include "drawing/drawing.h"
#include "objects/ObjectManager.h"
#include "gui/GUI.h"
#include "types/types.h"
#include "logs/Logger.h"
#include <Windows.h>
#include <d3d9.h>
#include <iostream>
#include <algorithm>

// MinHook
#include "../dependencies/MinHook/include/MinHook.h"

// ImGui
#include <imgui.h>
#include <imgui_impl_win32.h>
#include <imgui_impl_dx9.h>

// Global variables
static WNDPROC g_OriginalWndProc = nullptr;
static EndSceneFn g_OriginalEndScene = nullptr;
static ResetFn g_OriginalReset = nullptr;
static bool g_HooksInitialized = false;
static bool g_ImGuiInitialized = false;
static HWND g_WowWindow = nullptr;
static ObjectManager* g_ObjectManager = nullptr;
static void* g_EndSceneAddress = nullptr;
static void* g_ResetAddress = nullptr;
static bool g_MinHookInitialized = false; // New flag for robust shutdown

// Forward declarations
extern WorldToScreenManager g_WorldToScreenManager;
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// Global GUI visibility state
static bool g_GuiVisible = true; // Start visible by default

// Shutdown flag accessible by all components
volatile bool g_shutdownRequested = false;

// Window procedure for ImGui input handling
LRESULT __stdcall hkWndProc(const HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    // Let ImGui process the event first. This is critical as it sets internal
    // state flags like WantCaptureKeyboard.
    if (g_ImGuiInitialized) {
        ImGui_ImplWin32_WndProcHandler(hWnd, uMsg, wParam, lParam);
    }

    // Toggle GUI visibility with the INSERT key.
    if (uMsg == WM_KEYDOWN && wParam == VK_INSERT) {
        // THE FIX: Use `WantTextInput` instead of `WantCaptureKeyboard`.
        // `WantCaptureKeyboard` is too greedy and becomes true whenever the ImGui window
        // has focus. `WantTextInput` is only true when the user is actively typing
        // in a text field, making the toggle key feel truly global.
        if (g_ImGuiInitialized && !ImGui::GetIO().WantTextInput) {
            g_GuiVisible = !g_GuiVisible;
        }
    }

    // NEW HYBRID INPUT LOGIC:
    // If the GUI is visible AND ImGui wants to capture the mouse (meaning the
    // cursor is over an ImGui window or a button is held down on it), then
    // we consume the mouse input and prevent it from reaching the game.
    // Otherwise, we allow the input to pass through. This allows for clicking
    // on units in-game while the GUI is open.
    if (g_GuiVisible && g_ImGuiInitialized && ImGui::GetIO().WantCaptureMouse) {
        // We only need to block mouse messages. Keyboard is handled by `WantTextInput`.
        switch (uMsg) {
            case WM_MOUSEMOVE:
            case WM_LBUTTONDOWN:
            case WM_LBUTTONUP:
            case WM_RBUTTONDOWN:
            case WM_RBUTTONUP:
            case WM_MBUTTONDOWN:
            case WM_MBUTTONUP:
            case WM_MOUSEWHEEL:
            case WM_XBUTTONDOWN:
            case WM_XBUTTONUP:
            case WM_MOUSEHOVER:
                return true; // ImGui is handling it, block it from the game.
        }
    }

    // If our GUI is not visible, or ImGui doesn't want the mouse,
    // forward the message to the original game window procedure.
    return CallWindowProc(g_OriginalWndProc, hWnd, uMsg, wParam, lParam);
}

HRESULT __stdcall HookedReset(IDirect3DDevice9* device, D3DPRESENT_PARAMETERS* pPresentationParameters) {
    LOG_INFO("DirectX Reset called. Invalidating device objects.");
    if (g_ImGuiInitialized) {
        ImGui_ImplDX9_InvalidateDeviceObjects();
        g_WorldToScreenManager.OnDeviceLost();
    }
    
    // Call the original Reset function
    HRESULT hr = g_OriginalReset(device, pPresentationParameters);
    
    if (SUCCEEDED(hr) && g_ImGuiInitialized) {
        LOG_INFO("Reset succeeded. Re-creating device objects.");
        ImGui_ImplDX9_CreateDeviceObjects();
        g_WorldToScreenManager.OnDeviceReset();
    } else if (FAILED(hr)) {
        LOG_ERROR("Original Reset function failed.");
    }
    
    return hr;
}

HRESULT __stdcall HookedEndScene(IDirect3DDevice9* device) {
    // First, check if the device is operational. If we alt-tabbed or minimized, it might be lost.
    HRESULT cooperativeLevel = device->TestCooperativeLevel();
    if (cooperativeLevel != D3D_OK) {
        // If the device is lost, we can't render. Our HookedReset will handle recreation.
        if (cooperativeLevel == D3DERR_DEVICELOST) {
            // Invalidate device objects here to be safe.
            ImGui_ImplDX9_InvalidateDeviceObjects();
            g_WorldToScreenManager.OnDeviceLost();
        }
        // Skip all rendering and just call the original EndScene.
        return g_OriginalEndScene(device);
    }

    // Only update if safely initialized
    if (g_HooksInitialized) {
        g_WorldToScreenManager.Update();
        
        // Initialize ImGui if not already done
        if (!g_ImGuiInitialized && device && g_WowWindow) {
            IMGUI_CHECKVERSION();
            ImGui::CreateContext();
            ImGuiIO& io = ImGui::GetIO(); (void)io;
            
            // Configure ImGui
            io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
            io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
            io.ConfigWindowsMoveFromTitleBarOnly = true; // Only allow dragging from title bar for consistency.
            
            // CRITICAL CURSOR FIX:
            // 1. We NEVER want ImGui to draw its own cursor. The game's cursor should always be used.
            io.MouseDrawCursor = false;
            // 2. We explicitly tell ImGui not to change the system cursor icon.
            io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
            
            // Disable INI file saving to prevent state corruption from resizing/alt-tabbing
            io.IniFilename = nullptr;
            
            ImGui::StyleColorsDark();
            
            // Initialize ImGui with WoW window
            if (ImGui_ImplWin32_Init(g_WowWindow) && ImGui_ImplDX9_Init(device)) {
                g_ImGuiInitialized = true;
                LOG_INFO("ImGui initialized successfully");
                
                // Initialize WorldToScreen system
                if (g_WorldToScreenManager.Initialize(device)) {
                    LOG_INFO("WorldToScreen initialized successfully");
                } else {
                    LOG_WARNING("WorldToScreen initialization failed");
                }
                
                // Initialize GUI system
                GUI::Initialize();
                
                // Initialize ObjectManager with proper addresses
                g_ObjectManager = ObjectManager::GetInstance();
                if (g_ObjectManager) {
                    LOG_INFO("Initializing ObjectManager with proper game addresses...");
                    
                    // Initialize with proper WoW function addresses from GameOffsets
                    bool initSuccess = g_ObjectManager->InitializeFunctions(
                        GameOffsets::ENUM_VISIBLE_OBJECTS_ADDR,
                        GameOffsets::GET_OBJECT_BY_GUID_INNER_ADDR,
                        GameOffsets::GET_LOCAL_PLAYER_GUID_ADDR
                    );
                    
                    if (initSuccess) {
                        LOG_INFO("ObjectManager function pointers initialized successfully");
                        
                        // Try immediate initialization
                        if (g_ObjectManager->TryFinishInitialization()) {
                            LOG_INFO("ObjectManager immediate initialization SUCCESS");
                        } else {
                            LOG_INFO("ObjectManager immediate initialization failed - will retry in EndScene");
                        }
                    } else {
                        LOG_ERROR("Failed to initialize ObjectManager function pointers");
                    }
                    
                    // Connect ObjectManager to GUI regardless of initialization status
                    GUI::GUIManager::GetInstance()->SetObjectManager(g_ObjectManager);
                    
                    LOG_INFO("ObjectManager connected to GUI");
                } else {
                    LOG_ERROR("Failed to get ObjectManager instance");
                }
            } else {
                LOG_ERROR("Failed to initialize ImGui");
            }
        }
        
        // Main rendering and GUI update logic
        if (g_ImGuiInitialized) {
            // Correct Rendering Order:
            // 1. Begin the ImGui frame. This is crucial as it processes inputs from the last frame
            //    and sets the state flags like `IsAnyItemActive` for the *current* frame.
            ImGui_ImplDX9_NewFrame();
            ImGui_ImplWin32_NewFrame();
            ImGui::NewFrame();

            // 2. Render all GUI elements. ImGui figures out what is being interacted with here.
            //    The data displayed will be from the last time ObjectManager was updated. This is acceptable.
            if (g_GuiVisible) {
                GUI::GUIManager::GetInstance()->Update(); // Updates tab data (fast)
                GUI::Render(&g_GuiVisible);           // Renders the main window and tabs
            }

            // Render WorldToScreen overlays.
            g_WorldToScreenManager.Render();

            // 3. Update the ObjectManager and WorldToScreen system.
            //    ObjectManager is updated less frequently to avoid performance issues.
            //    WorldToScreenManager is updated every frame for smooth line updates.
            if (g_ObjectManager) {
                if (!g_ObjectManager->IsInitialized()) {
                    g_ObjectManager->TryFinishInitialization();
                } else {
                    static int obj_update_counter = 0;
                    obj_update_counter++;
                    // Update ObjectManager every 30 frames (0.5 seconds) instead of 120 frames
                    // This ensures target positions update more frequently
                    if ((obj_update_counter % 30 == 0) && !ImGui::IsAnyItemActive()) {
                        g_ObjectManager->Update();
                    }
                }
            }
            
            // Update WorldToScreen system every frame for smooth dynamic lines
            g_WorldToScreenManager.Update();

            // 4. Finalize the ImGui frame and render its draw data.
            ImGui::EndFrame();
            ImGui::Render();
            ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
        }
    }
    
    // Call original EndScene
    return g_OriginalEndScene(device);
}

bool InitializeHooks() {
    if (g_HooksInitialized) {
        return true;
    }

    try {
        if (g_shutdownRequested) return false;

        // Initialize MinHook
        if (MH_Initialize() != MH_OK) {
            LOG_ERROR("MH_Initialize failed");
            return false;
        }
        g_MinHookInitialized = true;
        LOG_INFO("MinHook initialized successfully");

        // Allow graceful shutdown if requested
        if (g_shutdownRequested) {
            ShutdownHooks();
            return false;
        }

        // Get WoW window handle - try multiple approaches with better error handling
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
            if (wowWindow && IsWindow(wowWindow)) {
                LOG_INFO("Found WoW window using class: " + std::string(className));
                break;
            }
        }
        
        // Method 2: If still not found, use current process window
        if (!wowWindow || !IsWindow(wowWindow)) {
            wowWindow = GetActiveWindow();
            if (wowWindow && IsWindow(wowWindow)) {
                LOG_INFO("Using active window as WoW window");
            }
        }
        
        // Method 3: Last resort - find any window for this process
        if (!wowWindow || !IsWindow(wowWindow)) {
            DWORD processId = GetCurrentProcessId();
            wowWindow = GetTopWindow(nullptr);
            while (wowWindow) {
                DWORD windowProcessId;
                GetWindowThreadProcessId(wowWindow, &windowProcessId);
                if (windowProcessId == processId) {
                    LOG_INFO("Found window belonging to current process");
                    break;
                }
                wowWindow = GetNextWindow(wowWindow, GW_HWNDNEXT);
            }
        }
        
        // If we still don't have a valid window, create a minimal one
        if (!wowWindow || !IsWindow(wowWindow)) {
            LOG_WARNING("Could not find WoW window, using desktop window");
            wowWindow = GetDesktopWindow();
        }

        // Validate window before proceeding
        if (!wowWindow || !IsWindow(wowWindow)) {
            LOG_ERROR("No valid window found for D3D device creation");
            MH_Uninitialize();
            return false;
        }
        
        // Store window handle for ImGui
        g_WowWindow = wowWindow;
        
        // Get the D3D9 device vtable
        IDirect3D9* pD3D = Direct3DCreate9(D3D_SDK_VERSION);
        if (!pD3D) {
            LOG_ERROR("Direct3DCreate9 failed");
            MH_Uninitialize();
            return false;
        }

        D3DPRESENT_PARAMETERS d3dpp = {};
        d3dpp.Windowed = TRUE;
        d3dpp.SwapEffect = D3DSWAPEFFECT_DISCARD;
        d3dpp.hDeviceWindow = g_WowWindow;

        IDirect3DDevice9* d3dDevice = nullptr;
        HRESULT deviceResult = pD3D->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, g_WowWindow,
                                                  D3DCREATE_SOFTWARE_VERTEXPROCESSING, &d3dpp, &d3dDevice);
        
        if (FAILED(deviceResult) || !d3dDevice) {
            LOG_ERROR("Failed to create D3D device to get vtable");
            pD3D->Release();
            MH_Uninitialize();
            return false;
        }
        
        void** vTable = *reinterpret_cast<void***>(d3dDevice);
        g_EndSceneAddress = vTable[42]; // EndScene is at index 42
        g_ResetAddress = vTable[16];    // Reset is at index 16
        
        d3dDevice->Release();
        pD3D->Release();
        LOG_INFO("Successfully found D3D9 vtable addresses.");

        // Hook window procedure for ImGui input handling
        g_OriginalWndProc = (WNDPROC)SetWindowLongPtr(g_WowWindow, GWLP_WNDPROC, (LONG_PTR)hkWndProc);
        LOG_INFO("Window procedure hooked");

        // Create hooks for EndScene and Reset
        MH_STATUS status_endscene = MH_CreateHook(g_EndSceneAddress, &HookedEndScene, reinterpret_cast<void**>(&g_OriginalEndScene));
        MH_STATUS status_reset = MH_CreateHook(g_ResetAddress, &HookedReset, reinterpret_cast<void**>(&g_OriginalReset));

        if (status_endscene != MH_OK || status_reset != MH_OK) {
            LOG_ERROR("MH_CreateHook failed. EndScene: " + std::to_string(status_endscene) + ", Reset: " + std::to_string(status_reset));
            SetWindowLongPtr(g_WowWindow, GWLP_WNDPROC, (LONG_PTR)g_OriginalWndProc); // Restore WndProc
            MH_Uninitialize();
            return false;
        }
        LOG_INFO("EndScene and Reset hooks created");

        // Enable the hooks
        status_endscene = MH_EnableHook(g_EndSceneAddress);
        status_reset = MH_EnableHook(g_ResetAddress);
        if (status_endscene != MH_OK || status_reset != MH_OK) {
            LOG_ERROR("MH_EnableHook failed. EndScene: " + std::to_string(status_endscene) + ", Reset: " + std::to_string(status_reset));
            MH_RemoveHook(g_EndSceneAddress);
            MH_RemoveHook(g_ResetAddress);
            SetWindowLongPtr(g_WowWindow, GWLP_WNDPROC, (LONG_PTR)g_OriginalWndProc);
            ShutdownHooks(); // Use the main shutdown function for cleanup
            return false;
        }
        LOG_INFO("Hooks enabled successfully");

        g_HooksInitialized = true;
        return true;
    }
    catch (...) {
        LOG_ERROR("Exception during hook initialization");
        ShutdownHooks(); // Ensure cleanup on exception
        return false;
    }
}

void ShutdownHooks() {
    LOG_INFO("ShutdownHooks called");

    // CRITICAL: Stop ObjectManager updates first to prevent deadlocks
    if (g_ObjectManager) {
        g_ObjectManager->RequestShutdown();
        LOG_INFO("ObjectManager shutdown requested");
    }

    // First, restore the original window procedure. This is critical.
    if (g_WowWindow && g_OriginalWndProc) {
        SetWindowLongPtr(g_WowWindow, GWLP_WNDPROC, (LONG_PTR)g_OriginalWndProc);
        g_OriginalWndProc = nullptr;
        LOG_INFO("Window procedure restored");
    }

    // Uninitialize MinHook only if it was successfully initialized
    if (g_MinHookInitialized) {
        if (g_EndSceneAddress) {
            MH_DisableHook(g_EndSceneAddress);
            MH_RemoveHook(g_EndSceneAddress);
            g_EndSceneAddress = nullptr;
        }
        if (g_ResetAddress) {
            MH_DisableHook(g_ResetAddress);
            MH_RemoveHook(g_ResetAddress);
            g_ResetAddress = nullptr;
        }
        MH_Uninitialize();
        g_MinHookInitialized = false;
        LOG_INFO("MinHook uninitialized");
    }

    // Finally, shut down ImGui
    if (g_ImGuiInitialized) {
        ImGui_ImplDX9_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        g_ImGuiInitialized = false;
        LOG_INFO("ImGui shut down");
    }
    
    // Shutdown ObjectManager singleton last
    if (g_ObjectManager) {
        ObjectManager::Shutdown();
        g_ObjectManager = nullptr;
        LOG_INFO("ObjectManager singleton destroyed");
    }
    
    g_HooksInitialized = false;
    LOG_INFO("ShutdownHooks finished");
}

void CleanupHook(bool isForceTermination) {
    LOG_INFO("CleanupHook called");
    if (isForceTermination) {
        LOG_INFO("Forcing termination cleanup");
        // In forced termination, we can't guarantee a graceful shutdown.
        // We will attempt to clean up, but some resources may be left.
    }
    
    // Call the main shutdown function
    ShutdownHooks();

    // Cleanup WorldToScreen
    g_WorldToScreenManager.Cleanup();
    LOG_INFO("WorldToScreen cleaned up");
} 