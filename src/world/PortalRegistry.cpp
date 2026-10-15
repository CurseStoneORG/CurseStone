/*
 _____                     _____ _
/  __ \                   /  ___| |
| |   | | | | | '__/ __|/ _ \`--. \ __/ _ \| '_ \ / _ \
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
#include "PortalRegistry.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace cursestone::world {
namespace {
// Two portals closer than this are considered the same one.
constexpr double kDedupeDistance = 4.0;
}

const PortalRegistry::Portal* PortalRegistry::findNearest(
        double x, double y, double z, double maxRadius) const {
    std::lock_guard<std::mutex> lk(mtx_);
    const Portal* best = nullptr;
    double bestScore = maxRadius * maxRadius;
    for (const Portal& p : portals_) {
        double dx = p.x + 0.5 - x;
        double dz = p.z + 0.5 - z;
        double dy = (p.y + 0.5 - y) * 0.5;   // vertical offset weighs less
        double score = dx * dx + dy * dy + dz * dz;
        if (score <= bestScore) {
            bestScore = score;
            best = &p;
        }
    }
    return best;
}

void PortalRegistry::add(int32_t x, int32_t y, int32_t z, uint8_t axis) {
    std::lock_guard<std::mutex> lk(mtx_);
    for (const Portal& p : portals_) {
        double dx = p.x - x, dy = p.y - y, dz = p.z - z;
        if (dx * dx + dy * dy + dz * dz <= kDedupeDistance * kDedupeDistance)
            return;
    }
    portals_.push_back(Portal{x, y, z, axis});
}

bool PortalRegistry::contains(int32_t x, int32_t y, int32_t z) const {
    std::lock_guard<std::mutex> lk(mtx_);
    for (const Portal& p : portals_)
        if (p.x == x && p.y == y && p.z == z) return true;
    return false;
}

bool PortalRegistry::remove(int32_t x, int32_t y, int32_t z) {
    std::lock_guard<std::mutex> lk(mtx_);
    for (auto it = portals_.begin(); it != portals_.end(); ++it) {
        if (it->x == x && it->y == y && it->z == z) {
            portals_.erase(it);
            return true;
        }
    }
    return false;
}

bool PortalRegistry::load(const std::string& path) {
    std::lock_guard<std::mutex> lk(mtx_);
    portals_.clear();

    FILE* f = std::fopen(path.c_str(), "r");
    if (!f) return false;   // fresh world: no file yet

    char line[128];
    while (std::fgets(line, sizeof(line), f)) {
        long x, y, z;
        int axis;
        if (std::sscanf(line, " %ld %ld %ld %d", &x, &y, &z, &axis) != 4)
            continue;   // skip blank/malformed lines
        if (x < -30000000 || x > 30000000 || y < 0 || y > 255 ||
            z < -30000000 || z > 30000000)
            continue;
        portals_.push_back(Portal{(int32_t)x, (int32_t)y, (int32_t)z,
                                  (uint8_t)(axis & 0xFF)});
    }
    std::fclose(f);
    return true;
}

bool PortalRegistry::save(const std::string& path) const {
    std::lock_guard<std::mutex> lk(mtx_);
    std::string tmp = path + ".tmp";
    FILE* f = std::fopen(tmp.c_str(), "w");
    if (!f) return false;
    for (const Portal& p : portals_) {
        std::fprintf(f, "%d %d %d %d\n", (int)p.x, (int)p.y, (int)p.z,
                     (int)p.axis);
    }
    std::fclose(f);
    return std::rename(tmp.c_str(), path.c_str()) == 0;
}

size_t PortalRegistry::size() const {
    std::lock_guard<std::mutex> lk(mtx_);
    return portals_.size();
}

} // namespace cursestone::world
