# WorldToScreen Testing Project

A World of Warcraft addon testing framework that demonstrates the real WorldToScreen function by sending coordinates to Lua for native UI frame creation.

## Overview

This project hooks into WoW's D3D9 EndScene and uses the actual game's WorldToScreen function to convert 3D world coordinates to 2D screen coordinates. Instead of using ImGui for rendering, it sends the calculated screen coordinates to Lua, which creates native WoW UI frames at the correct positions.

## Features

- **Real WorldToScreen Integration**: Uses WoW's actual WorldToScreen function at `0x4F6D20`
- **Native UI Rendering**: Creates WoW UI frames via Lua instead of external rendering
- **Lua Coordinate Sending**: Sends screen coordinates directly to WoW's Lua environment
- **Thread-Safe Hook System**: Uses MinHook for safe D3D9 EndScene hooking
- **Dynamic Arrow Management**: Add/remove arrows with real-time position updates

## Technical Details

### Architecture
- **Hook System**: MinHook-based D3D9 EndScene hook
- **Coordinate Conversion**: Real WoW WorldToScreen function with `__thiscall` convention
- **UI System**: Lua-generated WoW UI frames positioned at calculated screen coordinates
- **Memory Safety**: Proper type definitions and memory management

### Key Components
1. **WorldToScreenManager**: Manages arrow data and coordinates Lua frame creation
2. **Hook System**: Handles D3D9 EndScene hooking for update timing
3. **Lua Integration**: Executes Lua code to create and position UI frames
4. **Arrow Management**: Add, remove, and update arrow positions dynamically

### Memory Addresses (Update for your WoW version)
```cpp
constexpr uintptr_t WORLDFRAME_PTR = 0x00B7436C;     // Pointer to WorldFrame
constexpr uintptr_t WORLDTOSCREEN_ADDR = 0x004F6D20;  // WorldToScreen function
constexpr uintptr_t LUADOSTRING_ADDR = 0x00819210;    // LuaDoString function
```

## Building

### Prerequisites
- Visual Studio 2019 or later
- CMake 3.20+
- Windows SDK

### Build Steps
1. Clone the repository
2. Open in Visual Studio or use CMake
3. Build the solution
4. The DLL will be output to `build/bin/WorldToScreenTesting.dll`

### CMake Command Line
```bash
mkdir build
cd build
cmake ..
cmake --build . --config Release
```

## Usage

### Basic Integration
1. Inject the DLL into World of Warcraft
2. The system automatically adds test arrows at startup
3. Arrows appear as green UI frames with labels

### Programmatic Usage
```cpp
// Add an arrow at world coordinates
int arrowId = g_WorldToScreenManager.AddArrow(100.0f, 200.0f, 0.0f, "Test Arrow");

// Remove an arrow
g_WorldToScreenManager.RemoveArrow(arrowId);

// Clear all arrows
g_WorldToScreenManager.ClearAllArrows();
```

### Lua Frame Creation
The system automatically generates Lua code like:
```lua
local frameName = 'WorldToScreenArrow1';
local frame = _G[frameName];
if not frame then
  frame = CreateFrame('Frame', frameName, UIParent);
  frame:SetSize(20, 20);
  local texture = frame:CreateTexture(nil, 'BACKGROUND');
  texture:SetAllPoints();
  texture:SetColorTexture(0, 1, 0, 0.8); -- Green color
  frame.texture = texture;
  local text = frame:CreateFontString(nil, 'OVERLAY', 'GameFontNormal');
  text:SetPoint('BOTTOM', frame, 'TOP', 0, 2);
  text:SetText('Arrow Label');
  frame.text = text;
end;
frame:SetPoint('CENTER', UIParent, 'BOTTOMLEFT', 960.0, 540.0);
frame:Show();
```

## How It Works

1. **EndScene Hook**: Captures D3D9 EndScene calls for update timing
2. **WorldToScreen Call**: Uses real WoW function to convert world coordinates to normalized screen coordinates (0-1)
3. **Coordinate Conversion**: Multiplies normalized coordinates by screen dimensions for pixel coordinates
4. **Lua Execution**: Sends Lua code to create/update UI frames at calculated positions
5. **Frame Management**: Creates persistent WoW UI frames that move with camera changes

## Advantages Over ImGui

- **Native Integration**: Uses WoW's own UI system instead of external rendering
- **Performance**: No additional rendering overhead
- **Compatibility**: Works with WoW's existing UI framework
- **Persistence**: Frames persist and integrate with game UI
- **Styling**: Can use WoW's native fonts, textures, and styling

## Debugging

The system includes comprehensive error handling:
- Memory validation for game pointers
- Exception handling in Lua execution
- Safe arrow management with cleanup

## Dependencies

- **MinHook**: For safe function hooking
- **nlohmann/json**: For configuration (header-only)
- **Windows SDK**: For D3D9 and Windows APIs

## License

This project is for educational and testing purposes. Use responsibly and in accordance with game terms of service.

## Notes

- Update memory addresses for your specific WoW version
- LuaDoString address may vary between game versions
- Test in a safe environment before production use
- Arrows will appear as green squares with labels above them 