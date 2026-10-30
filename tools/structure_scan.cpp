// tools/structure_scan.cpp
//
// Offline structure census for the live generator. Builds a big chunk
// window with the exact same code path the server uses (generate +
// populate through VgenSource, neighbor writes delivered through a
// real INeighborBlockSink, spills drained on install) and then counts
// what actually landed in the BLOCKS:
//
//   villages   -> farmland / wooden doors / glass panes / grass paths
//   temples    -> sandstone stairs (desert pyramid), tripwire hooks (jungle)
//   mineshafts -> rail blocks
//   dungeons   -> mob spawners
//   wells      -> sandstone slabs high up in dry biomes
//
// Markers are clustered per type so one village spanning a dozen chunks
// counts once, and the report lists example world coordinates.
//
//   g++ -std=c++20 -O2 -Isrc tools/structure_scan.cpp -o /tmp/structure_scan
//   (link with the prebuilt generator objects)
#include "world/generator/vgen/VgenSource.h"
#include "world/format/Chunk.h"
#include "world/generator/NeighborChunkIO.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

using namespace cursestone::world::generator;
using vgen::Block;

namespace {

struct Key {
    int x, z;
    bool operator==(const Key& o) const { return x == o.x && z == o.z; }
};
struct KeyHash {
    size_t operator()(const Key& k) const {
        return (size_t)(uint32_t)k.x * 0x9E3779B9u ^ (size_t)(uint32_t)k.z;
    }
};

// Delivers generation-time writes aimed at any chunk of the window,
// mirroring ChunkManager::commitNeighborWrite's live-chunk branch.
struct GridSink : INeighborBlockSink {
    std::unordered_map<Key, mcp_chunk_t*, KeyHash>* grid = nullptr;
    void commitNeighborWrite(int cx, int cz, uint32_t index, uint8_t id,
                             uint8_t data) override {
        auto it = grid->find({cx, cz});
        if (it == grid->end()) return; // outside window: stays in the spill queue
        uint32_t y = index % 128;
        uint32_t rest = index / 128;
        uint32_t z = rest % 16;
        uint32_t x = rest / 16;
        mcp_chunk_set_block_full(it->second, (int)x, (int)y, (int)z, id, data);
    }
};

struct Marker {
    int type;
    int x, y, z;
};

enum MarkerType {
    T_VILLAGE = 0,
    T_TEMPLE_DESERT,
    T_TEMPLE_JUNGLE,
    T_MINESHAFT,
    T_DUNGEON,
    T_WELL,
    T_TYPE_COUNT
};
const char* typeName(int t) {
    static const char* names[T_TYPE_COUNT] = {"village", "desert temple",
                                              "jungle temple", "mineshaft",
                                              "dungeon", "desert well"};
    return names[t];
}

struct Cluster {
    int count = 0;   // markers merged
    long minX, maxX, minZ, maxZ;
    int bestY = 0;
};

bool idIn(uint8_t id, std::initializer_list<uint8_t> list) {
    for (uint8_t v : list)
        if (v == id) return true;
    return false;
}

} // namespace

