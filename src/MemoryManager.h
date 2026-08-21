// MemoryManager — tracks the server's memory budget. Each tick,
// it samples the process RSS and triggers a GC sweep if it grows
// past max-mb. PocketMine uses a PHP memory_limit; in C we rely on
// the glibc allocator + explicit frees, so this is mostly a stub
// for the limit config in mcppeserver.yml.
#pragma once
#include <cstdint>
namespace mcppeserver {
class MemoryManager {
public:
    static MemoryManager& get();
    void setLimitMB(int mb) { limit_mb_ = mb; }
    int limitMB() const { return limit_mb_; }
    int64_t currentUsageKB() const;
    bool shouldGc() const;
    void runGc();
private:
    int limit_mb_ = 512;
};
} // namespace
