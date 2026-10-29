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
// SPDX-License-Identifier: LGPL-3.0-or-later
// NeighborChunkIO — contracts for generation-time block writes that are
// aimed at a chunk other than the one being populated.
//
// Vanilla delivers those writes through live 3x3 chunk neighborhoods, so
// a tree or lake that crosses a border simply lands in the neighbor's
// real chunk data. CurseStone populates one chunk at a time on workers,
// so the write either reaches the neighbor's live chunk (already
// generated or loaded from disk) or has to wait in a spill queue until
// that chunk becomes live. These interfaces wire the generator (VgenSource)
// and the ChunkManager together without either of them including the
// other's header.
#pragma once
#include <stdint.h>
#include "world/format/Chunk.h"

namespace cursestone::world::generator {

// Implemented by VgenSource: owns the per-chunk spill queues.
class ISpillQueue {
public:
        virtual ~ISpillQueue() = default;
        // Store a write for a chunk that is not live yet; replayed by
        // drainSpills() when that chunk becomes live.
        virtual void queueSpill(int cx, int cz, uint32_t index, uint8_t id, uint8_t data) = 0;
        // Apply every queued write for (cx, cz) to `chunk` and clear the
        // queue. Called with the ChunkManager locks held right after the
        // chunk becomes live, so a write can never land between "queued"
        // and "live".
        virtual void drainSpills(int cx, int cz, mcp_chunk_t* chunk) = 0;
};

// Implemented by ChunkManager: delivers a neighbor-aimed write.
class INeighborBlockSink {
public:
        virtual ~INeighborBlockSink() = default;
        // Write into the live chunk when one exists, queue the write
        // otherwise — both under the manager's locks, so the decision is
        // atomic against concurrent chunk installs.
        virtual void commitNeighborWrite(int cx, int cz, uint32_t index, uint8_t id, uint8_t data) = 0;
};

} // namespace cursestone::world::generator
