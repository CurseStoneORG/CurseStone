#include "MemoryManager.h"
#include <fstream>
#include <string>
namespace cenisyspe {
MemoryManager& MemoryManager::get() { static MemoryManager m; return m; }
int64_t MemoryManager::currentUsageKB() const {
    // Linux: read /proc/self/status VmRSS
    std::ifstream f("/proc/self/status");
    std::string line;
    while (std::getline(f, line)) {
        if (line.rfind("VmRSS:", 0) == 0) {
            size_t i = line.find_first_of("0123456789");
            if (i != std::string::npos) return std::stoll(line.substr(i));
        }
    }
    return 0;
}
bool MemoryManager::shouldGc() const {
    return currentUsageKB() > (int64_t)limit_mb_ * 1024;
}
void MemoryManager::runGc() { /* glibc malloc_trim(0) */ }
} // namespace
