/*
 _____                     _____ _
/  __ \                   /  ___| |
| /  \/_   _ _ __ ___  ___\ `--.| |_ ___  _ __   ___
| |   | | | | '__/ __|/ _ \`--. \ __/ _ \| '_ \ / _ \
| \__/\ |_| | |  \__ \  __/\__/ / || (_) | | | |  __/
 \____/\__,_|_|  |___/\___\____/ \__\___/|_| |_|\___|

 * CurseStone | Open Source Server Software
 *
 * Copyright (C) 2026 CurseStoneTeam
 *
 * Developed by CurseStoneTeam.
 * Built for an open and extensible Minecraft server ecosystem.
 *
 * Licensed under the GNU Lesser General Public License v3.0 or later.
 * You may study, modify, and redistribute this software under its license.
 *
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 * Repository: https://github.com/CurseStoneTeam/CurseStone
 *
 */
// Timings — lightweight timing collector. Tracks tick time for
// server subsystems (network, level, entities, plugins). The reference server
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
    const std::unordered_map<std::string, int64_t>& totals() const {
        return totals_;
    }
    void reset() {
        totals_.clear();
    }

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
