// BanList — flat list of BanEntry objects, persisted to disk in
// banned-players.json and banned-ips.json. Two instances: one for
// names, one for IPs.
#pragma once
#include "BanEntry.h"
#include <vector>
#include <algorithm>
#include <string>
namespace cursestone::permission {
class BanList {
public:
    explicit BanList(std::string file_path) : file_path_(std::move(file_path)) {}
    void add(BanEntry e) { entries_.push_back(std::move(e)); }
    bool isBanned(const std::string& target) const {
        for (const auto& e : entries_)
            if (e.target() == target && !e.isExpired()) return true;
        return false;
    }
    void remove(const std::string& target) {
        entries_.erase(std::remove_if(entries_.begin(), entries_.end(),
            [&](const BanEntry& e) { return e.target() == target; }), entries_.end());
    }
    size_t size() const { return entries_.size(); }
    void load();  // load from file
    void save();  // save to file
private:
    std::vector<BanEntry> entries_;
    std::string file_path_;
};
} // namespace
