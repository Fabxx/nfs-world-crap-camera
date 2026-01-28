#include "MemoryPatcher.h"
#include <Windows.h>
#include "Logger.h"
#include "../Data/Globals.h"

namespace NFS {
    namespace Util {
        bool MemoryPatcher::UnprotectMemory(uintptr_t address, size_t size, uint32_t& oldProtect) {
            return VirtualProtect(reinterpret_cast<void*>(address), size, PAGE_EXECUTE_READWRITE, reinterpret_cast<PDWORD>(&oldProtect));
        }

        void MemoryPatcher::ReprotectMemory(uintptr_t address, size_t size, uint32_t oldProtect) {
            VirtualProtect(reinterpret_cast<void*>(address), size, oldProtect, reinterpret_cast<PDWORD>(&oldProtect));
        }

        bool MemoryPatcher::PatchBytes(uintptr_t offset, const std::vector<uint8_t>& newBytes) {
            Logger& L = Logger::Instance();

            // Controlla se g_ModuleBase è stato inizializzato
            if (g_moduleBase == 0) {
                L.Get()->critical("[MemoryPatcher] FAILED: g_ModuleBase is not initialized! Cannot patch memory.");
                return false;
            }

            // Calcola l'indirizzo assoluto
            uintptr_t absoluteAddress = g_moduleBase + offset;

            if (newBytes.empty()) {
                L.Get()->debug("[MemoryPatcher] PatchBytes called with no bytes to write.");
                return false;
            }

            uint32_t oldProtect;
            if (!UnprotectMemory(absoluteAddress, newBytes.size(), oldProtect)) {
                L.Get()->error("[MemoryPatcher] Failed to unprotect memory at address 0x{:X}", absoluteAddress);
                return false;
            }

            L.Get()->debug("[MemoryPatcher] Memory unprotected. Writing {} bytes to 0x{:X} (Offset: 0x{:X})", newBytes.size(), absoluteAddress, offset);

            memcpy(reinterpret_cast<void*>(absoluteAddress), newBytes.data(), newBytes.size());

            ReprotectMemory(absoluteAddress, newBytes.size(), oldProtect);
            L.Get()->debug("[MemoryPatcher] Memory reprotected successfully.");

            return true;
        }
    }
}