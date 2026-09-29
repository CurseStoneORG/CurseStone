#include "BanEntry.h"
#include <chrono>
namespace cursestone::permission {
bool BanEntry::isExpired() const {
    if (expires_ == 0) return false;
    auto now = std::chrono::system_clock::now().time_since_epoch().count();
    return (int64_t)now > expires_;
}
} // namespace
