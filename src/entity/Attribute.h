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
// Attribute — server-side attribute like max_health, movement_speed,
// attack_damage, knockback_resistance. Sent in UpdateAttributesPacket.
#pragma once
#include <string>
namespace cursestone::entity {
class Attribute {
public:
    Attribute() = default;
    Attribute(std::string name, float min, float max, float value)
        : name_(std::move(name)), min_(min), max_(max), value_(value) {}
    const std::string& name() const { return name_; }
    float min() const { return min_; }
    float max() const { return max_; }
    float value() const { return value_; }
    void setValue(float v) { if (v >= min_ && v <= max_) value_ = v; }
private:
    std::string name_;
    float min_, max_, value_;
};
} // namespace
