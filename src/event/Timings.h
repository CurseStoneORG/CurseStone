// Timings — lightweight timing collector. Tracks tick time for
// server subsystems (network, level, entities, plugins). PocketMine
// has a full Timings system with one timing per "section"; we keep
// a flat map of name→total_ns for later printing.
#pragma once
#include <unordered_map>
#include <string>
#include <chrono>
#include <cstdint>
namespace cursestone::event {
class Timings {
public:
    static Timings& get();
    void record(const std::string& section, int64_t nanos) {
        totals_[section] += nanos;
    }
    const std::unordered_map<std::string, int64_t>& totals() const { return totals_; }
    void reset() { totals_.clear(); }
private:
    std::unordered_map<std::string, int64_t> totals_;
};
class TimingsSection {
public:
    TimingsSection(std::string name) : name_(std::move(name)) {
        start_ = std::chrono::steady_clock::now();
    }
    ~TimingsSection() {
        auto end = std::chrono::steady_clock::now();
        auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start_).count();
        Timings::get().record(name_, ns);
    }
private:
    std::string name_;
    std::chrono::steady_clock::time_point start_;
};
} // namespace
