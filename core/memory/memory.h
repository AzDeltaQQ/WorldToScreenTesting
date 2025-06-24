#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <Windows.h>

// Exception class for memory access errors
class MemoryAccessError : public std::runtime_error {
public:
    explicit MemoryAccessError(const std::string& message) : std::runtime_error(message) {}
};

namespace Memory {
    // Basic memory reading functions
    template<typename T>
    T Read(uintptr_t address) {
        if (address == 0) {
            throw MemoryAccessError("Null address");
        }
        
        try {
            // Simple memory read - WoW is in same process
            return *reinterpret_cast<T*>(address);
        }
        catch (...) {
            throw MemoryAccessError("Failed to read memory at address: 0x" + std::to_string(address));
        }
    }

    // Basic memory writing functions
    template<typename T>
    void Write(uintptr_t address, const T& value) {
        if (address == 0) {
            throw MemoryAccessError("Null address");
        }
        
        try {
            // Simple memory write - WoW is in same process
            *reinterpret_cast<T*>(address) = value;
        }
        catch (...) {
            throw MemoryAccessError("Failed to write memory at address: 0x" + std::to_string(address));
        }
    }

    // Read memory with offset
    template<typename T>
    T Read(uintptr_t baseAddress, uintptr_t offset) {
        return Read<T>(baseAddress + offset);
    }

    // Read pointer (follow memory chain)
    uintptr_t ReadPointer(uintptr_t address);
    
    // Read pointer with offset
    uintptr_t ReadPointer(uintptr_t baseAddress, uintptr_t offset);

    // Read string (null-terminated)
    std::string ReadString(uintptr_t address, size_t maxLength = 256);

    // Check if address is valid/readable
    bool IsValidAddress(uintptr_t address);

    // Protection helpers
    bool ProtectMemory(uintptr_t address, size_t size, DWORD newProtect, DWORD& oldProtect);
    bool RestoreMemory(uintptr_t address, size_t size, DWORD oldProtect);
} 