// BanEntry — a single ban record. Has target (name or IP), reason,
// expiration (0 = permanent), and source.
#pragma once
#include <string>
#include <cstdint>
namespace cenisyspe::permission {
class BanEntry {
public:
    BanEntry(std::string target, std::string reason = "Banned", int64_t expires = 0)
        : target_(std::move(target)), reason_(std::move(reason)), expires_(expires) {}
    const std::string& target() const { return target_; }
    const std::string& reason() const { return reason_; }
    int64_t expires() const { return expires_; }
    bool isExpired() const;
private:
    std::string target_;
    std::string reason_;
    int64_t expires_;
    std::string source_ = "Server";
};
} // namespace
