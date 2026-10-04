// SPDX-License-Identifier: LGPL-3.0-or-later
#include "world/generator/carver/CaveCarver.h"
#include "block/BlockIds.h"

#include <cmath>
#include <cstdlib>

namespace cursestone::level::generator::carver {

using cursestone::block::BlockId;

static inline uint8_t bid(const mcp_chunk_t* c, int x, int y, int z) {
    return mcp_chunk_get_block_id(c, x, y, z);
}

void CaveCarver::generate(int64_t seed, int32_t chunkX, int32_t chunkZ,
                          mcp_chunk_t* chunk) {
    // Per-chunk deterministic seed: independent of generation order.
    uint64_t mix = (uint64_t)(uint32_t)chunkX * 341873128712ULL ^
                   (uint64_t)(uint32_t)chunkZ * 132897987541ULL;
    GenRandom random((int64_t)((uint64_t)seed ^ mix));

    rand_.setSeed(random.getSeed());
    int64_t j = rand_.nextLong();
    int64_t k = rand_.nextLong();

    for (int32_t currChunkX = chunkX - kChunkRadius; currChunkX <= chunkX + kChunkRadius; ++currChunkX) {
        for (int32_t currChunkZ = chunkZ - kChunkRadius; currChunkZ <= chunkZ + kChunkRadius; ++currChunkZ) {
            int64_t j1 = (int64_t)currChunkX * j;
            int64_t k1 = (int64_t)currChunkZ * k;
            rand_.setSeed(j1 ^ k1 ^ random.getSeed());
            recursiveGenerate(currChunkX, currChunkZ, chunkX, chunkZ, chunk, true);
        }
    }
}

void CaveCarver::recursiveGenerate(int32_t chunkX, int32_t chunkZ,
                                    int32_t originChunkX, int32_t originChunkZ,
                                    mcp_chunk_t* chunk, bool addRooms) {
    int32_t numAttempts = (int32_t)rand_.nextInt(
        rand_.nextInt(rand_.nextInt(15) + 1) + 1);

    if (rand_.nextInt(100) > kDensity) numAttempts = 0;

    for (int32_t i = 0; i < numAttempts; ++i) {
        double startX = chunkX * 16 + (int32_t)rand_.nextInt(16);
        double startY = (int32_t)rand_.nextInt(kTopY - kBottomY) + kBottomY;
        double startZ = chunkZ * 16 + (int32_t)rand_.nextInt(16);

        int32_t numAddTunnelCalls = 1;

        if (addRooms && rand_.nextInt(4) == 0) {
            addRoom(rand_.nextLong(), originChunkX, originChunkZ, chunk, startX, startY, startZ);
            numAddTunnelCalls += (int32_t)rand_.nextInt(4);
        }

        for (int32_t j = 0; j < numAddTunnelCalls; ++j) {
            float yaw = rand_.nextFloat() * ((float)M_PI * 2);
            float pitch = (rand_.nextFloat() - 0.5F) * 2.0F / 8.0F;
            float width = rand_.nextFloat() * 2.0F + rand_.nextFloat();

            if (addRooms && rand_.nextInt(10) == 0) {
                width *= rand_.nextFloat() * rand_.nextFloat() * 3.0F + 1.0F;
            }

            addTunnel(rand_.nextLong(), originChunkX, originChunkZ, chunk,
                      startX, startY, startZ, width, yaw, pitch, 0, 0, 1.0);
        }
    }
}

void CaveCarver::addRoom(int64_t seed, int32_t originChunkX, int32_t originChunkZ,
                         mcp_chunk_t* chunk, double startX, double startY, double startZ) {
    addTunnel(seed, originChunkX, originChunkZ, chunk, startX, startY, startZ,
              1.0F + rand_.nextFloat() * 6.0F, 0.0F, 0.0F, -1, -1, 0.5);
}

void CaveCarver::addTunnel(int64_t seed, int32_t originChunkX, int32_t originChunkZ,
                           mcp_chunk_t* chunk, double startX, double startY, double startZ,
                           float width, float yaw, float pitch,
                           int32_t startCounter, int32_t endCounter, double heightModifier) {
    GenRandom random(seed);

    double originBlockX = (originChunkX * 16 + 8);
    double originBlockZ = (originChunkZ * 16 + 8);

    float yawModifier = 0.0F;
    float pitchModifier = 0.0F;

    if (endCounter <= 0) {
        int32_t i = kChunkRadius * 16 - 16;
        endCounter = i - (int32_t)random.nextInt(i / 4);
    }

    bool comesFromRoom = false;
    if (startCounter == -1) {
        startCounter = endCounter / 2;
        comesFromRoom = true;
    }

    int32_t randomCounterValue = (int32_t)(random.nextInt(endCounter / 2) + endCounter / 4);

    while (startCounter < endCounter) {
        double xzOffset = 1.5 + (double)(sin((float)startCounter * (float)M_PI / (float)endCounter) * width);
        double yOffset = xzOffset * heightModifier;

        float pitchXZ = cos(pitch);
        float pitchY = sin(pitch);
        startX += cos(yaw) * pitchXZ;
        startY += pitchY;
        startZ += sin(yaw) * pitchXZ;

        bool flag = random.nextInt(6) == 0;
        pitch = flag ? pitch * 0.92F : pitch * 0.7F;

        pitch += pitchModifier * 0.1F;
        yaw += yawModifier * 0.1F;

        pitchModifier *= 0.9F;
        yawModifier *= 0.75F;

        pitchModifier += (random.nextFloat() - random.nextFloat()) * random.nextFloat() * 2.0F;
        yawModifier += (random.nextFloat() - random.nextFloat()) * random.nextFloat() * 4.0F;

        if (!comesFromRoom && startCounter == randomCounterValue && width > 1.0F && endCounter > 0) {
            addTunnel(random.nextLong(), originChunkX, originChunkZ, chunk,
                      startX, startY, startZ,
                      random.nextFloat() * 0.5F + 0.5F, yaw - ((float)M_PI / 2.0F), pitch / 3.0F,
                      startCounter, endCounter, 1.0);
            addTunnel(random.nextLong(), originChunkX, originChunkZ, chunk,
                      startX, startY, startZ,
                      random.nextFloat() * 0.5F + 0.5F, yaw + ((float)M_PI / 2.0F), pitch / 3.0F,
                      startCounter, endCounter, 1.0);
            return;
        }

        if (comesFromRoom || random.nextInt(4) != 0) {
            double offX = startX - originBlockX;
            double offZ = startZ - originBlockZ;
            double distanceToEnd = endCounter - startCounter;
            double d7 = width + 2.0F + 16.0F;

            if (offX * offX + offZ * offZ - distanceToEnd * distanceToEnd > d7 * d7) {
                return;
            }

            if (startX >= originBlockX - 16.0 - xzOffset * 2.0 &&
                startZ >= originBlockZ - 16.0 - xzOffset * 2.0 &&
                startX <= originBlockX + 16.0 + xzOffset * 2.0 &&
                startZ <= originBlockZ + 16.0 + xzOffset * 2.0) {

                int32_t minX = (int32_t)(floor(startX - xzOffset) - originChunkX * 16 - 1);
                int32_t minY = (int32_t)(floor(startY - yOffset) - 1);
                int32_t minZ = (int32_t)(floor(startZ - xzOffset) - originChunkZ * 16 - 1);
                int32_t maxX = (int32_t)(floor(startX + xzOffset) - originChunkX * 16 + 1);
                int32_t maxY = (int32_t)(floor(startY + yOffset) + 1);
                int32_t maxZ = (int32_t)(floor(startZ + xzOffset) - originChunkZ * 16 + 1);

                if (minX < 0)   minX = 0;
                if (maxX > 16)  maxX = 16;
                if (minY < 1)   minY = 1;
                if (maxY > 127) maxY = 127;   // MCPE 0.15.x world height
                if (minZ < 0)   minZ = 0;
                if (maxZ > 16)  maxZ = 16;

                for (int32_t currX = minX; currX < maxX; ++currX) {
                    double xAxisDist = ((double)(currX + originChunkX * 16) + 0.5 - startX) / xzOffset;
                    for (int32_t currZ = minZ; currZ < maxZ; ++currZ) {
                        double zAxisDist = ((double)(currZ + originChunkZ * 16) + 0.5 - startZ) / xzOffset;
                        if (xAxisDist * xAxisDist + zAxisDist * zAxisDist < 1.0) {
                            for (int32_t currY = maxY; currY > minY; --currY) {
                                double yAxisDist = ((double)(currY - 1) + 0.5 - startY) / yOffset;
                                if (yAxisDist > -0.7 &&
                                    xAxisDist * xAxisDist + yAxisDist * yAxisDist + zAxisDist * zAxisDist < 1.0) {
                                    digBlock(chunk, (int8_t)currX, (int16_t)currY, (int8_t)currZ);
                                }
                            }
                        }
                    }
                }

                if (comesFromRoom) break;
            }
        }
        startCounter++;
    }
}

int CaveCarver::highestBlockAt(const mcp_chunk_t* c, int x, int z) {
    for (int y = 127; y >= 0; --y) {
        if (bid(c, x, y, z) != (uint8_t)BlockId::AIR) return y;
    }
    return 0;
}

bool CaveCarver::isLiquid(uint8_t id) {
    return id == (uint8_t)BlockId::WATER || id == (uint8_t)BlockId::FLOWING_WATER ||
           id == (uint8_t)BlockId::LAVA || id == (uint8_t)BlockId::FLOWING_LAVA;
}

void CaveCarver::digBlock(mcp_chunk_t* chunk, int8_t x, int16_t y, int8_t z) const {
    // Never break through the terrain surface: keep at least the top ground
    // block intact so caves do not punch holes in the sky or open the ocean
    // floor. The heightmap is set by the terrain pass (generate()) before
    // the carver runs.
    const int top = mcp_chunk_get_height_map(chunk, x, z);
    if (top > 1 && y >= top - 1) return;

    if (!canReplaceBlock(chunk, x, y, z)) return;

    if ((y - 1) < kLiquidAltitude) {
        mcp_chunk_set_block_id(chunk, x, y, z, (uint8_t)BlockId::LAVA);
    } else {
        mcp_chunk_set_block_id(chunk, x, y, z, (uint8_t)BlockId::AIR);
        int16_t healY = (int16_t)(y - 1);

        // How tall is the opening we have just carved in this column? Walk
        // up and down from the block we hollowed out while the neighbours
        // are already air. A tall opening (a tunnel breaking out of a
        // hillside, or a big room) is exactly the "carved 10+ blocks of
        // height" case the player asked about.
        int upAir = 0;
        for (int yy = y + 1; yy <= 127 && bid(chunk, x, yy, z) == (uint8_t)BlockId::AIR; ++yy)
            ++upAir;
        int downAir = 0;
        while ((healY - downAir) >= 1 &&
               bid(chunk, x, (int)(healY - downAir), z) == (uint8_t)BlockId::AIR)
            ++downAir;
        const int opening = upAir + downAir + 1;
        const int floorY = healY - downAir;

        if (opening >= 10) {
            // Vanilla: a deep carve (a river/carver cutting through a
            // mountain) gets a sandy floor two blocks thick instead of a
            // bare stone shelf. Only natural ground turns to sand; anything
            // else (ore, already-carved air) stops the skin immediately.
            for (int fy = floorY; fy >= floorY - 1 && fy >= 1; --fy) {
                uint8_t fb = bid(chunk, x, fy, z);
                if (fb == (uint8_t)BlockId::DIRT || fb == (uint8_t)BlockId::GRASS ||
                    fb == (uint8_t)BlockId::STONE || fb == (uint8_t)BlockId::SAND ||
                    fb == (uint8_t)BlockId::GRAVEL) {
                    mcp_chunk_set_block_id(chunk, x, fy, z, (uint8_t)BlockId::SAND);
                } else {
                    break;
                }
            }
        } else if (bid(chunk, x, healY, z) == (uint8_t)BlockId::DIRT &&
                   highestBlockAt(chunk, x, z) == healY) {
            mcp_chunk_set_block_id(chunk, x, healY, z, (uint8_t)BlockId::GRASS);
        }
    }
}

bool CaveCarver::canReplaceBlock(const mcp_chunk_t* chunk, int8_t x, int16_t y, int8_t z) const {
    uint8_t block = bid(chunk, x, y, z);
    uint8_t above = (y + 1 <= 127) ? bid(chunk, x, y + 1, z) : (uint8_t)BlockId::AIR;

    // Never damage logs/leaves (would leave floating tree parts).
    if (block == (uint8_t)BlockId::LOG || block == (uint8_t)BlockId::LEAVES ||
        block == (uint8_t)BlockId::WOOD_2 || block == (uint8_t)BlockId::LEAVES_2) {
        return false;
    }

    // Never carve a block directly under water/lava: that would drain the
    // body of liquid sitting on top of it (ocean / lake floors).
    if (isLiquid(above)) return false;

    if (y > kLiquidAltitude) {
        const int ox[4] = { x - 1, x + 1, x, x };
        const int oz[4] = { z, z, z - 1, z + 1 };
        for (int i = 0; i < 4; ++i) {
            if (ox[i] < 0 || ox[i] > 15 || oz[i] < 0 || oz[i] > 15) continue;
            if (isLiquid(bid(chunk, ox[i], y, oz[i]))) return false;
        }
    }

    if (block == (uint8_t)BlockId::STONE || block == (uint8_t)BlockId::DIRT ||
        block == (uint8_t)BlockId::GRASS || block == (uint8_t)BlockId::SANDSTONE ||
        block == (uint8_t)BlockId::SNOW_LAYER) {
        return true;
    }

    return (block == (uint8_t)BlockId::SAND || block == (uint8_t)BlockId::GRAVEL) && !isLiquid(above);
}

} // namespace cursestone::level::generator::carver
