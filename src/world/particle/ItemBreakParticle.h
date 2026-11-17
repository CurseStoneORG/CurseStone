/*
 _____                     _____ _
/  __ \                   /  ___| |
| /  \/_   _ _ __ ___  ___\ `--.| |_ ___  _ __   ___
| |   | | | | '__/ __|/ _ \`--. \ __/ _ \| '_ \ / _ \
| \__/\ |_| | |  \__ \  __/\__/ / || (_) | | | |  __/
 \____/\__,_|_|  |___/\___\____/ \__\___/|_| |_|\___|

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
// ItemBreakParticle — protocol-84 particle id 10 ("item_break").
// Family: Item. The client reads the payload as the item it shatters:
// item id in bits 16+, damage in the low 16 bits (reference
// ItemBreakParticle). A payload whose item id resolves to nothing —
// most visibly 0, what the client parses out of the old low-bits
// packing — crashes the 0.15.10 renderer, so validData() rejects those
// payloads before they reach the wire.
#pragma once
#include "GenericParticle.h"
#include "ParticleFamily.h"
namespace cursestone::world::particle {

class ItemBreakParticle : public GenericParticle {
  public:
    static constexpr ParticleId kId = PARTICLE_ITEM_BREAK;
    static constexpr DataKind kDataKind = DataKind::Item;

    // Packs the payload the protocol-84 client parses: id high, damage low.
    static int32_t pack(uint16_t itemId, uint16_t damage) {
        return packItemData(itemId, damage);
    }

    // False for payloads the 0.15.10 client cannot render without crashing
    // (the item id half of the payload must be a registered, non-air item).
    static bool validData(int32_t data);

    explicit ItemBreakParticle(Position pos, int32_t data = 0)
        : GenericParticle(kId, pos, data) {
    }
};
} // namespace
