#include "memory.h"
#include <Windows.h>

namespace Memory {
    uintptr_t ReadPointer(uintptr_t address) {
        if (address == 0) return 0;
        
        try {
            return *reinterpret_cast<uintptr_t*>(address);
        }
        catch (...) {
            return 0;
        }
    }

    uintptr_t ReadPointer(uintptr_t baseAddress, uintptr_t offset) {
        return ReadPointer(baseAddress + offset);
    }

    std::string ReadString(uintptr_t address, size_t maxLength) {
        if (address == 0) return "";
        
        try {
            const char* str = reinterpret_cast<const char*>(address);
            return std::string(str, strnlen_s(str, maxLength));
        }
        catch (...) {
            return "";
        }
    }

    bool IsValidAddress(uintptr_t address) {
        if (address == 0) return false;
        
        // Basic check - try to read a byte
        try {
            volatile uint8_t test = *reinterpret_cast<uint8_t*>(address);
            (void)test; // Suppress unused variable warning
            return true;
        }
        catch (...) {
            return false;
        }
    }

    bool ProtectMemory(uintptr_t address, size_t size, DWORD newProtect, DWORD& oldProtect) {
        return VirtualProtect(reinterpret_cast<LPVOID>(address), size, newProtect, &oldProtect) != 0;
    }

    bool RestoreMemory(uintptr_t address, size_t size, DWORD oldProtect) {
        DWORD dummy;
        return VirtualProtect(reinterpret_cast<LPVOID>(address), size, oldProtect, &dummy) != 0;
    }

} // namespace Memory 