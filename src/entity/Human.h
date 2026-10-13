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
// Human — base class for human entities (Players and NPC villagers).
// Holds inventory, armor, food, experience. Player subclass adds
// network session handling.
#pragma once
#include "Living.h"
#include <memory>
namespace cursestone::inventory { class PlayerInventory; }
namespace cursestone::entity {
class Human : public Living {
public:
    using Living::Living;
    void setInventory(std::shared_ptr<inventory::PlayerInventory> inv) { inv_ = std::move(inv); }
    std::shared_ptr<inventory::PlayerInventory> inventory() const { return inv_; }
    int foodLevel() const { return food_; }
    void setFoodLevel(int f) { food_ = f; }
    float saturation() const { return saturation_; }
    void setSaturation(float s) { saturation_ = s; }
    int experience() const { return xp_; }
    void setExperience(int x) { xp_ = x; }
protected:
    std::shared_ptr<inventory::PlayerInventory> inv_;
    int food_ = 20;
    float saturation_ = 5.0f;
    int xp_ = 0;
};
} // namespace
