#pragma once

#include <cstdint>
#include <vector>

namespace NFS {
    namespace Util {
        class MemoryPatcher {
        public:
            static bool PatchBytes(uintptr_t offset, const std::vector<uint8_t>& newBytes);

        private:
            static bool UnprotectMemory(uintptr_t address, size_t size, uint32_t& oldProtect);
            static void ReprotectMemory(uintptr_t address, size_t size, uint32_t oldProtect);
        };
    }
}