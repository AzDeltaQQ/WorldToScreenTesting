# WorldToScreen Test DLL

A safe testing framework for WoW's WorldToScreen function with comprehensive crash prevention.

## Features

- **Safe Initialization**: No WorldToScreen calls during startup
- **Memory Validation**: Comprehensive pointer validation before any game function calls
- **Delayed Execution**: 60-frame delay before any WorldToScreen operations
- **Exception Handling**: Graceful error handling with detailed logging
- **ImGui Interface**: User-friendly control panel for testing
- **Real-time Logging**: Debug logging to `worldtoscreen_debug.log`

## Safety Features

1. **Startup Protection**: The DLL waits 60 frames after injection before attempting any WorldToScreen calls
2. **Pointer Validation**: All game pointers are validated using VirtualQuery before access
3. **Safe Mode**: Enabled by default, prevents WorldToScreen calls unless arrows are manually added
4. **Bounds Checking**: World positions are validated to be within reasonable ranges
5. **Memory Safety**: Uses VirtualQuery instead of exception handling for C++ compatibility

## Usage

1. **Build**: The DLL is built to `build/bin/Release/WorldToScreenTest.dll`
2. **Inject**: Use your preferred DLL injector to inject into WoW
3. **Wait**: The system will initialize safely over 60 frames
4. **Add Arrows**: Use the ImGui control panel to add test arrows
5. **Test**: Arrows will only appear if WorldToScreen succeeds

## Controls

- **INSERT Key**: Toggle the control panel (if implemented)
- **Add Safe Test Arrows**: Adds arrows at safe positions (0,0,0), (10,0,0), (0,10,0)
- **Add Arrow**: Manually add arrows at specific world coordinates
- **Clear All Arrows**: Remove all arrows
- **Safe Mode**: Toggle safety restrictions

## Game Addresses Used

- **WorldToScreen Function**: `0x4F6D20`
- **WorldFrame Pointer**: `0x00B7436C`

## Files

- `core/worldtoscreen.cpp/h`: Main arrow rendering and WorldToScreen interface
- `core/hook.cpp/h`: D3D9 EndScene hook implementation
- `core/types.h`: Game structures and function signatures
- `dllmain.cpp`: DLL entry point

## Crash Prevention

The system implements multiple layers of crash prevention:

1. **No immediate WorldToScreen calls** - waits for game stability
2. **Comprehensive pointer validation** - checks all memory before access
3. **Safe bounds checking** - validates world coordinates
4. **Graceful error handling** - logs errors instead of crashing
5. **Thread-safe operations** - uses proper synchronization

## Debugging

Check `worldtoscreen_debug.log` for detailed information about:
- Initialization status
- WorldFrame pointer validation
- WorldToScreen function calls
- Error conditions
- Arrow additions/removals

## If It Still Crashes

1. Check the log file for error messages
2. Ensure you're injecting into the correct WoW version
3. Verify the game addresses are correct for your client
4. Try disabling Safe Mode only after confirming basic functionality works
5. Add arrows one at a time to isolate issues 