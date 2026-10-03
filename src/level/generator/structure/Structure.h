// SPDX-License-Identifier: LGPL-3.0-or-later
// Structure — a fixed block template stamped into the world by
// StructurePopulator.
//
// A structure is stored the way the CurseStone-Structures exports store
// it: a palette of (legacy id, metadata) pairs plus one DENSE volume —
// every cell of the bounding box is present, air included. Keeping the
// air matters: placement is then a plain overwrite, so the hollow
// interior of a desert temple really is hollow instead of leaving
// whatever the terrain pass happened to leave inside it.
//
// The volume is a string, row-major in (z, y): row `ly * sizeZ + lz`
// holds `sizeX` characters and character 'a' + n is palette entry n.
// Structures are small (the desert temple is 11x7x10 = 770 cells) so the
// whole template stays readable in the source and needs no data file at
// runtime. Invariant: the string has exactly sizeX*sizeY*sizeZ entries.
#pragma once
#include "level/format/Chunk.h"
#include <cstddef>
#include <cstdint>

namespace cursestone::level::generator::structure {

// One palette entry: the MCPE 0.15.x legacy block id and its metadata
// byte, which is what the chunk actually stores.
struct BlockState {
    uint8_t id;
    uint8_t data;
};

class Structure {
public:
    Structure(const char* name, int sizeX, int sizeY, int sizeZ,
              const BlockState* palette, size_t paletteCount,
              const char* volume)
        : name_(name), sizeX_(sizeX), sizeY_(sizeY), sizeZ_(sizeZ),
          palette_(palette), paletteCount_(paletteCount), volume_(volume) {}

    virtual ~Structure() = default;

    const char* name() const { return name_; }
    int sizeX() const { return sizeX_; }
    int sizeY() const { return sizeY_; }
    int sizeZ() const { return sizeZ_; }

    // Block at local coordinates (lx, ly, lz). Coordinates outside the
    // volume read as AIR, so callers never have to bounds-check.
    BlockState blockAt(int lx, int ly, int lz) const {
        if (lx < 0 || ly < 0 || lz < 0 ||
            lx >= sizeX_ || ly >= sizeY_ || lz >= sizeZ_)
            return {0, 0};
        const size_t i = (size_t)((ly * sizeZ_ + lz) * sizeX_ + lx);
        const size_t entry = (size_t)(volume_[i] - 'a');
        if (entry >= paletteCount_) return {0, 0};
        return palette_[entry];
    }

    // Does the bounding box, placed with its (0,0,0) cell at world
    // (wx, wy, wz), touch the chunk (chunkX, chunkZ)?
    //
    // Y is NOT tested: the caller only asks this to decide whether a
    // candidate structure is worth scanning, and the vertical extent is
    // checked when the base height is chosen.
    bool overlapsChunk(int chunkX, int chunkZ, int wx, int wz) const {
        const int bx = chunkX * 16, bz = chunkZ * 16;
        return wx < bx + 16 && wx + sizeX_ > bx &&
               wz < bz + 16 && wz + sizeZ_ > bz;
    }

    // Write every cell of the volume into `chunk`, clipped to that chunk.
    // Blocks that fall outside the chunk, or above the 128-block world,
    // are skipped — so a structure is never left half written just
    // because its neighbour has not been generated yet: every chunk
    // re-derives the same template from the seed and writes its own
    // share. Returns how many cells were actually written.
    size_t placeInto(mcp_chunk_t* chunk, int chunkX, int chunkZ,
                     int wx, int wy, int wz) const;

    // Vanilla gates every structure on a biome list; a candidate whose
    // footprint is not entirely inside an allowed biome is dropped.
    virtual bool allowsBiome(uint8_t biome) const {
        (void)biome;
        return true;
    }

private:
    const char* name_;
    int sizeX_, sizeY_, sizeZ_;
    const BlockState* palette_;
    size_t paletteCount_;
    const char* volume_;
};

} // namespace cursestone::level::generator::structure