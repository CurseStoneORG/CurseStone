/*
 _____                     _____ _
/  __ \                   /  ___| |
| /  \/_   _ _ __ ___  ___\ `--.| |_ ___  _ __   ___
| |   | | | | '__/ __|/ _ \`--. \ __/ _ \| '_ \ / _ \
| \__/\ |_| | |  \__ \  __/\__/ / || (_) | | | |  __/
 \____/\__,_|_|  |___/\___\____/ \__\___/|_| |_|\___|

 * CurseStone | Open Source server Software
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

// Base class for inventory items. Subclasses are registered instances
// retrieved through Item::get(id).
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <memory>

namespace cursestone {
class Player; // Provides the hunger and eating effect hooks.
}

namespace cursestone::item {
class Item {
  public:
    Item(uint16_t id, uint8_t meta = 0, int count = 1) : id_(id), meta_(meta), count_(count) {
    }
    virtual ~Item() = default;
    uint16_t id() const {
        return id_;
    }
    uint8_t meta() const {
        return meta_;
    }
    int count() const {
        return count_;
    }
    void setMeta(uint8_t m) {
        meta_ = m;
    }
    void setCount(int c) {
        count_ = c;
    }
    virtual std::string name() const {
        return std::string("Item#") + std::to_string(id_);
    }
    // Return the maximum stack size.
    virtual int maxStackSize() const {
        return 64;
    }
    // Return the item's attack damage.
    virtual int attackDamage() const {
        return 0;
    }
    // Check whether the item is a tool with durability.
    virtual bool isTool() const {
        return false;
    }
    // Check whether the item can be worn as armor.
    virtual bool isArmor() const {
        return false;
    }
    // Check whether the item can be eaten.
    virtual bool isFood() const {
        return false;
    }
    virtual int foodPoints() const {
        return 0;
    }
    // Return food values for a specific item variant.
    virtual int foodPoints(uint8_t) const {
        return foodPoints();
    }
    virtual float foodSaturation(uint8_t) const {
        return 0.6f;
    }
    // Check whether the item can be eaten at full hunger.
    virtual bool alwaysEdible() const {
        return false;
    }
    // Return whether a specific item variant can always be eaten.
    virtual bool alwaysEdible(uint8_t) const {
        return alwaysEdible();
    }
    // Return the item left after eating, or 0 if there is none.
    virtual uint16_t eatResidueId() const {
        return 0;
    }
    // Apply eating effects for the specified item variant.
    virtual void applyEatEffects(Player&, uint8_t) const {}
    // Retrieve the registered item, using its block definition when applicable.
    static Item* get(uint16_t id);
    static void registerItem(std::unique_ptr<Item> b);
    // Register all built-in items.
    static void registerAll();

  private:
    uint16_t id_;
    uint8_t meta_;
    int count_;
    static std::vector<std::unique_ptr<Item>>& registry();
};
} // namespace cursestone::item