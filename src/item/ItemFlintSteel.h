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
// ItemFlintSteel.h — portado of pocketmine/item/FlintSteel.php (PMMP)
//
// Implementa:
//   - Colocación of fire in blocks solids (onActivate)
//   - lit of the portal of the Nether about a frame of obsidian
//     (interior of 2x3 until 21x21, in the axis X or in the axis Z)
//   - Degradación of durability (2 points by use in survival)
//
// Diferencias with the PHP original:
//   - onActivate() receives World* + player* + coordinates enteras instead of
//     objetos Block with position embebida.
//   - the inserción of blocks portal uses World::setBlock(x,and,z, portal, meta).
//     BlockPortal (id=90 in MCPE 0.15.x) must estar registrado in
//     Block::registry().
//   - the durability is saves in the field `meta` of the ItemStack (mirrors PMMP
//     Tool::useOn($block, $damage=2)).
#pragma once
#include "Item.h"
#include <string>
#include <cstdint>

namespace cursestone {
class Player;
namespace world {
class World;
}
}

namespace cursestone::item {

class ItemFlintSteel : public Item {
  public:
    ItemFlintSteel() : Item(259) {
    }
    std::string name() const override {
        return "Flint and Steel";
    }

    // Flint & Steel is a herramienta (toma durability).
    bool isTool() const override {
        return true;
    }
    int maxStackSize() const override {
        return 1;
    }

    // durability máxima: 65 usos (just like PMMP MCPE 0.15.x)
    static constexpr uint8_t MAX_DURABILITY = 64;

    // point of entrada main — call from player::handleBlockPlace()
    // when heldItemId == 259.
    //
    // Mirrors: FlintSteel::onActivate($level, $player, $block, $target, $face, ...)
    //
    // Parámetros:
    //   level         — world activo
    //   player        — player that activó the ítem
    //   bx,by,bz      — coordinates of the block AIR in the cara clicked
    //   tx,ty,tz      — coordinates of the block TARGET (that which recibió the click)
    //   face          — cara of the target (0=below, 1=above, 2-5=laterales)
    //
    // returns true if is realizó some action (propagando the respuesta to the client).
    static bool onActivate(world::World* world, Player* player, int32_t bx, int32_t by,
                           int32_t bz,                         // block AIR
                           int32_t tx, int32_t ty, int32_t tz, // block target
                           uint8_t face);

  private:
    // applies 2 points of damage to the ítem in mano of the player (survival only).
    // mirrors: $this->useOn($block, 2)
    static void applyDurability(Player* player);
};

} // namespace cursestone::item
