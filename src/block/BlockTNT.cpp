/*
 _____                     _____ _
/  __ \                   /  ___| |
| |  | |                  |  ___| |
| |  | |_   _  __ _  __ _ | |___| |__   ___  _ __
| |  | | | | |/ _` |/ _` ||  __| '_ \ / _ \| '_ \
| |__| | |_| | (_| | (_| || |__| | | | (_) | | | |
 \____/ \__,_|\__, |\__,_|\____|_| |_|\___/|_| |_|
               __/ |
              |___/

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
// SPDX-License-Identifier: LGPL-3.0-or-later
// BlockTNT.cpp — TNT block (id 46).
//
// Today the block exists only so it can be registered, placed and broken
// (breaking it drops nothing, per lookupBlockDrop). It does NOT ignite.
//
// TODO (explosion): the full vanilla TNT chain, ported from Infinity
// (Dr1xyDev/Infinity, MCPE 0.15.10):
//
//   1. Ignition sources — block/TNT.php onActivate (flint & steel),
//      redstone power (updateTNTAt in RedstoneManager) and fire spread
//      (BlockFire). Igniting replaces the block with AIR and spawns
//      EntityPrimedTNT (entity/EntityPrimedTNT.cpp) with fuse = 80 ticks
//      (4 seconds, Infinity EntityPrimedTNT default) + the LevelEventPacket
//      EVENT_SOUND_TNT (1005) fuse hiss.
//   2. Prime event — fire ExplosionPrimeEvent (cancellable) before the
//      entity starts its countdown, exactly like PMMP.
//   3. Detonation — when the fuse ends, run world::Explosion(power 4) at
//      the entity position (world/Explosion.cpp), then fire
//      EntityExplodeEvent for plugins and broadcast the explosion
//      (EVENT_SOUND_EXPLODE 3501 + destroy particles for each removed
//      block, same 2001 packet the block-break path uses).
//   4. Knockback + damage — push and hurt every Living whose bounding box
//      touches the blast sphere (Infinity Explosion::explodeA / explodeB).
//   5. Drops — ~30% of the destroyed blocks drop as item entities
//      (vanilla explosion drop chance).
//
// Until that lands, TNT placed in the world is decorative: it never
// primes and never explodes.
#include "BlockTNT.h"

namespace cursestone::block {}
