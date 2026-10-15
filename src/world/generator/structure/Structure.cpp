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
// Structure.cpp — rotated placement core.
//
// placeRotated() is the one place blocks reach the chunk from a
// template. It maps each output cell back to its source cell for the
// chosen clockwise quarter turn, so any template — desert temple,
// village piece, whatever comes later — can be stamped at any of the
// four 90-degree rotations without duplicating data. Facings inside the
// blocks rotate with the shape: the resolver receives the rotation so
// it can turn a door to still face its opening, a torch its wall.
#include "Structure.h"

namespace cursestone::world::generator::structure {

// rotateFacing() lives in the header: the per-biome resolvers turn the
// very same facings, so both sides must share one table.

// Shared clip-and-write loop. Every output cell (lx, ly, lz) of the
// ROTATED bounding box maps back to one source cell through sourceCell()
// — the same mapping rotateRect() in VillagePopulator.cpp uses for the
// footprint rectangle, which is what keeps the pixels and the rectangle
// in lockstep. Blocks falling outside the chunk or above the 128-block
// world are skipped: every chunk re-derives the same template and writes
// only its own share, so nothing is ever half written.
size_t Structure::writeCells(const Structure& s, mcp_chunk_t* chunk, int chunkX, int chunkZ, int wx,
                             int wy, int wz, int rot, SpecResolver resolve, void* user) {
    const int bx = chunkX * 16;
    const int bz = chunkZ * 16;
    const int sx = s.rotatedSizeX(rot);
    const int szz = s.rotatedSizeZ(rot);
    size_t written = 0;

    for (int ly = 0; ly < s.sizeY(); ++ly) {
        const int y = wy + ly;
        if (y < 0 || y > 127)
            continue;

        for (int lz = 0; lz < szz; ++lz) {
            const int cz = wz + lz - bz;
            if (cz < 0 || cz > 15)
                continue;

            for (int lx = 0; lx < sx; ++lx) {
                const int cx = wx + lx - bx;
                if (cx < 0 || cx > 15)
                    continue;

                int ox, oz;
                s.sourceCell(lx, lz, rot, ox, oz);
                BlockState b;
                if (resolve != nullptr)
                    b = resolve(s.specAt(ox, ly, oz), rot, user);
                else
                    b = s.blockAt(ox, ly, oz);
                mcp_chunk_set_block_full(chunk, cx, y, cz, b.id, b.data);
                ++written;
            }
        }
    }
    return written;
}

size_t Structure::placeRotated(mcp_chunk_t* chunk, int chunkX, int chunkZ, int wx, int wy, int wz,
                               int rot, SpecResolver resolve, void* user) const {
    if (specs_ != nullptr) {
        // Spec palette: a resolver is required to turn specs into legacy
        // blocks; without one there is nothing meaningful to write.
        if (resolve == nullptr)
            return 0;
        return writeCells(*this, chunk, chunkX, chunkZ, wx, wy, wz, rot, resolve, user);
    }
    if (palette_ != nullptr) {
        // Direct palette (desert temple): blocks are already legacy
        // pairs, placement is verbatim. Rotation of a finished legacy
        // palette would need per-block data-bit knowledge that lives
        // with the template, not here — and no caller rotates those.
        if (rot != ROT_0)
            return 0;
        return writeCells(*this, chunk, chunkX, chunkZ, wx, wy, wz, ROT_0, nullptr, nullptr);
    }
    return 0;
}

} // namespace cursestone::world::generator::structure
