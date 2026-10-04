// SPDX-License-Identifier: LGPL-3.0-or-later
// Structure — a fixed block template stamped into the world by
// StructurePopulator / VillagePopulator.
//
// A structure is stored the way the CurseStone-Structures exports store
// it: a palette plus one DENSE volume — every cell of the bounding box
// is present, air included. Keeping the air matters: placement is then
// a plain overwrite, so the hollow interior of a desert temple really
// is hollow instead of leaving whatever the terrain pass happened to
// leave inside it.
//
// The volume is a string, row-major in (z, y): row `ly * sizeZ + lz`
// holds `sizeX` characters and character 'a' + n is palette entry n.
// Structures are small (the desert temple is 11x7x10 = 770 cells) so the
// whole template stays readable in the source and needs no data file at
// runtime. Invariant: the string has exactly sizeX*sizeY*sizeZ entries.
//
// Two palette flavours:
//
//   * Direct: the palette is finished (id, data) pairs — the desert
//     temple, whose Java states were already hand-mapped to legacy.
//   * Spec: the palette is biome-independent BlockSpecs — the village
//     pieces, whose wood/cobble/farm blocks must resolve DIFFERENTLY per
//     village biome (a desert village is sandstone where a plains one is
//     wood, vanilla's getBiomeSpecificBlockState). The caller passes a
//     resolver when placing.
//
// Rotation: villages are stamped at one of four 90-degree rotations so
// the same handful of pieces does not always read the same way. Rotation
// is clockwise viewed from above (+X east, +Z south); facings inside the
// blocks rotate with the shape, so doors still face their opening after
// the piece is turned.
#pragma once
#include "world/format/Chunk.h"
#include <cstddef>
#include <cstdint>

namespace cursestone::level::generator::structure {

// One palette entry: the MCPE 0.15.x legacy block id and its metadata
// byte, which is what the chunk actually stores.
struct BlockState {
    uint8_t id;
    uint8_t data;
};

// One biome-independent palette entry for village templates. `kind` is a
// SpecKind (defined in village/VillagePalette.h — it is the vocabulary
// the per-biome resolver speaks); the other fields carry the export's
// Java state, normalised:
//
//   facing: 0 none, 1 east, 2 west, 3 south, 4 north, 5 up — the JAVA
//           compass the exports use. Legacy data bits are derived from
//           it at resolve time, per block kind.
//   aux:    stairs/slab: 1 = top half. door: 1 = upper half. farmland:
//           moisture. everything else: 0.
//   misc:   door: 1 = open (the legacy format does not store the hinge
//           side, so the export's hinge property is dropped). wool:
//           colour. fluids: level.
struct BlockSpec {
    uint8_t kind;
    uint8_t facing;
    uint8_t aux;
    uint8_t misc;
};

// Clockwise quarter turns applied to a template at placement time.
enum Rotation : uint8_t { ROT_0 = 0, ROT_90 = 1, ROT_180 = 2, ROT_270 = 3 };

// Turns a spec into the legacy (id, data) pair to write, honouring the
// rotation already applied to the template (facing values rotate with
// it) and whatever user context the caller chained through (the village
// resolver reads the village biome from it).
using SpecResolver = BlockState (*)(const BlockSpec& spec, int rot, void* user);

// Rotate a facing (JAVA compass: 1 east, 2 west, 3 south, 4 north; 0
// none and 5 up are rotation-invariant) by `rot` clockwise quarter
// turns. One turn maps east->south, south->west, west->north,
// north->east. Lives here because both the placement core and the
// per-biome resolvers need the exact same turn.
inline uint8_t rotateFacing(uint8_t f, int rot) {
    switch (rot & 3) {
        case ROT_90: {
            constexpr uint8_t cw[5] = {0, 3, 4, 2, 1};
            return f <= 4 ? cw[f] : f;
        }
        case ROT_180: {
            constexpr uint8_t cc[5] = {0, 2, 1, 4, 3};
            return f <= 4 ? cc[f] : f;
        }
        case ROT_270: {
            constexpr uint8_t cw3[5] = {0, 4, 3, 1, 2};
            return f <= 4 ? cw3[f] : f;
        }
        default:
            return f;
    }
}

class Structure {
public:
    // Direct palette: placement writes palette entries verbatim.
    Structure(const char* name, int sizeX, int sizeY, int sizeZ,
              const BlockState* palette, size_t paletteCount,
              const char* volume)
        : name_(name), sizeX_(sizeX), sizeY_(sizeY), sizeZ_(sizeZ),
          palette_(palette), paletteCount_(paletteCount),
          specs_(nullptr), specCount_(0), volume_(volume) {}

