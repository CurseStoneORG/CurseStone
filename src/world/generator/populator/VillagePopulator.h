// SPDX-License-Identifier: LGPL-3.0-or-later
// VillagePopulator — vanilla-style villages stamped into the terrain.
//
// Placement follows vanilla's village structure placement: a candidate
// anchor every 32 x 32 chunks, kept 8 chunks away from the region edge,
// salt 10387312 (the number vanilla uses for villages). The whole
// layout — where the well sits inside the anchor chunk, which way the
// village rotates, which piece every slot gets, and every road cell —
// is derived from a PRNG seeded only by (seed, regionX, regionZ), so
// every chunk that touches the village re-derives the IDENTICAL layout
// and writes only its own share of blocks. Nothing depends on
// generation order, and nothing can be half a village.
//
// The layout itself mirrors StructureVillagePieces.Start: the well at
// the center, four streets leaving it on the cardinal axes, houses and
// farms branching off each street on one side only (so pieces of
// different arms can never collide), and a street lamp near each well
// face. Every piece rotates so its door faces the road, exactly like
// vanilla turns its templates.
//
// The streets themselves are a small GRAPH, not stripes: everything
// starts at the well (the village's centre) and walks outward. Each arm
// gets a lane that leaves the well face, runs right along the door wall
// of every structure on that arm (so houses, farms and the street lamp
// all stay connected to the network) and weaves gently across its own
// corridor (a sine wave, so no street is a ruler-straight line); a
// couple of branches fork off the open side of an arm and wander away
// with their own curve, ending in a widened head — the little dead-end
// alleys that make a village look lived in. Every cell remembers the
// cell it walked from, which is what lets the terrain gate follow the
// ground step by step instead of judging every column against the well
// alone.
//
// Materials are chosen from the WELL's biome (vanilla picks the
// village's material set from its start piece): a desert village comes
// out sandstone and sand, a plains/savanna village oak and cobble.
// The well is also the start piece as a GATE: if it cannot stand
// (lake, river, cliff, another biome) the whole village is dropped,
// instead of leaving roads that lead to nothing.
//
// The terrain verdict — the well's biome, each piece's base height,
// the height and surface block of every column a piece or a road
// touches — is read ONCE per region and cached, because a chunk of
// terrain is expensive to evaluate and every chunk of the village used
// to ask the same questions again. Placing a chunk is then just block
// writing.
#pragma once
#include "Populator.h"
#include "world/generator/TerrainShape.h"
#include <cstdint>
#include <utility>
#include <vector>

namespace cursestone::level::generator::populator {

// One placed piece of a village layout, in WORLD coordinates: the
// top-left corner of the rotated footprint plus its extent.
struct VillagePiecePlacement {
    int kind;   // structure::village::PieceKind
    int rot;    // total quarter turns (piece rotation + village rotation)
    int x, z;
    int w, h;
};

// The full layout of one region's village — a pure function of
// (seed, regionX, regionZ). populate() stamps exactly this, and the
// verification tools rebuild it through buildPlan() so they always test
// the real layout instead of a copy of it.
// One planned road column: its world position plus the index of the
// cell the road WALKED from (in pathCells; -1 = the cell leaves the
// well itself, so it is judged against the well's own ground). Parent
// indexes always point backwards, and after the layout drops the cells
// that fall inside a building every surviving cell still reaches a root
// through a chain of physically adjacent cells — the roads form one
// connected network hanging off the well.
struct VillagePathCell {
    int x = 0, z = 0;
    int parent = -1;
};

struct VillagePlan {
    std::vector<VillagePiecePlacement> pieces;     // well first
    std::vector<VillagePathCell> pathCells;        // world road columns
    int minX = 0, minZ = 0, maxX = 0, maxZ = 0;    // world bounding box (half-open)
    int wellCx = 0, wellCz = 0;                    // well centre (biome gate)
};

class VillagePopulator final : public Populator {
public:
    VillagePopulator() = default;

    const char* name() const override { return "Village"; }
    void populate(GenerationContext& ctx) override;

    // Build the layout of the village anchored in region (regionX,
    // regionZ). Deterministic: same arguments, byte-identical plan.
    static void buildPlan(int64_t seed, int regionX, int regionZ,
                          VillagePlan& out);

    // Does this village actually generate here? False when the well's
    // own footprint is unusable (lake, river, cliff, wrong biome) — the
    // case where the whole village is dropped instead of leaving orphan
    // roads. On true, outBiome/outWellH report the biome and ground
    // level the village builds with (the biome decides its materials).
    static bool villageStands(const TerrainNoise& noise, const VillagePlan& plan,
                              uint8_t& outBiome, int& outWellH);

    // Nanoseconds this populator has spent on the calling thread. The
    // verification tools read it to report the village's REAL marginal
    // cost per chunk: timing whole chunks only measures the rest of the
    // generator, which is an order of magnitude more expensive and
    // differs from place to place (trees, caves, ore).
    static uint64_t& nanosAccumulator();
};

} // namespace cursestone::level::generator::populator