int main(int argc, char** argv) {
    const int64_t seed = argc > 1 ? strtoll(argv[1], nullptr, 10) : 12345;
    const int R = argc > 2 ? atoi(argv[2]) : 40; // -R..R chunks

    VgenSource gen(seed);
    std::unordered_map<Key, mcp_chunk_t*, KeyHash> grid;
    grid.reserve((size_t)(2 * R + 1) * (2 * R + 1));
    GridSink sink;
    sink.grid = &grid;
    gen.setNeighborSink(&sink);

    std::vector<Marker> markers;
    std::map<uint8_t, long> biomes;
    long chunks = 0;
    auto t0 = std::chrono::steady_clock::now();
    auto lastReport = t0;

    for (int cz = -R; cz <= R; ++cz) {
        for (int cx = -R; cx <= R; ++cx) {
            mcp_chunk_t* c = mcp_chunk_new(cx, cz);
            grid[{cx, cz}] = c;
            GenerationContext ctx{};
            ctx.seed = seed;
            ctx.chunk_x = cx;
            ctx.chunk_z = cz;
            ctx.biomes_in = nullptr;
            ctx.chunk = c;
            gen.generate(ctx);
            gen.populate(ctx);
            gen.drainSpills(cx, cz, c); // install-time replay, like the server

            // ---- scan the finished blocks ----
            for (int x = 0; x < 16; ++x) {
                for (int z = 0; z < 16; ++z) {
                    biomes[mcp_chunk_get_biome(c, x, z)]++;
                    const int h = mcp_chunk_get_height_map(c, x, z);
                    for (int y = 0; y < 128; ++y) {
                        const uint8_t id = mcp_chunk_get_block_id(c, x, y, z);
                        if (id == 0) continue;
                        Marker m{};
                        m.x = cx * 16 + x;
                        m.z = cz * 16 + z;
                        m.y = y;
                        if (id == Block::mFarmland->mID ||
                            id == Block::mWoodenDoor->mID ||
                            id == Block::mGrassPathBlock->mID ||
                            (id == Block::mGlassPane->mID && y > 50)) {
                            m.type = T_VILLAGE;
                        } else if (id == Block::mSandstoneStairs->mID) {
                            m.type = T_TEMPLE_DESERT;
                        } else if (id == Block::mTripwireHook->mID) {
                            m.type = T_TEMPLE_JUNGLE;
                        } else if (id == Block::mRail->mID) {
                            m.type = T_MINESHAFT;
                        } else if (id == Block::mMobSpawner->mID) {
                            m.type = T_DUNGEON;
                        } else if (id == Block::mStoneSlab->mID && y > 58 &&
                                   h < y) {
                            m.type = T_WELL;
                        } else {
                            continue;
                        }
                        markers.push_back(m);
                    }
                }
            }
            ++chunks;
            mcp_chunk_destroy(c);
            grid.erase({cx, cz});

            auto now = std::chrono::steady_clock::now();
            if (chunks % 500 == 0 && now - lastReport > std::chrono::seconds(10)) {
                lastReport = now;
                printf("  ... %ld chunks, %zu markers, %.1fs\n", chunks,
                       markers.size(),
                       std::chrono::duration<double>(now - t0).count());
                fflush(stdout);
            }
        }
    }

    // ---- cluster markers per type (same structure spans many chunks) ----
    std::vector<Cluster> clusters[T_TYPE_COUNT];
    for (const Marker& m : markers) {
        bool merged = false;
        for (Cluster& c : clusters[m.type]) {
            const long cxm = (c.minX + c.maxX) / 2;
            const long czm = (c.minZ + c.maxZ) / 2;
            if (std::abs(m.x - cxm) <= 80 && std::abs(m.z - czm) <= 80) {
                c.count++;
                c.minX = std::min<long>(c.minX, m.x);
                c.maxX = std::max<long>(c.maxX, m.x);
                c.minZ = std::min<long>(c.minZ, m.z);
                c.maxZ = std::max<long>(c.maxZ, m.z);
                c.bestY = m.y;
                merged = true;
                break;
            }
        }
        if (!merged) {
            Cluster c;
            c.count = 1;
            c.minX = c.maxX = m.x;
            c.minZ = c.maxZ = m.z;
            c.bestY = m.y;
            clusters[m.type].push_back(c);
        }
    }

    const double secs = std::chrono::duration<double>(
                            std::chrono::steady_clock::now() - t0)
                            .count();
    printf("\n=== structure census (seed %lld, window %dx%d chunks, "
           "%.1fs) ===\n",
           (long long)seed, 2 * R + 1, 2 * R + 1, secs);
    printf("chunks scanned: %ld, markers: %zu\n", chunks, markers.size());
    for (int t = 0; t < T_TYPE_COUNT; ++t) {
        printf("%-14s clusters: %3zu   (marker blocks: %d)\n", typeName(t),
               clusters[t].size(), [&] {
                   int n = 0;
                   for (const Cluster& c : clusters[t]) n += c.count;
                   return n;
               }());
        for (size_t i = 0; i < clusters[t].size() && i < 6; ++i) {
            const Cluster& c = clusters[t][i];
            printf("    #%zu center (%ld, %d, %ld) span %ldx%ld, %d blocks\n",
                   i + 1, (c.minX + c.maxX) / 2, c.bestY, (c.minZ + c.maxZ) / 2,
                   c.maxX - c.minX + 1, c.maxZ - c.minZ + 1, c.count);
        }
    }

    // biome share of the window (id -> blocks), top 8
    printf("biomes (top 8 by block share):\n");
    std::vector<std::pair<long, uint8_t>> bio(biomes.begin(), biomes.end());
    std::sort(bio.begin(), bio.end(),
              [](const auto& a, const auto& b) { return a.first > b.first; });
    for (size_t i = 0; i < bio.size() && i < 8; ++i)
        printf("    biome %-3d %ld blocks\n", bio[i].second, bio[i].first);

    return 0;
}
