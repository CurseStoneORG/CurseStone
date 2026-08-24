// Level — a single world (overworld, nether, end). Holds the
// ChunkManager, generator, players, entities, tile-entities and
// per-world settings (time, weather, spawn position).
#pragma once
#include <string>
#include <memory>
#include <vector>
#include <unordered_map>
#include <cstdint>
#include <functional>
#include "math/Vector3.h"
#include "level/ChunkManager.h"
namespace atomikpe {
class Player; class Entity; class Tile;
namespace level::generator { class Generator; }
}
namespace atomikpe::level {
struct LevelConfig {
    std::string name;
    std::string format   = "mcacpp";  // mcacpp | leveldb | mca | mcapm
    std::string generator = "normal"; // normal | flat | nether | void
    int64_t seed = 0;
    int32_t time = 0;
    int32_t spawn_x = 0, spawn_y = 64, spawn_z = 0;
    int32_t view_distance = 8;
};
class Level {
public:
    explicit Level(LevelConfig cfg);
    ~Level();
    const std::string& name() const { return cfg_.name; }
    int64_t seed() const { return cfg_.seed; }
    const LevelConfig& config() const { return cfg_; }
    ChunkManager& chunks() { return chunks_; }
    // Returns the block id at world coords. Loads chunk if needed.
    uint8_t getBlockId(int32_t x, int32_t y, int32_t z);
    void setBlock(int32_t x, int32_t y, int32_t z, uint8_t id, uint8_t data);
    // World time (0..24000). Incremented each tick (20/s).
    int32_t time() const { return cfg_.time; }
    void setTime(int32_t t) { cfg_.time = t; }
    void advanceTime(int dt_ticks) { cfg_.time = (cfg_.time + dt_ticks) % 24000; }
    // Spawn point.
    mcp_vector3_t spawn() const { return spawn_; }
    void setSpawn(mcp_vector3_t v) { spawn_ = v; }
    // Save all dirty chunks.
    int save();
    // Tick this level — entities, tile entities, scheduled blocks.
    void tick(uint64_t current_tick);
    // Generator accessor for AsyncPool workers.
    std::shared_ptr<generator::Generator> generator() const { return generator_; }
    // ChunkLoaders (players) registered to receive chunk updates.
    void registerLoader(uint64_t loader_id, class ChunkLoader* loader);
    void unregisterLoader(uint64_t loader_id);
private:
    LevelConfig cfg_;
    ChunkManager chunks_;
    std::shared_ptr<generator::Generator> generator_;
    mcp_vector3_t spawn_;
    std::unordered_map<uint64_t, class ChunkLoader*> loaders_;
};
} // namespace
