/*
 _____                     _____ _
/  __ \                   /  ___| |
| /  \/_   _ _ __ ___  ___\ `--.| |_ ___  _ __   ___
| \__/\ |_| | |  \__ \  __/\__/ / || (_) | | | |  __/
| \____/\__,_|_|  |___/\___\____/ \__\___/|_| |_|\___|

 * CurseStone | Open Source Server Software
 *
 * Copyright (C) 2026 CurseStoneORG
 *
 * Developed by CurseStoneORG.
 * Built for an open and extensible Minecraft server ecosystem.
 *
 * Licensed under the GNU Lesser General Public License v3.0 or later.
 * You may study, modify, and redistribute this software under its license.
 *
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 * Repository: https://github.com/CurseStoneORG/CurseStone
 *
 */
// SPDX-License-Identifier: LGPL-3.0-or-later
#include "world/MobPersistence.h"

#include <cmath>
#include <cstdio>

namespace cursestone::world {
namespace {

// Field format of one line: type x y z yaw pitch health max_health.
// Two decimals on the coordinates are centimeter resolution — far
// below anything a mob's AI or a player can distinguish — and keep the
// file compact (1024 mobs cap ~= 60 KB worst case, rewritten atomically
// on the existing save points).
constexpr char kLineFormat[] = "%d %.2f %.2f %.2f %.1f %.1f %.2f %.2f\n";
constexpr int kLineFields = 8;

// Parse-time sanity bounds. The era world is [0, 255] high and the
// portals.dat reader already draws the +-30000000 horizontal border;
// the health ceiling only rejects garbage (the largest registry value
// is a wither-scale number far below it).
constexpr double kMaxCoord = 30000000.0;
constexpr float kMaxSavedHealth = 4096.0f;

// Parses one line; returns false for blank or malformed rows. Every
// numeric field must be finite (a corrupted "nan"/"inf" must not place
// a mob), and the stored healths must sit inside the sane band — the
// World layer still re-clamps against the registry.
bool parseMobLine(const char* line, PersistedMob* out) {
    PersistedMob m;
    int type = 0;
    if (std::sscanf(line, " %d %lf %lf %lf %f %f %f %f", &type, &m.x, &m.y,
                    &m.z, &m.yaw, &m.pitch, &m.health, &m.max_health) !=
        kLineFields)
        return false;
    if (type < 0 || type > 1000000)
        return false;
    if (!std::isfinite(m.x) || !std::isfinite(m.y) || !std::isfinite(m.z) ||
        !std::isfinite(m.yaw) || !std::isfinite(m.pitch) ||
        !std::isfinite(m.health) || !std::isfinite(m.max_health))
        return false;
    if (m.x < -kMaxCoord || m.x > kMaxCoord || m.z < -kMaxCoord ||
        m.z > kMaxCoord || m.y < 0.0 || m.y > 255.0)
        return false;
    if (m.health <= 0.0f || m.health > kMaxSavedHealth ||
        m.max_health <= 0.0f || m.max_health > kMaxSavedHealth)
        return false;
    // Wrap the rotations into the ranges the wire uses so a hand-edited
    // file cannot push a weird yaw through AddEntityPacket.
    m.yaw = std::fmod(m.yaw, 360.0f);
    m.pitch = std::fmod(m.pitch, 360.0f);
    out->type = type;
    out->x = m.x;
    out->y = m.y;
    out->z = m.z;
    out->yaw = m.yaw;
    out->pitch = m.pitch;
    out->health = m.health;
    out->max_health = m.max_health;
    return true;
}

} // namespace

std::vector<PersistedMob> MobPersistence::load(const std::string& path) {
    std::vector<PersistedMob> out;
    FILE* f = std::fopen(path.c_str(), "r");
    if (!f)
        return out; // Fresh world: no file yet.

    char line[160];
    while (std::fgets(line, sizeof(line), f)) {
        PersistedMob m;
        if (parseMobLine(line, &m))
            out.push_back(m);
    }
    std::fclose(f);
    return out;
}

bool MobPersistence::save(const std::string& path,
                          const std::vector<PersistedMob>& mobs) {
    const std::string tmp = path + ".tmp";
    FILE* f = std::fopen(tmp.c_str(), "w");
    if (!f)
        return false;
    for (const PersistedMob& m : mobs) {
        std::fprintf(f, kLineFormat, (int)m.type, m.x, m.y, m.z, m.yaw, m.pitch,
                     m.health, m.max_health);
    }
    std::fclose(f);
    return std::rename(tmp.c_str(), path.c_str()) == 0;
}

} // namespace cursestone::world