    // Spec palette: placement resolves every entry through `resolve`.
    // (Unambiguous against the direct ctor: the pointer types differ.)
    Structure(const char* name, int sizeX, int sizeY, int sizeZ,
              const BlockSpec* specs, size_t specCount,
              const char* volume)
        : name_(name), sizeX_(sizeX), sizeY_(sizeY), sizeZ_(sizeZ),
          palette_(nullptr), paletteCount_(0),
          specs_(specs), specCount_(specCount), volume_(volume) {}

    virtual ~Structure() = default;

    const char* name() const { return name_; }
    int sizeX() const { return sizeX_; }
    int sizeY() const { return sizeY_; }
    int sizeZ() const { return sizeZ_; }

    // Footprint of the template after `rot` quarter turns.
    int rotatedSizeX(int rot) const {
        return (rot == ROT_90 || rot == ROT_270) ? sizeZ_ : sizeX_;
    }
    int rotatedSizeZ(int rot) const {
        return (rot == ROT_90 || rot == ROT_270) ? sizeX_ : sizeZ_;
    }

    // Block at local coordinates (lx, ly, lz). Coordinates outside the
    // volume read as AIR, so callers never have to bounds-check.
    //
    // Spec palettes have no meaning outside a placement (their blocks
    // depend on the village biome), so this only answers for direct
    // palettes and reads AIR for spec ones.
    BlockState blockAt(int lx, int ly, int lz) const {
        if (lx < 0 || ly < 0 || lz < 0 ||
            lx >= sizeX_ || ly >= sizeY_ || lz >= sizeZ_)
            return {0, 0};
        const size_t i = (size_t)((ly * sizeZ_ + lz) * sizeX_ + lx);
        const size_t entry = (size_t)(volume_[i] - 'a');
        if (palette_ == nullptr || entry >= paletteCount_) return {0, 0};
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
    // re-derives the same template from the seed and writes only its own
    // share. Returns how many cells were actually written.
    size_t placeInto(mcp_chunk_t* chunk, int chunkX, int chunkZ,
                     int wx, int wy, int wz) const {
        return placeRotated(chunk, chunkX, chunkZ, wx, wy, wz,
                            ROT_0, nullptr, nullptr);
    }

    // Same, rotated by `rot` quarter turns clockwise, with the palette
    // resolved per entry through `resolve` (village pieces). A nullptr
    // resolver on a spec palette resolves entries as-is-unrotated and is
    // only useful for tests.
    size_t placeInto(mcp_chunk_t* chunk, int chunkX, int chunkZ,
                     int wx, int wy, int wz, Rotation rot,
                     SpecResolver resolve, void* user) const {
        return placeRotated(chunk, chunkX, chunkZ, wx, wy, wz,
                            rot, resolve, user);
    }

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
    const BlockSpec* specs_;
    size_t specCount_;
    const char* volume_;

    // Template cell -> source cell, for `rot` clockwise turns. Kept in
    // lockstep with rotateRect() in VillagePopulator.cpp: a point (x, z)
    // of the source lands at (sizeZ-1-z, x) after one CW turn, so both
    // the pixels and the caller's footprint rectangle rotate identically.
    void sourceCell(int lx, int lz, int rot, int& sx, int& sz) const {
        switch (rot & 3) {
            case ROT_90:  sx = lz;            sz = sizeZ_ - 1 - lx; break;
            case ROT_180: sx = sizeX_ - 1 - lx; sz = sizeZ_ - 1 - lz; break;
            case ROT_270: sx = sizeX_ - 1 - lz; sz = lx;             break;
            default:      sx = lx;            sz = lz;               break;
        }
    }

    // Spec entry at source coordinates, or {0,0,0,0} out of range.
    BlockSpec specAt(int lx, int ly, int lz) const {
        if (lx < 0 || ly < 0 || lz < 0 ||
            lx >= sizeX_ || ly >= sizeY_ || lz >= sizeZ_)
            return {0, 0, 0, 0};
        const size_t i = (size_t)((ly * sizeZ_ + lz) * sizeX_ + lx);
        const size_t entry = (size_t)(volume_[i] - 'a');
        if (entry >= specCount_) return {0, 0, 0, 0};
        return specs_[entry];
    }

    // Shared clip-and-write loop (defined in Structure.cpp). A static
    // member, not a free function, because it reads the private cells.
    static size_t writeCells(const Structure& s, mcp_chunk_t* chunk,
                             int chunkX, int chunkZ, int wx, int wy, int wz,
                             int rot, SpecResolver resolve, void* user);

    size_t placeRotated(mcp_chunk_t* chunk, int chunkX, int chunkZ,
                        int wx, int wy, int wz, int rot,
                        SpecResolver resolve, void* user) const;
};

} // namespace cursestone::level::generator::structure
