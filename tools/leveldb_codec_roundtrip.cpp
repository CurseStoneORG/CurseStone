// Standalone LevelDBChunkCodec symmetry test:
//   snapshot -> encode (record) -> decode -> re-encode -> compare bytes.
// Build:
//   g++ -std=c++20 -Isrc -o /tmp/codectest tools/leveldb_codec_roundtrip.cpp \
//     src/world/format/leveldb/LevelDBChunkCodec.cpp src/world/format/Chunk.cpp \
//     src/world/format/ChunkSection.cpp src/nbt/NBT.cpp src/utils/BinaryStream.cpp \
//     <all the tag/nbt/util/biome sources it pulls> -lz -lpthread
#include <cstdio>
#include <cstring>
#include <random>
#include <vector>

#include "world/format/Chunk.h"
#include "world/format/leveldb/LevelDBChunkCodec.h"

using namespace cursestone::world::format::leveldb;

int main() {
    // Build a random-ish chunk (deterministic).
    mcp_chunk_t* c = mcp_chunk_new(3, -7);
    if (!c)
        return 1;
    std::mt19937 rng(12345);
    for (int sy = 0; sy < 8; ++sy) {
        mcp_chunk_section_t* s = mcp_chunk_get_or_alloc_section(c, (uint8_t)sy);
        if (!s)
            return 1;
        for (int i = 0; i < 4096; ++i)
            s->blocks[i] = (uint8_t)(rng() % 256);
        for (int i = 0; i < 2048; ++i) {
            s->data[i] = (uint8_t)(rng() % 256);
            s->sky_light[i] = (uint8_t)(rng() % 256);
            s->block_light[i] = (uint8_t)(rng() % 256);
        }
    }
    for (int i = 0; i < 256; ++i) {
        c->biomes[i] = (uint8_t)(rng() % 128);
        c->height_map[i] = rng() % 128;
    }
    c->terrain_populated = true;
    c->light_populated = true;

    // snapshot -> record (the save path)
    mcp_chunk_wire_snapshot_t* snap = mcp_chunk_wire_snapshot_create(c);
    if (!snap)
        return 1;
    size_t len1 = 0;
    uint8_t* rec1 = mcp_chunk_snapshot_legacy_terrain_serialize(snap, &len1);
    if (!rec1 || len1 != LevelDBChunkCodec::kLegacyTerrainSize) {
        fprintf(stderr, "encode failed (len=%zu)\n", len1);
        return 1;
    }

    // record -> chunk -> snapshot -> record (the load path)
    mcp_chunk_t* back = LevelDBChunkCodec::decode_legacy_terrain(rec1, len1, 3, -7);
    if (!back) {
        fprintf(stderr, "decode failed\n");
        return 1;
    }
    mcp_chunk_wire_snapshot_t* snap2 = mcp_chunk_wire_snapshot_create(back);
    if (!snap2)
        return 1;
    size_t len2 = 0;
    uint8_t* rec2 = mcp_chunk_snapshot_legacy_terrain_serialize(snap2, &len2);
    if (!rec2 || len2 != len1) {
        fprintf(stderr, "re-encode failed (len=%zu vs %zu)\n", len2, len1);
        return 1;
    }

    // Compare region by region.
    struct Region {
        const char* name;
        size_t lo, hi;
    };
    const Region regions[] = {
        {"blocks", 0, 32768},   {"data", 32768, 49152},   {"sky", 49152, 65536},
        {"light", 65536, 81920}, {"heightmap", 81920, 82176},
        {"biomes", 82176, 83200},
    };
    int bad = 0;
    for (const Region& r : regions) {
        bool eq = memcmp(rec1 + r.lo, rec2 + r.lo, r.hi - r.lo) == 0;
        printf("%-10s %s\n", r.name, eq ? "OK" : "MISMATCH");
        if (!eq) {
            ++bad;
            for (size_t i = r.lo; i < r.hi; ++i) {
                if (rec1[i] != rec2[i]) {
                    printf("   first diff at offset %zu (rec=0x%02x back=0x%02x)\n",
                           i, rec1[i], rec2[i]);
                    break;
                }
            }
        }
    }

    free(rec1);
    free(rec2);
    mcp_chunk_wire_snapshot_free(snap);
    mcp_chunk_wire_snapshot_free(snap2);
    mcp_chunk_destroy(c);
    mcp_chunk_destroy(back);
    printf("%s\n", bad == 0 ? "ALL REGIONS MATCH" : "FAILED");
    return bad == 0 ? 0 : 1;
}
